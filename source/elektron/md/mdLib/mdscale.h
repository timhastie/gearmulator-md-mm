#pragma once

// Scale quantizer for the Machinedrum PTCH parameter. Machine tuning tables
// (PTCH value per semitone) are ported from the GPL MCL project; TONAL
// machines of the unofficial X firmware use quarter-tone spacing.

#include <array>
#include <cstdint>
#include <optional>
#include <random>

namespace md::scale
{
	constexpr uint8_t g_scaleCount = 25;	// 0 = OFF
	constexpr uint32_t g_tonalModelMask = 0x30000;	// kit model bit 16/17: TONAL tuning

	const char* scaleName(uint8_t _scale);
	const char* rootName(uint8_t _root);
	uint16_t scaleMask(uint8_t _scale);	// bit k = semitone k above the root

	struct Tuning
	{
		uint8_t baseNote = 0;		// MIDI note of values[0]
		const uint8_t* values = nullptr;	// PTCH value per semitone
		uint8_t count = 0;

		bool inScale(uint8_t _index, uint16_t _mask, uint8_t _root) const
		{
			return _mask >> ((baseNote + _index + 12 - _root) % 12) & 1u;
		}
	};

	// Tuning for a kit-dump machine model word, or nullopt for machines
	// without a pitched PTCH parameter.
	std::optional<Tuning> tuningForModel(uint32_t _kitModel);

	// Next in-scale PTCH value in the turn direction (+1/-1). An out-of-scale
	// value snaps to the nearest degree in that direction. Returns _value when
	// the scale has no further degree in range.
	uint8_t step(const Tuning& _tuning, uint16_t _mask, uint8_t _root, uint8_t _value, int _direction);
	// Nearest in-scale value; ties resolve downwards.
	uint8_t snap(const Tuning& _tuning, uint16_t _mask, uint8_t _root, uint8_t _value);
	uint8_t random(const Tuning& _tuning, uint16_t _mask, uint8_t _root, std::mt19937& _rng);
}
