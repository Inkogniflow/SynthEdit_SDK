#include "./Disperser.h"

REGISTER_PLUGIN2(Disperser, L"FV Disperser");

Disperser::Disperser()
{
	// Register pins (order must match Disperser.xml).
	initializePin(pinSignal);
	initializePin(pinFrequency);
	initializePin(pinPinch);
	initializePin(pinAmount);
	initializePin(pinMix);
	initializePin(pinOutput);
}

int32_t Disperser::open()
{
	sampleRate = getSampleRate();

	k1Buf.assign(getBlockSize(), 0.f);
	k2Buf.assign(getBlockSize(), 0.f);

	return FilterBase::open();
}

// RBJ allpass, normalised so that b0 = a2 = k2, b1 = a1 = k1, b2 = a0 = 1.
void Disperser::calcCoefs(float pitch, float pinch, float& k1, float& k2) const
{
	// Frequency: SynthEdit volts (pin value * 10), 1 V/oct, 5 V = 440 Hz.
	float volts = pitch * 10.f;
	if (!(volts < 11.f)) volts = 11.f; // reversed test also catches NaN
	if (volts < -4.f)    volts = -4.f;

	float hz = 440.f * exp2f(volts - 5.f);
	hz = (std::min)(hz, sampleRate * 0.45f);
	hz = (std::max)(hz, 10.f);

	// Pinch: 0..10 V -> Q 0.5 .. 32 (exponential so the knob feels even).
	float pv = pinch * 10.f;
	if (!(pv < 10.f)) pv = 10.f;
	if (pv < 0.f)     pv = 0.f;
	const float q = 0.5f * exp2f(0.6f * pv);

	const float w0 = 2.f * (float)M_PI * hz / sampleRate;
	const float alpha = sinf(w0) / (2.f * q);
	const float a0inv = 1.f / (1.f + alpha);

	k1 = -2.f * cosf(w0) * a0inv;
	k2 = (1.f - alpha) * a0inv;
}

void Disperser::clearStages(int from, int to)
{
	for (int i = from; i < to; ++i)
	{
		s1[i] = s2[i] = 0.f;
	}
}

template<bool CoefsModulated, bool MixModulated>
void Disperser::subProcess(int sampleFrames)
{
	doStabilityCheck(); // must be first (FilterBase)

	const float* in    = getBuffer(pinSignal);
	const float* freq  = getBuffer(pinFrequency);
	const float* pinch = getBuffer(pinPinch);
	const float* mix   = getBuffer(pinMix);
	float*       out   = getBuffer(pinOutput);

	if (CoefsModulated)
	{
		for (int s = 0; s < sampleFrames; ++s)
		{
			calcCoefs(freq[s], pinch[s], k1Buf[s], k2Buf[s]);
		}
	}

	// Work in the output buffer, one stage at a time (keeps state in registers).
	for (int s = 0; s < sampleFrames; ++s)
	{
		out[s] = in[s];
	}

	for (int st = 0; st < stages; ++st)
	{
		float z1 = s1[st];
		float z2 = s2[st];

		if (CoefsModulated)
		{
			const float* k1 = k1Buf.data();
			const float* k2 = k2Buf.data();
			for (int s = 0; s < sampleFrames; ++s)
			{
				const float x = out[s];
				const float y = k2[s] * x + z1;
				z1 = k1[s] * (x - y) + z2;
				z2 = x - k2[s] * y;
				out[s] = y;
			}
		}
		else
		{
			const float k1 = fixedK1;
			const float k2 = fixedK2;
			for (int s = 0; s < sampleFrames; ++s)
			{
				const float x = out[s];
				const float y = k2 * x + z1;
				z1 = k1 * (x - y) + z2;
				z2 = x - k2 * y;
				out[s] = y;
			}
		}

		s1[st] = z1;
		s2[st] = z2;
	}

	if (MixModulated)
	{
		for (int s = 0; s < sampleFrames; ++s)
		{
			float m = mix[s];
			m = m < 0.f ? 0.f : (m > 1.f ? 1.f : m); // 0..10 V
			out[s] = in[s] + m * (out[s] - in[s]);
		}
	}
}

void Disperser::chooseProcessMethod()
{
	typedef void (Disperser::* MyProcess_ptr)(int sampleFrames);
	static const MyProcess_ptr table[2][2] =
	{
		{ &Disperser::subProcess<false, false>, &Disperser::subProcess<false, true> },
		{ &Disperser::subProcess<true,  false>, &Disperser::subProcess<true,  true> },
	};

	const bool coefsModulated = pinFrequency.isStreaming() || pinPinch.isStreaming();
	const bool mixModulated   = pinMix.isStreaming() || pinMix.getValue() < 1.f;

	setSubProcess(table[coefsModulated][mixModulated]);
}

void Disperser::onSetPins()
{
	// Stage count. Newly added stages start from silence.
	int newStages = pinAmount;
	newStages = (std::max)(0, (std::min)(newStages, maxStages));
	if (newStages > stages)
	{
		clearStages(stages, newStages);
	}
	stages = newStages;

	// Static coefficients.
	if (!pinFrequency.isStreaming() && !pinPinch.isStreaming())
	{
		calcCoefs(pinFrequency.getValue(), pinPinch.getValue(), fixedK1, fixedK2);
	}

	pinOutput.setStreaming(true);

	chooseProcessMethod();

	initSettling(); // must be last (FilterBase)
}

void Disperser::StabilityCheck()
{
	for (int i = 0; i < stages; ++i)
	{
		if (!std::isfinite(s1[i]) || !std::isfinite(s2[i]))
		{
			clearStages(0, maxStages);
			return;
		}

		// flush denormals
		if (fabsf(s1[i]) < 1.0e-20f) s1[i] = 0.f;
		if (fabsf(s2[i]) < 1.0e-20f) s2[i] = 0.f;
	}
}
