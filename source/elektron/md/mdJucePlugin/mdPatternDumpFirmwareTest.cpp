// Firmware-backed round trip for the randomize gestures: request the current
// pattern, decode it, flip its trigs, send it back, and read it again.
#include "mdAutomationTestSupport.h"
#include "mdLib/mdpatterndump.h"
#include "mdLib/mdpanel.h"

#include <cstdio>

int main()
{
	using namespace mdAutomationTest;
	try
	{
		Harness harness(md::MachineModel::Machinedrum);
		if(!harness.hasLocalFirmware())
			return allowMissingFirmware("mdPatternDumpFirmwareTest", md::MachineModel::Machinedrum) ? 0 : SkipReturnCode;
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
