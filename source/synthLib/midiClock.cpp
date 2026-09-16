#include "midiClock.h"
#include "midiTypes.h"
#include "plugin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace synthLib
{
	static constexpr double ClockTicksPerQuarter = 24.0;

	void MidiClock::process(const double _bpm, const double _ppqPos, const bool _isPlaying,
		const size_t _sampleCount, const bool _ppqKnown)
	{
		// Hosts may omit BPM when stopping. Transport edges must still reach the
		// instrument; stopped transport intentionally does not generate clocks.
		if(!_isPlaying)
		{
			if(m_isPlaying) stop();
			return;
		}
		if(!_sampleCount) return;
		const double rate = m_plugin.getHostSamplerate();
		if(!std::isfinite(rate) || rate <= 0) return;
		if(std::isfinite(_bpm) && _bpm > 0 && _bpm <= rate * 60 / ClockTicksPerQuarter)
			m_lastBpm = _bpm;
		if(m_lastBpm <= 0) return;

		const auto samplesPerClock = rate * 60.0 / (m_lastBpm * ClockTicksPerQuarter);
		const bool positionKnown = _ppqKnown && std::isfinite(_ppqPos) && std::abs(_ppqPos) < 1e12;
		const auto ppq = positionKnown ? _ppqPos : 0.0;
		m_lastPpq = ppq; m_lastRate = rate; m_lastCount = _sampleCount;

		if(m_isPlaying && positionKnown)
		{
			// Where the free-running clock believes the host is, in ticks.
			const auto ourTicks = static_cast<double>(m_nextClockTick) - m_samplesToNextTick / samplesPerClock;
			const auto drift = std::abs(ppq * ClockTicksPerQuarter - ourTicks);
			m_maxDriftTicks = std::max(m_maxDriftTicks, drift);
			if(drift >= ClockTicksPerQuarter)
			{
				++m_relocates;
				stop();
			}
			else if(drift > 0.5)
				++m_driftBlocks;
		}
		if(!m_isPlaying)
		{
			start(ppq);
			// First tick lands exactly on the next 24 PPQN grid line.
			m_samplesToNextTick = (static_cast<double>(m_nextClockTick) - ppq * ClockTicksPerQuarter) * samplesPerClock;
			if(m_samplesToNextTick < 0) m_samplesToNextTick = 0;
		}

		static const bool trace = std::getenv("GEARMULATOR_CLOCK_TRACE") != nullptr;
		while(m_samplesToNextTick < static_cast<double>(_sampleCount))
		{
			const auto offset = std::max(0.0, std::ceil(m_samplesToNextTick - 1e-7));
			if(trace)
				std::fprintf(stderr, "[GEN] tick=%lld ppq=%.6f count=%zu offset=%.0f\n",
					static_cast<long long>(m_nextClockTick), ppq, _sampleCount, offset);
			m_plugin.insertMidiEvent({MidiEventSource::Internal, M_TIMINGCLOCK, 0, 0,
				static_cast<uint32_t>(std::min(offset, static_cast<double>(_sampleCount - 1)))});
			m_samplesToNextTick += samplesPerClock;
			++m_nextClockTick;
		}
		m_samplesToNextTick -= static_cast<double>(_sampleCount);
	}

	void MidiClock::restart()
	{
		stop();
	}

	void MidiClock::start(const double _ppqPos)
	{
		m_nextClockTick = static_cast<int64_t>(std::ceil(_ppqPos * ClockTicksPerQuarter - 1e-9));
		m_isPlaying = true;
		if(_ppqPos <= 0)
			m_plugin.insertMidiEvent({MidiEventSource::Internal, M_START});
		else
		{
			// MIDI SPP has six clocks per unit (one sixteenth note), and a
			// 14-bit range. Clock phase remains at the host's 24-PPQN position.
			const auto position = static_cast<uint32_t>(std::fmod(std::floor(_ppqPos * 4), 16384.0));
			m_plugin.insertMidiEvent({MidiEventSource::Internal, M_SONGPOSITION,
				static_cast<uint8_t>(position & 127), static_cast<uint8_t>(position >> 7)});
			m_plugin.insertMidiEvent({MidiEventSource::Internal, M_CONTINUE});
		}
	}

	void MidiClock::stop()
	{
		m_isPlaying = false;
		m_plugin.insertMidiEvent({MidiEventSource::Internal, M_STOP});
	}
}
