// Firmware-backed round trip for the randomize gestures: request the current
// pattern, decode it, flip its trigs, send it back, and read it again.
#include "mdAutomationTestSupport.h"
#include "mdLib/mdpatterndump.h"
#include "mdLib/mmpatterndump.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdpanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

int main()
{
	using namespace mdAutomationTest;
	try
	{
		const auto model = std::getenv("MODEL") != nullptr && std::string(std::getenv("MODEL")) == "MM"
			? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
		Harness harness(model);
		if(!harness.hasLocalFirmware())
			return allowMissingFirmware("mdPatternDumpFirmwareTest", model) ? 0 : SkipReturnCode;
		harness.prepare();
		auto& controller = harness.controller;
		// REALTIME=1 mimics a DAW: the host callback is realtime and the controller
		// is drained separately, as its UI timer would.
		const bool realtime = std::getenv("REALTIME") != nullptr;
		if(realtime)
			harness.audioProcessor.setNonRealtime(false);
		auto pump = [&](const int _blocks)
		{
			for(int i = 0; i < _blocks; ++i)
			{
				harness.process(1);
				if(realtime)
					controller.processOfflineControllerWork();
			}
		};
		bool synced = false;
		for(int i = 0; i < 12000 && !synced; ++i) { pump(1); synced = controller.isAutomationSynchronized(); }
		require(synced, "initial firmware synchronization timed out: " + harness.firmwareReadiness());
		std::printf("mode: %s\n", realtime ? "realtime" : "offline");

		std::vector<uint8_t> captured;
		controller.setPatternDumpListener([&captured](const std::vector<uint8_t>& _dump) { captured = _dump; });

		// SYNCTEST=<bpm>: put the MD on external tempo/transport by editing its
		// global dump, write a four-on-the-floor pattern, run the host transport
		// and time the audio onsets the machine produces.
		if(const auto* const syncEnv = std::getenv("SYNCTEST"))
		{
			std::vector<uint8_t> global;
			controller.setSysexListener([&global](const std::vector<uint8_t>& _m)
			{
				if(_m.size() > 12 && _m[6] == 0x50 && _m[4] == 0x02) global = _m;
			});
			controller.sendSysexToDevice(md::automation::sysex::globalRequest(md::MachineModel::Machinedrum, 0));
			for(int i = 0; i < 2000 && global.empty(); ++i) pump(1);
			require(!global.empty(), "no global dump");
			std::printf("global dump %zu bytes; sync byte 0x%02x tempo %u\n", global.size(), global[178], (global[175] << 7) | global[176]);
			const bool internal = std::getenv("SYNCTEST_INTERNAL") != nullptr;
			global[178] = static_cast<uint8_t>((internal ? (global[178] & ~0x01) : (global[178] | 0x01)) & ~0x10);	// TEMPO IN, CTRL IN = ON
			std::printf("mode: %s tempo, %s\n", internal ? "internal" : "external", realtime ? "realtime" : "offline");
			{
				const auto checksumPos = global.size() - 5;
				uint32_t sum = 0; for(size_t i = 9; i < checksumPos; ++i) sum += global[i];
				global[checksumPos] = static_cast<uint8_t>(sum >> 7 & 0x7f); global[checksumPos + 1] = static_cast<uint8_t>(sum & 0x7f);
			}
			if(internal) { global[175] = static_cast<uint8_t>((120 * 24) >> 7); global[176] = static_cast<uint8_t>((120 * 24) & 0x7f); }
			{
				const auto checksumPos = global.size() - 5;
				uint32_t sum = 0; for(size_t i = 9; i < checksumPos; ++i) sum += global[i];
				global[checksumPos] = static_cast<uint8_t>(sum >> 7 & 0x7f); global[checksumPos + 1] = static_cast<uint8_t>(sum & 0x7f);
			}
			controller.sendSysexToDevice(global);
			pump(300);
			// Reload global slot 0 so the running machine adopts it, then read it back.
			controller.sendSysexToDevice({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x71, 0x01, 0x00, 0xf7});
			pump(300);
			global.clear();
			controller.sendSysexToDevice(md::automation::sysex::globalRequest(md::MachineModel::Machinedrum, 0));
			for(int i = 0; i < 2000 && global.empty(); ++i) pump(1);
			require(!global.empty(), "no global dump after reload");
			std::printf("global after reload: sync byte 0x%02x tempo %u\n", global[178], (global[175] << 7) | global[176]);
			// Pattern: track 1 trig on every beat.
			captured.clear();
			controller.requestCurrentPatternDump();
			for(int i = 0; i < 6000 && captured.empty(); ++i) pump(1);
			require(!captured.empty(), "no pattern dump");
			auto pattern = md::patternDump::decode(captured);
			require(pattern.has_value(), "pattern decode failed");
			for(auto& t : pattern->trigs) t = 0;
			pattern->trigs[0] = 0x1111ull;
			pattern->patternLength = 16;
			controller.sendSysexToDevice(md::patternDump::encode(*pattern));
			pump(400);
			// Re-select the pattern so the sequencer plays the stored version, then verify.
			controller.sendSysexToDevice({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x71, 0x04, pattern->position, 0xf7});
			pump(300);
			captured.clear();
			controller.requestCurrentPatternDump();
			for(int i = 0; i < 6000 && captured.empty(); ++i) pump(1);
			{
				const auto check = md::patternDump::decode(captured);
				std::printf("pattern after write: track1 trigs 0x%llx, length %u\n",
					check ? static_cast<unsigned long long>(check->trigs[0]) : 0ull, check ? check->patternLength : 0);
			}

			class PlayingPlayHead final : public juce::AudioPlayHead
			{
			public:
				double bpm = 120.0; double ppq = 0.0;
				juce::Optional<PositionInfo> getPosition() const override
				{
					PositionInfo r; r.setIsPlaying(true); r.setIsRecording(false); r.setBpm(bpm); r.setPpqPosition(ppq); return r;
				}
			} playHead;
			playHead.bpm = std::atof(syncEnv);
			const double rate = std::getenv("HOST_SAMPLERATE") ? std::atof(std::getenv("HOST_SAMPLERATE")) : 48000.0;
			harness.audioProcessor.setPlayHead(&playHead);
			const int seconds = 24;
			const int blocks = static_cast<int>(seconds * rate / BlockSize);
			std::vector<double> onsets; double quiet = 0; int quietBlocks = 0;
			// Step-LED chase: time every newly lit step LED (one per 16th note).
			std::vector<double> stepEvents; uint32_t previousLit = 0;
			for(int i = 0; i < blocks; ++i)
			{
				pump(1);
				{
					uint32_t lit = 0;
					harness.processor.getPlugin().withDeviceLocked([&lit](synthLib::Device* const _device)
					{
						if(auto* const device = dynamic_cast<md::Device*>(_device))
						{
							const auto panel = device->getHardware().getFrontPanelSnapshot();
							for(uint32_t k = 0; k < 16; ++k)
								if(panel.getStepLed(k)) lit |= 1u << k;
						}
					});
					const auto newlyLit = lit & ~previousLit;
					if(newlyLit && i > 0)
						stepEvents.push_back(static_cast<double>(i) * BlockSize / rate);
					previousLit = lit;
				}
				playHead.ppq += static_cast<double>(BlockSize) / rate * playHead.bpm / 60.0;
				double energy = 0;
				for(int c = 0; c < harness.audio.getNumChannels(); ++c)
				{
					const auto* d = harness.audio.getReadPointer(c);
					for(int n = 0; n < BlockSize; ++n) energy += d[n] * d[n];
				}
				const double rms = std::sqrt(energy / (BlockSize * harness.audio.getNumChannels()));
				// A hit: energy jumps to at least 4x the previous block and above a floor.
				const double t = static_cast<double>(i) * BlockSize / rate;
				if(rms > 0.02 && rms > 2.0 * quiet && (onsets.empty() || t - onsets.back() > 0.25))
					onsets.push_back(t);
				quiet = rms;
				(void)quietBlocks;
			}
			if(stepEvents.size() > 20)
			{
				double sum = 0, mn = 1e9, mx = 0; size_t n = 0;
				for(size_t k = 1; k < stepEvents.size(); ++k)
				{
					if(stepEvents[k] < 4.0) continue;
					const auto iv = stepEvents[k] - stepEvents[k - 1];
					sum += iv; mn = std::min(mn, iv); mx = std::max(mx, iv); ++n;
				}
				const auto first = std::lower_bound(stepEvents.begin(), stepEvents.end(), 4.0);
				const auto span = stepEvents.back() - *first; const auto steps = static_cast<double>(stepEvents.end() - first - 1);
				std::printf("step LEDs: %zu events; mean 16th %.5f s (min %.4f max %.4f) -> %.3f bpm; baseline %.3f s / %.0f steps = %.3f bpm  [host expects %.5f s]\n",
					stepEvents.size(), sum / n, mn, mx, 15.0 / (sum / n), span, steps, 15.0 / (span / steps), 15.0 / playHead.bpm);
			}
			else
				std::printf("step LEDs: only %zu events\n", stepEvents.size());
			std::printf("onsets: %zu\n", onsets.size());
			double sum = 0, mn = 1e9, mx = 0; size_t n = 0;
			for(size_t i = 1; i < onsets.size(); ++i)
			{
				const auto iv = onsets[i] - onsets[i - 1];
				if(i <= 6 || i + 3 >= onsets.size()) std::printf("  onset %zu at %.3f s, interval %.4f s\n", i, onsets[i], iv);
				if(onsets[i] > 4.0) { sum += iv; mn = std::min(mn, iv); mx = std::max(mx, iv); ++n; }
			}
			if(n)
				std::printf("after 4 s: %zu intervals, mean %.4f s (%.2f bpm), min %.4f, max %.4f  [host %.1f bpm expects %.4f s]\n",
					n, sum / n, 60.0 / (sum / n), mn, mx, playHead.bpm, 60.0 / playHead.bpm);
			// Long-baseline estimate: first to last onset.
			if(onsets.size() > 8)
			{
				const auto beats = std::round((onsets.back() - onsets[4]) / (60.0 / playHead.bpm) * 0.96);
				std::printf("baseline: %.3f s over %zu hits -> %.4f s per hit = %.2f bpm\n", onsets.back() - onsets[4], onsets.size() - 5,
					(onsets.back() - onsets[4]) / (onsets.size() - 5), 60.0 / ((onsets.back() - onsets[4]) / (onsets.size() - 5)));
				(void)beats;
			}
			return 0;
		}

		// CLOCKTEST=<bpm>: play the host transport at that tempo for 10 s and count
		// the MIDI bytes the firmware consumes (24 clock bytes per beat expected).
		if(const auto* const clockEnv = std::getenv("CLOCKTEST"))
		{
			class PlayingPlayHead final : public juce::AudioPlayHead
			{
			public:
				double bpm = 120.0; double ppq = 0.0;
				juce::Optional<PositionInfo> getPosition() const override
				{
					PositionInfo r; r.setIsPlaying(true); r.setIsRecording(false); r.setBpm(bpm); r.setPpqPosition(ppq); return r;
				}
			} playHead;
			playHead.bpm = std::atof(clockEnv);
			harness.audioProcessor.setPlayHead(&playHead);
			pump(200);	// let start/continue settle
			const auto before = harness.telemetry().consumed;
			const double rate = std::getenv("HOST_SAMPLERATE") ? std::atof(std::getenv("HOST_SAMPLERATE")) : 48000.0;
			const int blocks = static_cast<int>(10.0 * rate / BlockSize);
			// Timing regularity: ticks consumed per host block, and the spread of
			// gaps (in blocks) between consecutive ticks.
			std::vector<int> perBlock; int lastTickBlock = -1, minGap = 1 << 30, maxGap = 0; uint64_t previous = before;
			for(int i = 0; i < blocks; ++i)
			{
				playHead.ppq += static_cast<double>(BlockSize) / rate * playHead.bpm / 60.0;
				pump(1);
				const auto now = harness.telemetry().consumed;
				const int ticks = static_cast<int>(now - previous); previous = now;
				perBlock.push_back(ticks);
				if(ticks > 0)
				{
					if(lastTickBlock >= 0) { minGap = std::min(minGap, i - lastTickBlock); maxGap = std::max(maxGap, i - lastTickBlock); }
					lastTickBlock = i;
				}
			}
			int maxPerBlock = 0; for(const auto t : perBlock) maxPerBlock = std::max(maxPerBlock, t);
			const double blockMs = 1000.0 * BlockSize / rate, tickMs = 60000.0 / playHead.bpm / 24.0;
			std::printf("timing: block %.2f ms, ideal tick spacing %.2f ms = %.2f blocks; observed gaps %d..%d blocks, max ticks in one block %d\n",
				blockMs, tickMs, tickMs / blockMs, minGap, maxGap, maxPerBlock);
			const auto after = harness.telemetry().consumed;
			const double expected = playHead.bpm / 60.0 * 24.0 * 10.0;
			std::printf("clock test @%.0f Hz: host %.1f bpm for 10 s -> firmware consumed %llu MIDI bytes (expected ~%.0f clock ticks)\n",
				rate, playHead.bpm, static_cast<unsigned long long>(after - before), expected);
			return 0;
		}

		if(model == md::MachineModel::Monomachine)
		{
			auto fetchMM = [&]() -> std::optional<md::mmPatternDump::Pattern>
			{
				captured.clear();
				controller.requestCurrentPatternDump();
				for(int i = 0; i < 6000 && captured.empty(); ++i)
					pump(1);
				std::printf("MM pattern dump: %zu bytes%s\n", captured.size(), captured.empty() ? " (NO REPLY)" : "");
				if(captured.empty())
					return std::nullopt;
				std::string error;
				auto pattern = md::mmPatternDump::decode(captured, &error);
				std::printf("decode: %s (version 0x%02x pos %u len %zu rows %zu)\n", pattern ? "ok" : error.c_str(),
					captured[7], captured[9], pattern ? pattern->stepCount() : 0, pattern ? pattern->rowCount() : 0);
				return pattern;
			};
			auto pattern = fetchMM();
			require(pattern.has_value(), "no decodable MM pattern dump");
			pattern->setTrigs(0, 0x9249ull);
			pattern->setNote(0, 0, 60); pattern->setNote(0, 3, 67);
			require(pattern->setLock(0, 0, 0, 100) && pattern->setLock(0, 9, 3, 40), "MM setLock failed");
			const auto encoded = md::mmPatternDump::encode(*pattern);
			std::printf("sending %zu bytes\n", encoded.size());
			controller.sendSysexToDevice(encoded);
			pump(400);
			auto verify = fetchMM();
			require(verify.has_value(), "no MM pattern dump after send");
			std::printf("track 1 trigs 0x%llx notes %u %u, lock(0,0)@0=%u lock(0,9)@3=%u, rows %zu\n",
				static_cast<unsigned long long>(verify->trigs(0)), verify->note(0, 0), verify->note(0, 3),
				verify->hasLock(0, 0) ? verify->row(verify->rowIndex(0, 0))[0] : 255,
				verify->hasLock(0, 9) ? verify->row(verify->rowIndex(0, 9))[3] : 255, verify->rowCount());
			require(verify->trigs(0) == 0x9249ull && verify->note(0, 3) == 67, "MM firmware did not store trigs/notes");
			require(verify->hasLock(0, 0) && verify->row(verify->rowIndex(0, 0))[0] == 100, "MM firmware did not store the lock");
			std::printf("track models: ");
			for(uint8_t t = 0; t < 6; ++t) std::printf("%x ", controller.getTrackModel(t));
			std::printf("\nmdPatternDumpFirmwareTest (MM): PASS\n");
			return 0;
		}

		auto fetch = [&]() -> std::optional<md::patternDump::Pattern>
		{
			captured.clear();
			controller.requestCurrentPatternDump();
			for(int i = 0; i < 6000 && captured.empty(); ++i)
				pump(1);
			std::printf("pattern dump: %zu bytes%s\n", captured.size(),
				captured.empty() ? " (NO REPLY)" : "");
			if(captured.empty())
				return std::nullopt;
			std::string error;
			auto pattern = md::patternDump::decode(captured, &error);
			std::printf("decode: %s (version 0x%02x rev %u pos %u len %u rows %zu ext %d extra %d)\n",
				pattern ? "ok" : error.c_str(), captured[7], captured[8], captured[9],
				pattern ? pattern->patternLength : 0, pattern ? pattern->rows.size() : 0,
				pattern ? pattern->hasExtension : 0, pattern ? pattern->extra : 0);
			return pattern;
		};

		// HOLD_FUNCTION=1 keeps the FUNCTION key pressed while requesting, as the
		// editor's FUNCTION+DOWN chord does when Shift is still held.
		const bool holdFunction = std::getenv("HOLD_FUNCTION") != nullptr;
		auto panel = [&](const uint8_t _row, const uint8_t _mask)
		{
			harness.processor.getPlugin().withDeviceLocked([&](synthLib::Device* const _device)
			{
				if(auto* const device = dynamic_cast<md::Device*>(_device))
					device->sendPanelEvent(_row, _mask);
			});
		};
		if(holdFunction)
		{
			const auto packet = md::panelPacket(md::MachineModel::Machinedrum, md::PanelControl::Function);
			require(packet.has_value(), "no FUNCTION packet");
			panel(packet->row, packet->mask);
			pump(60);
			std::printf("FUNCTION held (row 0x%02x mask 0x%02x)\n", packet->row, packet->mask);
		}
		auto pattern = fetch();
		if(holdFunction)
		{
			const auto packet = md::panelPacket(md::MachineModel::Machinedrum, md::PanelControl::Function);
			panel(packet->row, 0);
			pump(60);
			std::printf("FUNCTION released; retrying fetch\n");
			if(!pattern) pattern = fetch();
		}
		require(pattern.has_value(), "no decodable pattern dump");
		const uint64_t newTrigs = 0x9249ull;	// steps 1,4,7,10,13,16
		pattern->trigs[0] = newTrigs;
		require(pattern->setLock(0, 0, 0, 100) && pattern->setLock(0, 0, 3, 40), "setLock failed");
		const auto encoded = md::patternDump::encode(*pattern);
		std::printf("sending %zu bytes\n", encoded.size());
		controller.sendSysexToDevice(encoded);
		pump(400);

		auto verify = fetch();
		require(verify.has_value(), "no pattern dump after send");
		std::printf("track 1 trigs after send: 0x%llx (expected 0x%llx), PTCH lock step 1 = %u, step 4 = %u\n",
			static_cast<unsigned long long>(verify->trigs[0]), static_cast<unsigned long long>(newTrigs),
			verify->hasLock(0, 0) ? verify->rows[verify->rowIndex(0, 0)][0] : 255,
			verify->hasLock(0, 0) ? verify->rows[verify->rowIndex(0, 0)][3] : 255);
		require(verify->trigs[0] == newTrigs, "firmware did not store the sent trigs");
		require(verify->hasLock(0, 0) && verify->rows[verify->rowIndex(0, 0)][0] == 100, "firmware did not store the sent lock");
		// Randomize exclusions must survive a DAW state save/load round trip.
		{
			using Aspect = mdJucePlugin::Controller::RandomizeAspect;
			controller.setExcludeMask(Aspect::Trigs, 0x0005);
			controller.setExcludeMask(Aspect::Machines, 0x8000);
			controller.setExcludeMask(Aspect::Locks, 0x0f0f);
			controller.setTrigChancePercent(23);
			juce::MemoryBlock state;
			harness.audioProcessor.getStateInformation(state);
			controller.setExcludeMask(Aspect::Trigs, 0); controller.setExcludeMask(Aspect::Machines, 0); controller.setExcludeMask(Aspect::Locks, 0);
			controller.setTrigChancePercent(50);
			harness.audioProcessor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			pump(50);
			std::printf("exclusions after state reload: %04x %04x %04x\n", controller.getExcludeMask(Aspect::Trigs),
				controller.getExcludeMask(Aspect::Machines), controller.getExcludeMask(Aspect::Locks));
			require(controller.getExcludeMask(Aspect::Trigs) == 0x0005 && controller.getExcludeMask(Aspect::Machines) == 0x8000
				&& controller.getExcludeMask(Aspect::Locks) == 0x0f0f, "randomize exclusions did not survive state reload");
			std::printf("trig chance after state reload: %d\n", controller.getTrigChancePercent());
			require(controller.getTrigChancePercent() == 23, "trig chance did not survive state reload");
		}
		std::printf("track models: ");
		for(uint8_t t = 0; t < 16; ++t) std::printf("%x ", controller.getTrackModel(t));
		std::printf("\nmdPatternDumpFirmwareTest: PASS\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("mdPatternDumpFirmwareTest: FAIL: %s\n", e.what());
		return 1;
	}
}
