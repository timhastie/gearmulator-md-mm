#pragma once

// Monomachine pattern dump (SysEx 0x67, product 0x03) codec. The payload is
// 7-bit packed and RLE compressed; decoded it is a fixed 6520-byte image whose
// layout follows the MCL project's MNMPattern. Edits are made in place on that
// image so every field this code does not understand survives untouched.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace md::mmPatternDump
{
	constexpr size_t g_tracks = 6;
	constexpr size_t g_steps = 64;
	constexpr size_t g_params = 56;	// 7 pages x 8 (SYNTH, AMP, FILTER, FX, LFO1-3)
	constexpr size_t g_maxRows = 62;
	constexpr size_t g_decodedSize = 6520;
	constexpr uint8_t g_noLock = 0xff;

	struct Pattern
	{
		uint8_t version = 0, revision = 0, position = 0;
		std::vector<uint8_t> data;	// decoded image, g_decodedSize bytes

		uint64_t trigs(uint8_t _track) const;			// AMP trigs = note trigs
		void setTrigs(uint8_t _track, uint64_t _mask);	// AMP+FILTER+LFO set, OFF/TRIGLESS/CHORD cleared for the track
		uint8_t note(uint8_t _track, uint8_t _step) const;
		void setNote(uint8_t _track, uint8_t _step, uint8_t _note);
		size_t stepCount() const;
		uint64_t lockMask(uint8_t _track) const;
		bool hasLock(uint8_t _track, uint8_t _param) const;
		size_t rowIndex(uint8_t _track, uint8_t _param) const;
		size_t rowCount() const;
		uint8_t* row(size_t _index);
		bool setLock(uint8_t _track, uint8_t _param, uint8_t _step, uint8_t _value);
		void clearTrackLocks(uint8_t _track);
	};

	std::vector<uint8_t> request(uint8_t _pattern);
	bool isPatternDump(const std::vector<uint8_t>& _message);
	std::optional<Pattern> decode(const std::vector<uint8_t>& _message, std::string* _error = nullptr);
	std::vector<uint8_t> encode(const Pattern& _pattern);
}
