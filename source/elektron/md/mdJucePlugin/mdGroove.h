#pragma once

// AFX / Autechre style groove generator for the randomize gestures. Each track
// gets a musical role; each role has its own grammar for where hits land on a
// 16-step bar. Bars are repeated across the pattern with small mutations and
// a fill in the last bar. Density (0..2, from the trig-chance slider) scales
// every probability. Monomachine notes come from a per-role melodic walker.

#include <cstdint>
#include <random>

namespace mdJucePlugin::groove
{
	enum class Role : uint8_t { Kick, Snare, Clap, ClosedHat, OpenHat, Tom, Perc, Cymbal, Bass, Lead, Pad, Arp };

	Role roleForMachinedrum(uint8_t _track, uint32_t _kitModel);
	Role roleForMonomachine(uint8_t _track);
	const char* roleName(Role _role);

	// Trig mask for a track over _steps steps (1..64).
	uint64_t trigs(Role _role, size_t _steps, double _density, std::mt19937& _rng);

	// Melodic walker for Monomachine notes: _mask is a 12-bit scale (bit k = semitone
	// above root), _root 0..11. When _mask is 0, minor pentatonic is used.
	class NoteWalker
	{
	public:
		NoteWalker(Role _role, uint16_t _mask, uint8_t _root, std::mt19937& _rng);
		uint8_t next(size_t _step, size_t _steps);
	private:
		Role m_role;
		uint8_t m_root;
		uint16_t m_mask;
		std::mt19937& m_rng;
		int m_degree = 0;	// scale-degree index relative to the role's base octave
		int m_scaleSize = 5;
		uint8_t m_scaleNotes[12]{};	// semitone offsets of the scale, ascending
		uint8_t m_base = 48;
	};
}
