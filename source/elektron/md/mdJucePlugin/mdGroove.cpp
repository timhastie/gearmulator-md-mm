#include "mdGroove.h"

#include <algorithm>
#include <cmath>

namespace mdJucePlugin::groove
{
	namespace
	{
		double p(const double _base, const double _density) { return std::clamp(_base * _density, 0.0, 1.0); }
		bool roll(std::mt19937& _rng, const double _probability)
		{
			return std::bernoulli_distribution(std::clamp(_probability, 0.0, 1.0))(_rng);
		}
		int pick(std::mt19937& _rng, const int _n) { return std::uniform_int_distribution<int>(0, _n - 1)(_rng); }

		// One bar (16 steps) for a role.
		uint16_t bar(const Role _role, const double _d, std::mt19937& _rng, const bool _lastBar)
		{
			uint16_t m = 0;
			auto set = [&](const int _s) { m |= static_cast<uint16_t>(1u << (_s & 15)); };
			auto clear = [&](const int _s) { m &= static_cast<uint16_t>(~(1u << (_s & 15))); };
			auto has = [&](const int _s) { return (m >> (_s & 15)) & 1u; };
			switch(_role)
			{
			case Role::Kick:
			{
				static const int seeds[4][4] = { {0,4,8,12}, {0,10,-1,-1}, {0,7,10,-1}, {0,4,8,14} };
				const auto& seed = seeds[pick(_rng, 4)];
				for(const auto s : seed) if(s >= 0 && roll(_rng, p(0.88, _d))) set(s);
				static const int extras[] = {3, 6, 7, 10, 11, 13, 14, 15};
				for(const auto s : extras) if(!has(s) && roll(_rng, p(0.12, _d))) set(s);
				if(roll(_rng, 0.92)) set(0);
				if(_lastBar && roll(_rng, 0.3)) set(15);
				break;
			}
			case Role::Snare:
			case Role::Clap:
			{
				const bool displaced = roll(_rng, 0.1);
				const int a = displaced ? (roll(_rng, 0.5) ? 3 : 5) : 4;
				const int b = displaced ? (roll(_rng, 0.5) ? 11 : 13) : 12;
				if(roll(_rng, p(0.92, _d))) set(a);
				if(roll(_rng, p(0.92, _d))) set(b);
				static const int ghosts[] = {7, 9, 13, 15, 2};
				for(const auto s : ghosts) if(!has(s) && roll(_rng, p(_role == Role::Clap ? 0.08 : 0.14, _d))) set(s);
				if(_lastBar) for(const int s : {13, 14, 15}) if(roll(_rng, p(0.35, _d))) set(s);
				break;
			}
			case Role::ClosedHat:
			{
				const int style = pick(_rng, 3);
				for(int s = 0; s < 16; ++s)
				{
					const bool candidate = style == 0 ? (s % 4 == 2) : style == 1 ? (s % 2 == 0) : true;
					if(candidate && roll(_rng, p(style == 2 ? 0.8 : 0.92, _d))) set(s);
				}
				if(roll(_rng, p(0.25, _d))) set(pick(_rng, 16));
				break;
			}
			case Role::OpenHat:
				for(const int s : {2, 6, 10, 14}) if(roll(_rng, p(0.32, _d))) set(s);
				if(m == 0 && roll(_rng, p(0.6, _d))) set(14);
				break;
			case Role::Tom:
			{
				static const int spots[] = {3, 6, 9, 11, 13, 14, 15, 7};
				const int hits = std::min(3, static_cast<int>(std::lround(pick(_rng, 3) * _d)));
				for(int i = 0; i < hits; ++i) set(spots[pick(_rng, 8)]);
				if(_lastBar) for(const int s : {12, 13, 14, 15}) if(roll(_rng, p(0.4, _d))) set(s);
				break;
			}
			case Role::Perc:
				for(const int s : {2, 5, 9, 13, 15, 7, 11}) if(roll(_rng, p(0.22, _d))) set(s);
				break;
			case Role::Cymbal:
				if(roll(_rng, p(0.3, _d))) set(0);
				if(roll(_rng, p(0.15, _d))) for(int s = 0; s < 16; s += 2) set(s);
				break;
			case Role::Bass:
			{
				static const double profile[16] = {0.95,0.2,0.5,0.6, 0.3,0.2,0.7,0.55, 0.65,0.2,0.6,0.7, 0.3,0.25,0.7,0.4};
				int run = 0;
				for(int s = 0; s < 16; ++s)
				{
					if(roll(_rng, p(profile[s], _d)) && run < 3) { set(s); ++run; }
					else run = 0;
				}
				break;
			}
			case Role::Lead:
			{
				static const double profile[16] = {0.8,0.1,0.4,0.3, 0.6,0.1,0.5,0.4, 0.7,0.1,0.4,0.5, 0.6,0.1,0.5,0.25};
				int run = 0;
				for(int s = 0; s < 16; ++s)
				{
					if(roll(_rng, p(profile[s], _d)) && run < 4) { set(s); ++run; }
					else run = 0;
				}
				break;
			}
			case Role::Pad:
				if(roll(_rng, p(0.9, _d))) set(0);
				if(roll(_rng, p(0.5, _d))) set(8);
				for(const int s : {6, 10, 14}) if(roll(_rng, p(0.12, _d))) set(s);
				break;
			case Role::Arp:
				for(int s = 0; s < 16; s += 2) if(roll(_rng, p(s % 4 == 2 ? 0.75 : 0.45, _d))) set(s);
				for(const int s : {3, 7, 11, 15}) if(roll(_rng, p(0.15, _d))) set(s);
				break;
			}
			return m;
		}
	}

	Role roleForMachinedrum(const uint8_t _track, const uint32_t _kitModel)
	{
		const auto model = _kitModel == 0xffffffffu ? 0xffffu : (_kitModel & 0xffff);
		switch(model)
		{
		case 16: case 32: case 48: case 64: return Role::Kick;
		case 17: case 33: case 49: case 65: return Role::Snare;
		case 19: case 35: case 52: return Role::Clap;
		case 20: case 36: case 53: case 69: case 21: case 37: case 54: case 68: case 25: case 59: case 60: case 61: case 62: case 63: return Role::Perc;
		case 22: case 38: case 55: case 72: return Role::ClosedHat;
		case 23: case 56: return Role::OpenHat;
		case 24: case 39: case 57: case 58: case 70: case 71: return Role::Cymbal;
		case 18: case 34: case 50: case 51: case 66: case 67: return Role::Tom;
		case 1: case 4: case 5: return Role::Bass;
		default: break;
		}
		static const Role byLabel[16] = { Role::Kick, Role::Snare, Role::Tom, Role::Tom, Role::Tom, Role::Clap, Role::Perc, Role::Perc,
			Role::ClosedHat, Role::OpenHat, Role::Cymbal, Role::Cymbal, Role::Bass, Role::Lead, Role::Arp, Role::Pad };
		return byLabel[_track & 15];
	}

	Role roleForMonomachine(const uint8_t _track)
	{
		static const Role byTrack[6] = { Role::Bass, Role::Lead, Role::Pad, Role::Arp, Role::Lead, Role::Pad };
		return byTrack[_track % 6];
	}

	const char* roleName(const Role _role)
	{
		static const char* names[] = {"kick","snare","clap","closed hat","open hat","tom","perc","cymbal","bass","lead","pad","arp"};
		return names[static_cast<size_t>(_role)];
	}

	uint64_t trigs(const Role _role, const size_t _steps, const double _density, std::mt19937& _rng)
	{
		const size_t bars = std::max<size_t>(1, (_steps + 15) / 16);
		const uint16_t first = bar(_role, _density, _rng, bars == 1);
		uint64_t out = 0;
		for(size_t b = 0; b < bars; ++b)
		{
			uint16_t m = first;
			const bool last = b + 1 == bars;
			if(b > 0)
			{
				// Mutate: flip a few hits, keep the grammar's strong beats mostly intact.
				const double flip = _role == Role::Kick || _role == Role::Snare || _role == Role::Clap ? 0.08 : 0.16;
				for(int s = 0; s < 16; ++s)
					if(roll(_rng, flip) && !(s == 0 && _role == Role::Kick))
						m ^= static_cast<uint16_t>(1u << s);
				if(last && bars > 1)
					m |= bar(_role, _density, _rng, true) & 0xf000;	// fill from a fresh last bar
			}
			out |= static_cast<uint64_t>(m) << (b * 16);
		}
		const uint64_t stepMask = _steps >= 64 ? ~0ull : (1ull << _steps) - 1;
		return out & stepMask;
	}

	NoteWalker::NoteWalker(const Role _role, const uint16_t _mask, const uint8_t _root, std::mt19937& _rng)
		: m_role(_role), m_root(_root % 12), m_mask(_mask ? _mask : 0x04a9 /* minor pentatonic 0 3 5 7 10 */), m_rng(_rng)
	{
		m_scaleSize = 0;
		for(uint8_t k = 0; k < 12; ++k)
			if(m_mask >> k & 1u) m_scaleNotes[m_scaleSize++] = k;
		if(m_scaleSize == 0) { m_scaleNotes[0] = 0; m_scaleSize = 1; }
		m_base = _role == Role::Bass ? 36 : _role == Role::Pad ? 48 : 60;
		m_degree = 0;
	}

	uint8_t NoteWalker::next(const size_t _step, const size_t _steps)
	{
		(void)_steps;
		auto note = [&](const int _degree)
		{
			const int octave = _degree >= 0 ? _degree / m_scaleSize : -((-_degree + m_scaleSize - 1) / m_scaleSize);
			const int index = _degree - octave * m_scaleSize;
			return static_cast<uint8_t>(std::clamp(m_base + m_root + octave * 12 + m_scaleNotes[index], 0, 127));
		};
		switch(m_role)
		{
		case Role::Bass:
		{
			// 303: root most of the time, fifth/octave, occasional other tone; rare octave jump.
			const int r = pick(m_rng, 100);
			int degree = 0;
			if(r < 58) degree = 0;
			else if(r < 74) degree = std::min(m_scaleSize - 1, m_scaleSize > 4 ? 4 : m_scaleSize - 1);	// fifth-ish
			else if(r < 88) degree = m_scaleSize;	// octave
			else degree = pick(m_rng, m_scaleSize);
			if(_step % 16 == 0) degree = 0;
			return note(degree);
		}
		case Role::Lead:
		case Role::Arp:
		{
			// Stepwise walk with occasional leaps, held within two octaves of the base.
			const int r = pick(m_rng, 100);
			int delta = r < 35 ? 1 : r < 70 ? -1 : r < 80 ? 2 : r < 90 ? -2 : r < 95 ? 4 : 0;
			if(_step % 16 == 0 && roll(m_rng, 0.5)) m_degree = 0;
			m_degree = std::clamp(m_degree + delta, -m_scaleSize, 2 * m_scaleSize);
			return note(m_degree);
		}
		case Role::Pad:
		default:
		{
			static const int chordTones[] = {0, 2, 4};	// degrees 1, 3, 5 of the scale
			return note(chordTones[pick(m_rng, 3)] % m_scaleSize);
		}
		}
	}
}
