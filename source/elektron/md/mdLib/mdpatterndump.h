#pragma once

// Machinedrum pattern dump (SysEx 0x67) codec used by the editor's randomize
// gestures. The base layout follows the public Elektron SysEx reference; the
// trailing SPS-X extension block emitted by the unofficial X firmware is kept
// opaque except for the lock-row count and the high lock-mask bits it carries.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace md::patternDump
{
	constexpr uint8_t g_patternDump = 0x67;
	constexpr uint8_t g_patternRequest = 0x68;
	constexpr size_t g_tracks = 16;
	constexpr size_t g_maxSteps = 64;
	constexpr size_t g_classicParams = 24;
	constexpr size_t g_maxRows = 64;
	constexpr uint8_t g_noLock = 0xff;

	using Row = std::array<uint8_t, g_maxSteps>;

	struct Pattern
	{
		uint8_t version = 0;
		uint8_t revision = 0;
		uint8_t position = 0;
		std::array<uint64_t, g_tracks> trigs{};
		std::array<uint64_t, g_tracks> lockMasks{};
		uint64_t accent = 0, slide = 0, swing = 0;
		uint32_t swingAmount = 0;
		uint8_t accentAmount = 0, patternLength = 16, doubleTempo = 0;
		uint8_t scale = 0, kit = 0, numLockedRows = 0;
		// Ordered by (track, param) over the set bits of lockMasks.
		std::vector<Row> rows;
		std::array<uint32_t, 3> editAll{};
		std::array<uint64_t, g_tracks> accentPatterns{}, slidePatterns{}, swingPatterns{};
		bool extra = false;			// 64-step block present
		bool hasExtension = false;	// SPS-X block present
		std::vector<uint8_t> extension;	// decoded (unpacked + RLE expanded)
		size_t lockSlotCount = g_classicParams;

		size_t stepCount() const;
		size_t rowIndex(uint8_t _track, uint8_t _param) const;
		bool hasLock(uint8_t _track, uint8_t _param) const;
		// Allocates a lock row on demand. Fails when the pattern would exceed
		// the 64 rows this codec supports.
		bool setLock(uint8_t _track, uint8_t _param, uint8_t _step, uint8_t _value);
	};

	std::vector<uint8_t> request(uint8_t _pattern);
	bool isPatternDump(const std::vector<uint8_t>& _message);
	std::optional<Pattern> decode(const std::vector<uint8_t>& _message, std::string* _error = nullptr);
	std::vector<uint8_t> encode(const Pattern& _pattern);
}
