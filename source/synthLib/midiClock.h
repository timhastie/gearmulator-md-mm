#pragma once

#include <cstddef>
#include <cstdint>

namespace synthLib
{
	class Plugin;

	class MidiClock
	{
	public:
		explicit MidiClock(Plugin& _plugin) : m_plugin(_plugin) {}

		void process(double _bpm, double _ppqPos, bool _isPlaying, size_t _sampleCount, bool _ppqKnown = true);

		void restart();
		// Diagnostics. Ticks run free from the tempo; the host position only
		// detects jumps of a beat or more (loop wrap, seek), which relocate with
		// STOP / song position / CONTINUE. Blocks whose host position strays more
		// than half a tick from the free-running phase are counted as drift.
		uint64_t getRephaseCount() const { return m_driftBlocks; }
		uint64_t getRelocateCount() const { return m_relocates; }
		double getMaxDriftTicks() const { return m_maxDriftTicks; }
		double getLastBpm() const { return m_lastBpm; }
		double getLastPpq() const { return m_lastPpq; }
		double getLastRate() const { return m_lastRate; }
		size_t getLastCount() const { return m_lastCount; }
		uint64_t getTicksEmitted() const { return static_cast<uint64_t>(m_nextClockTick); }
	private:
		void stop();
		void start(double _ppqPos);

		Plugin& m_plugin;

		bool m_isPlaying = false;
		int64_t m_nextClockTick = 0;
		double m_samplesToNextTick = 0.0;	// free-running phase, in host samples
		double m_lastBpm = 0.0;
		uint64_t m_driftBlocks = 0;
		uint64_t m_relocates = 0;
		double m_maxDriftTicks = 0.0;
		double m_lastPpq = 0.0, m_lastRate = 0.0;
		size_t m_lastCount = 0;
	};
}
