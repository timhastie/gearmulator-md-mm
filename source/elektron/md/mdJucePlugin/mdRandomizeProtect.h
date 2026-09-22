#pragma once

// Parameters that can make a track suddenly loud or wash it in effects. Each
// has two independent protections: from value randomization (random machine
// assignments, YES + UP) and from parameter-lock randomization. Both default
// to protected. The FX-machine entry covers the whole SYNTH page of a track
// whose machine is an effect (MM THRU/REVERB/CHORUS/DYNAMIX/RINGMOD), which is
// where make-up gain, feedback and reverb amounts live.

#include "mdLib/mdtypes.h"

#include <cstdint>
#include <vector>

namespace mdJucePlugin::randomizeProtect
{
	struct Entry
	{
		const char* label;
		uint8_t page;		// automation page (255 = FX-machine SYNTH page rule)
		uint8_t index;		// parameter index on that page
	};
	constexpr uint8_t g_fxMachineRule = 255;
	// Constraint rules: the parameter is still randomized, but inside a band that
	// keeps the track audible. Applied through constrainedValue() below.
	constexpr uint8_t g_filterWindowRule = 254;	// MM FILTER page 2: BASE 0, WIDTH 1, BOFS 6, WOFS 7
	constexpr uint8_t g_ampEnvelopeRule = 253;		// MM AMP page 1: ATK 0, DEC 2, REL 3
	constexpr uint8_t g_lfoPagesRule = 252;			// MM LFO pages 4..6 (the MD LFO is not reachable by the randomizer)

	inline const std::vector<Entry>& entries(const md::MachineModel _model)
	{
		static const std::vector<Entry> machinedrum =
		{
			{"Distortion (DIST)", 2, 0},
			{"Delay Send (DEL)", 2, 3},
			{"Reverb Send (REV)", 2, 4},
			{"EQ Gain (EQG)", 1, 3},
			{"Filter Q (FLTQ)", 1, 6},
			{"Sample Rate Reduction (SRR)", 1, 7},
			{"Amplitude Mod Depth (AMD)", 1, 0},
			{"Pan (PAN)", 2, 2},
			{"Filter Frequency (FLTF)", 1, 4},
			{"Filter Width (FLTW)", 1, 5},
			{"Volume (VOL)", 2, 1},
		};
		static const std::vector<Entry> monomachine =
		{
			{"Distortion (DIST)", 1, 4},
			{"Delay Send (DSND)", 3, 4},
			{"Delay Feedback (DFB)", 3, 5},
			{"EQ Gain (EQG)", 3, 1},
			{"Sample Rate Reduction (SRR)", 3, 2},
			{"HP Filter Resonance (HPQ)", 2, 2},
			{"LP Filter Resonance (LPQ)", 2, 3},
			{"FX machine settings (SYNTH page of THRU, REVERB, CHORUS, DYNAMIX, RINGMOD)", g_fxMachineRule, 0},
			{"Pan (PAN)", 1, 6},
			{"Filter window kept open (BASE, WIDTH, BOFS, WOFS constrained)", g_filterWindowRule, 0},
			{"Amp envelope kept audible (ATK short, DEC and REL not tiny)", g_ampEnvelopeRule, 0},
			{"LFO settings (LFO 1, 2, 3 pages)", g_lfoPagesRule, 0},
			{"Filter Base (BASE)", 2, 0},
			{"Filter Width (WIDTH)", 2, 1},
			{"Volume (VOL)", 1, 5},
		};
		return _model == md::MachineModel::Monomachine ? monomachine : machinedrum;
	}

	inline bool isMonomachineFxMachine(const uint32_t _kitModel)
	{
		const auto model = _kitModel & 0xff;
		return _kitModel != 0xffffffffu && (model == 12 || model == 13 || model == 15 || model == 16 || model == 17);
	}

	// Constraint rules for MM randomization. Returns the value to use for a
	// parameter given the freely rolled _random value (0..127), or _random when no
	// rule applies. _mask is the values-protection mask.
	inline uint8_t constrainedValue(const md::MachineModel _model, const uint32_t _mask,
		const uint8_t _page, const uint8_t _index, const uint8_t _random)
	{
		if(_model != md::MachineModel::Monomachine)
			return _random;
		const auto& list = entries(_model);
		auto ruleOn = [&](const uint8_t _rule)
		{
			for(size_t i = 0; i < list.size(); ++i)
				if(list[i].page == _rule && (_mask >> i & 1u))
					return true;
			return false;
		};
		const auto band = [&](const int _lo, const int _hi) { return static_cast<uint8_t>(_lo + (_random * (_hi - _lo + 1)) / 128); };
		if(_page == 2 && ruleOn(g_filterWindowRule))
		{
			switch(_index)
			{
			case 0: return band(0, 36);		// BASE low: the passband starts in the bass/low-mid region
			case 1: return band(80, 127);	// WIDTH wide open
			case 6: return band(48, 80);	// BOFS mild around centre (64)
			case 7: return band(48, 80);	// WOFS mild around centre
			default: break;
			}
		}
		if(_page == 1 && ruleOn(g_ampEnvelopeRule))
		{
			switch(_index)
			{
			case 0: return band(0, 24);		// ATK short
			case 2: return band(24, 127);	// DEC not tiny
			case 3: return band(16, 127);	// REL not tiny
			default: break;
			}
		}
		return _random;
	}

	// Monomachine LFO routing. PAGE (LFO page index 0) selects the target page in
	// value bands (PTCH, SYNTH, AMP, FILT, EFFX, LFO1-3, MIDI); DEST (index 1)
	// selects the parameter within it in bands of 16.
	inline int mmLfoPageFromValue(const uint8_t _value)
	{
		// -1 = PTCH, 0..6 = SYNTH..LFO3, 7 = MIDI
		if(_value < 14) return -1;
		if(_value < 28) return 0;
		if(_value < 42) return 1;
		if(_value < 56) return 2;
		if(_value < 70) return 3;
		if(_value < 85) return 4;
		if(_value < 99) return 5;
		if(_value < 113) return 6;
		return 7;
	}
	inline uint8_t mmLfoPageValue(const int _page)
	{
		static const uint8_t centres[] = {7, 21, 35, 49, 63, 78, 92, 106, 120};
		return centres[_page + 1];
	}
	inline uint8_t mmLfoDestValue(const uint8_t _index) { return static_cast<uint8_t>(_index * 16 + 8); }

	// _mask bit n set = entry n is protected.
	inline bool isProtected(const md::MachineModel _model, const uint32_t _mask,
		const uint8_t _page, const uint8_t _index, const uint32_t _trackKitModel)
	{
		const auto& list = entries(_model);
		for(size_t i = 0; i < list.size(); ++i)
		{
			if(!(_mask >> i & 1u))
				continue;
			const auto& e = list[i];
			if(e.page == g_fxMachineRule)
			{
				if(_page == 0 && _model == md::MachineModel::Monomachine && isMonomachineFxMachine(_trackKitModel))
					return true;
			}
			else if(e.page == g_lfoPagesRule)
			{
				if(_model == md::MachineModel::Monomachine && _page >= 4 && _page <= 6)
					return true;
			}
			else if(e.page == g_filterWindowRule || e.page == g_ampEnvelopeRule)
				continue;	// constraint rules never exclude
			else if(e.page == _page && e.index == _index)
				return true;
		}
		return false;
	}
}
