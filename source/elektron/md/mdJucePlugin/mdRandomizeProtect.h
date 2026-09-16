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
		};
		return _model == md::MachineModel::Monomachine ? monomachine : machinedrum;
	}

	inline bool isMonomachineFxMachine(const uint32_t _kitModel)
	{
		const auto model = _kitModel & 0xff;
		return _kitModel != 0xffffffffu && (model == 12 || model == 13 || model == 15 || model == 16 || model == 17);
	}

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
			else if(e.page == _page && e.index == _index)
				return true;
		}
		return false;
	}
}
