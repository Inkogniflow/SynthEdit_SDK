#pragma once

/*
	Disperser - SynthEdit SEM v3 module

	A chain of identical 2nd-order allpass filters. Each stage delays the
	frequencies around the centre frequency more than the rest of the
	spectrum, so transients get "smeared" into a chirp / laser-zap.
	Magnitude response stays flat (it's an allpass), only phase changes.

	Pins:
		Signal     audio in
		Frequency  audio in, 1 V/oct, 5 V = 440 Hz          (modulatable)
		Pinch      audio in, 0..10 V -> Q 0.5 .. 32           (modulatable)
		Amount     int, number of allpass stages 0..128
		Mix        audio in, 0 V = dry, 10 V = fully dispersed (modulatable)
		Output     audio out
*/

#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <vector>
#include "../shared/FilterBase.h"

class Disperser : public FilterBase
{
public:
	static constexpr int maxStages = 128;

	Disperser();

	int32_t MP_STDCALL open() override;
	void onSetPins() override;

	// CoefsModulated: Frequency or Pinch is streaming, recalc coefficients per sample.
	// MixModulated: Mix is streaming (or not fully wet), blend dry/wet per sample.
	template<bool CoefsModulated, bool MixModulated>
	void subProcess(int sampleFrames);

	// FilterBase support (power saving / sleep once tail has decayed).
	bool isFilterSettling() override
	{
		return !pinSignal.isStreaming();
	}
	AudioOutPin& getOutputPin() override
	{
		return pinOutput;
	}
	void StabilityCheck() override;

private:
	void calcCoefs(float pitch, float pinch, float& k1, float& k2) const;
	void clearStages(int from, int to);
	void chooseProcessMethod();

	AudioInPin  pinSignal;
	AudioInPin  pinFrequency;
	AudioInPin  pinPinch;
	IntInPin    pinAmount;
	AudioInPin  pinMix;
	AudioOutPin pinOutput;

	int   stages = 0;
	float sampleRate = 44100.f;

	// fixed coefficients (when Frequency and Pinch are not modulated)
	float fixedK1 = 0.f;
	float fixedK2 = 0.f;

	// per-sample coefficients (when modulated)
	std::vector<float> k1Buf;
	std::vector<float> k2Buf;

	// transposed direct form II state, one pair per stage
	float s1[maxStages] = {};
	float s2[maxStages] = {};
};
