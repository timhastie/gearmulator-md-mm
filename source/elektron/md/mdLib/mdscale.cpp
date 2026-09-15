#include "mdscale.h"

#include <algorithm>
#include <vector>

namespace md::scale
{
	namespace
	{
		struct Scale { const char* name; uint16_t mask; };
		constexpr uint16_t m(std::initializer_list<int> _semitones)
		{
			uint16_t mask = 0;
			for(const auto s : _semitones) mask |= static_cast<uint16_t>(1u << s);
			return mask;
		}
		const Scale g_scales[g_scaleCount] =
		{
			{"OFF", 0},
			{"MAJOR", m({0,2,4,5,7,9,11})}, {"DORIAN", m({0,2,3,5,7,9,10})},
			{"PHRYGIAN", m({0,1,3,5,7,8,10})}, {"LYDIAN", m({0,2,4,6,7,9,11})},
			{"MIXOLYDIAN", m({0,2,4,5,7,9,10})}, {"MINOR", m({0,2,3,5,7,8,10})},
			{"LOCRIAN", m({0,1,3,5,6,8,10})}, {"PENTATONIC MINOR", m({0,3,5,7,10})},
			{"PENTATONIC MAJOR", m({0,2,4,7,9})}, {"MELODIC MINOR", m({0,2,3,5,7,9,11})},
			{"HARMONIC MINOR", m({0,2,3,5,7,8,11})}, {"WHOLE TONE", m({0,2,4,6,8,10})},
			{"BLUES", m({0,3,5,6,7,10})}, {"PHRYGIAN DOMINANT", m({0,1,4,5,7,8,10})},
			{"WHOLE-HALF DIM", m({0,2,3,5,6,8,9,11})}, {"HALF-WHOLE DIM", m({0,1,3,4,6,7,9,10})},
			{"HUNGARIAN MINOR", m({0,2,3,6,7,8,11})}, {"HIRAJOSHI", m({0,2,3,7,8})},
			{"IN-SEN", m({0,1,5,7,10})}, {"IWATO", m({0,1,5,6,10})},
			{"PELOG", m({0,1,3,7,8})}, {"DOUBLE HARMONIC", m({0,1,4,5,7,8,11})},
			{"SUPER LOCRIAN", m({0,1,3,4,6,8,10})}, {"LYDIAN DOMINANT", m({0,2,4,6,7,9,10})},
		};
		const char* const g_roots[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

		// MCL machine tuning tables (PTCH value per semitone from the base note).
		const uint8_t efm_rs[] = {1,3,6,9,11,14,17,19,22,25,27,30,33,35,38,41,43,46,49,51,54,57,59,62,65,67,70,73,75,78,81,83,86,89,91,94,97,99,102,105,107,110,113,115,118,121,123,126};
		const uint8_t efm_hh[] = {1,5,9,14,18,22,27,31,35,39,44,48,52,56,61,65,69,73,78,82,86,91,95,99,103,108,112,116,120,125};
		const uint8_t efm_cp[] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,95,97,99,101,103,105,107,109,111,113,115,117,119,121,123,125,127};
		const uint8_t efm_xt[] = {1,7,12,17,23,28,33,39,44,49,55,60,65,71,76,81,87,92,97,102,108,113,118,124};
		const uint8_t trx_cl[] = {5,11,17,23,29,36,42,48,54,60,66,72,78,84,91,97,103,109,115,121,127};
		const uint8_t trx_sd[] = {3,13,24,35,45,56,67,77,88,98,109,120};
		const uint8_t trx_xc[] = {1,6,11,17,22,27,33,38,43,49,54,60,65,70,76,81,86,92,97,102,108,113,118,124};
		const uint8_t trx_xt[] = {2,7,12,18,23,28,34,39,44,49,55,60,65,71,76,81,87,92,97,103,108,113,118,124};
		const uint8_t trx_bd[] = {1,7,12,17,23,28,33,39,44,49,55,60,66,71,76,82,87,92,98,103,108,114,119,124};
		const uint8_t trx_s2[] = {3,7,11,15,20,24,30,35,41,47,54,60,68,76,84,92,101,111,121};
		const uint8_t rom[] = {0,2,5,7,9,12,14,16,19,21,23,26,28,31,34,37,40,43,46,49,52,55,58,61,64,67,70,73,76,79,82,85,88,91,94,97,100,102,105,107,109,112,114,116,119,121,123,125};
		const uint8_t gnd_sn[] = {0,2,3,4,6,7,8,10,11,12,14,15,16,18,19,20,22,23,24,26,27,28,30,31,32,34,35,36,38,39,40,42,43,44,46,47,48,50,51,52,54,55,56,58,59,60,62,63,64,66,67,68,70,71,72,74,75,76,78,79,80,82,83,84,86,87,88,90,91,92,94,95,96,98,99,100,102,103,104,106,107,108,110,111,112,114,115,116,118,119,120,122,123,124,126,127};
		const uint8_t trx_b2[] = {31,33,36,39,43,45,48,51,56,60,62,66,70,74,79,83,89,94,97,104,108,113,120,125};
		const uint8_t trx_rs[] = {2,10,17,26,36,45,55,66,78,90,103,117};
		const uint8_t efm_cb[] = {2,6,10,14,19,23,27,32,36,40,44,48,53,57,62,66,70,74,78,83,87,91,96,100,104,108,113,117,121,126};
		const uint8_t efm_cy[] = {1,6,10,14,18,22,27,31,36,40,44,49,52,57,61,65,70,74,78,82,87,91,95,100,103,108,112,116,121,125};
		const uint8_t e12_bc[] = {2,4,7,9,12,15,17,20,22,25,28,31,34,36,39,42,44,47,49,52,55,57,60,62,65,68,71,74,76,79,82,84,87,89,92,95,97,100,103,106,108,111,114,116,119,122,124,127};
		const uint8_t e12_cb[] = {1,4,6,9,12,14,17,19,22,25,27,30,33,36,39,41,44,46,49,52,54,57,59,62,65,68,71,73,76,79,81,84,86,89,92,94,97,100,102,105,107,111,113,116,119,121,124,126};
		const uint8_t e12_lt[] = {2,5,8,11,15,18,21,25,28,31,34,37,41,44,47,50,53,57,60,63,66,70,73,76,79,82,85,89,92,95,98,101,105,108,111,114,118,121,124,127};
		const uint8_t tonal[] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,114,116,118,120,122,124,126};

		template<size_t N> constexpr Tuning t(const uint8_t _base, const uint8_t (&_v)[N]) { return {_base, _v, static_cast<uint8_t>(N)}; }
		struct ModelTuning { uint16_t model; Tuning tuning; };
		// MIDI note numbers: B1 = 23, AB1 = 20, A1 = 21, F2 = 29, F3 = 41, B3 = 47, F4 = 53, B4 = 59, B6 = 83, A2 = 45, CS0 = 1.
		const ModelTuning g_tunings[] =
		{
			{36, t(59, efm_rs)}, {38, t(59, efm_hh)}, {35, t(47, efm_cp)}, {33, t(47, efm_hh)},
			{34, t(29, efm_xt)}, {32, t(20, efm_rs)}, {26, t(83, trx_cl)}, {17, t(53, trx_sd)},
			{27, t(41, trx_xc)}, {18, t(47, trx_xt)}, {16, t(23, trx_bd)}, {1, t(29, gnd_sn)},
			{4, t(29, gnd_sn)}, {5, t(29, gnd_sn)}, {28, t(21, trx_b2)}, {20, t(53, trx_rs)},
			{29, t(29, trx_s2)},
			// F3 = 41, B2 = 35, D3 = 38, D#3 = 39, F#5 = 78
			{37, t(41, efm_cb)}, {39, t(35, efm_cy)}, {63, t(38, e12_bc)}, {54, t(39, e12_cb)}, {51, t(78, e12_lt)},
		};
	}

	const char* scaleName(const uint8_t _scale) { return g_scales[std::min<uint8_t>(_scale, g_scaleCount - 1)].name; }
	const char* rootName(const uint8_t _root) { return g_roots[_root % 12]; }
	uint16_t scaleMask(const uint8_t _scale) { return g_scales[std::min<uint8_t>(_scale, g_scaleCount - 1)].mask; }

	std::optional<Tuning> tuningForModel(const uint32_t _kitModel)
	{
		if(_kitModel == 0xffffffff)
			return std::nullopt;
		const auto model = static_cast<uint16_t>(_kitModel & 0xffff);
		if(_kitModel & g_tonalModelMask)
			return t(1, tonal);
		if(model >= 128 && model < 192)	// UW ROM / RAM machines
			return t(45, rom);
		for(const auto& entry : g_tunings)
			if(entry.model == model)
				return entry.tuning;
		return std::nullopt;
	}

	namespace
	{
		// First table index whose value reaches _value (MCL convention); count when above the table.
		size_t indexFor(const Tuning& _t, const uint8_t _value)
		{
			for(size_t i = 0; i < _t.count; ++i)
				if(_t.values[i] >= _value)
					return i;
			return _t.count;
		}
	}

	uint8_t step(const Tuning& _t, const uint16_t _mask, const uint8_t _root, const uint8_t _value, const int _direction)
	{
		if(!_mask || _t.count == 0)
			return _value;
		const auto i = indexFor(_t, _value);
		if(_direction > 0)
		{
			// Exactly on a degree: move past it. Between degrees: the degree at i already lies above.
			const size_t from = (i < _t.count && _t.values[i] == _value) ? i + 1 : i;
			for(size_t j = from; j < _t.count; ++j)
				if(_t.inScale(static_cast<uint8_t>(j), _mask, _root))
					return _t.values[j];
			return _value;
		}
		for(size_t j = i; j-- > 0;)
			if(_t.inScale(static_cast<uint8_t>(j), _mask, _root))
				return _t.values[j];
		return _value;
	}

	uint8_t snap(const Tuning& _t, const uint16_t _mask, const uint8_t _root, const uint8_t _value)
	{
		if(!_mask || _t.count == 0)
			return _value;
		int best = -1;
		for(size_t j = 0; j < _t.count; ++j)
		{
			if(!_t.inScale(static_cast<uint8_t>(j), _mask, _root))
				continue;
			const int distance = std::abs(static_cast<int>(_t.values[j]) - static_cast<int>(_value));
			if(best < 0 || distance < std::abs(static_cast<int>(_t.values[best]) - static_cast<int>(_value)))
				best = static_cast<int>(j);
		}
		return best < 0 ? _value : _t.values[best];
	}

	uint8_t random(const Tuning& _t, const uint16_t _mask, const uint8_t _root, std::mt19937& _rng)
	{
		std::vector<uint8_t> candidates;
		for(size_t j = 0; j < _t.count; ++j)
			if(_t.inScale(static_cast<uint8_t>(j), _mask, _root))
				candidates.push_back(_t.values[j]);
		if(candidates.empty())
			return static_cast<uint8_t>(std::uniform_int_distribution<int>(0, 127)(_rng));
		return candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(_rng)];
	}
}
