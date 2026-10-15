#pragma once

/*
	SXE Waveshaper - SynthEdit SEM v3 module (stereo)

	Pins:
		Left In, Right In   audio in, +/-5 V = x -1..+1
		Position            audio in, 0..10 V = first..last function of the table (morphs between neighbours)
		A, B, C, D          audio in, -5..+5 V, scaled to the range written in the formula, e.g. A(-1.5, 3.3)
		Mix                 audio in, 0..10 V = dry..wet (dry is latency-compensated)
		Table               enum: 5 built-in tables, or Custom
		Functions           function list used when Table = Custom (';' or new line separated, "name: formula")
		Oversampling        Off, 2x, 4x, 8x (adds 32 samples latency when on)
		DC Block            removes DC from the shaped signal (asymmetric curves)
		Left Out, Right Out audio out
		Display Table       blob out -> SXE Waveshaper Display "Table"
		Display Curve       blob out -> SXE Waveshaper Display "Curve"
*/

#include <memory>
#include <vector>
#include "../se_sdk3/mp_sdk_audio.h"
#include "FormulaEngine.h"
#include "Oversampler.h"

class Waveshaper : public MpBase2
{
public:
	static constexpr int kChunk = sxws::Oversampler::kMaxChunk; // base samples per inner loop
	static constexpr int kMaxFactor = 8;
	static constexpr int kMaxOs = kChunk * kMaxFactor;            // = sxws::kMaxBlock

	Waveshaper();

	int32_t MP_STDCALL open() override;
	void onSetPins() override;
	void subProcess(int sampleFrames);

private:
	struct Delay
	{
		float buf[64] = {};
		int w = 0;
		float process(float in, int d)
		{
			buf[w] = in;
			const float out = buf[(w - d) & 63];
			w = (w + 1) & 63;
			return out;
		}
		void reset() { for (auto& b : buf) b = 0.f; w = 0; }
	};

	void loadFunctions();
	void setOversampling(int factor);
	void processChunk(int offset, int n);
	void shapeChannel(const float* x, float* y, int m, const float* const* vars, const float* fpos);
	void computeDisplayCurve(float position, float* y, int points);
	void sendDisplayTable();
	void sendDisplayCurve();

	AudioInPin  pinInL;
	AudioInPin  pinInR;
	AudioInPin  pinPosition;
	AudioInPin  pinVar[sxws::kNumVars];
	AudioInPin  pinMix;
	EnumInPin   pinTable;
	StringInPin pinFunctions;
	EnumInPin   pinOversampling;
	BoolInPin   pinDcBlock;
	AudioOutPin pinOutL;
	AudioOutPin pinOutR;
	BlobOutPin  pinDisplayTable;
	BlobOutPin  pinDisplayCurve;

	std::shared_ptr<const sxws::FunctionSet> set_;

	float sampleRate_ = 44100.f;
	sxws::Oversampler os_[2];
	int factor_ = 1;
	int latency_ = 0;

	Delay dryDelay_[2];
	Delay varDelay_[sxws::kNumVars];
	Delay posDelay_;
	float prevVar_[sxws::kNumVars] = {};

	float dcCoeff_ = 0.999f;
	float dcX1_[2] = {};
	float dcY1_[2] = {};

	// work buffers (oversampled)
	alignas(16) float xs_[2][kMaxOs];
	alignas(16) float ys_[2][kMaxOs];
	alignas(16) float varOs_[sxws::kNumVars][kMaxOs];
	alignas(16) float fposOs_[kMaxOs];
	alignas(16) float tmp_[kMaxOs];
	alignas(16) float tmp2_[kMaxOs];
	float wet_[2][kChunk];

	// display
	float lastVars_[sxws::kNumVars] = {};
	float lastPosition_ = 0.f;
	float peak_[2] = {};
	float sentVars_[sxws::kNumVars] = { 9.f, 9.f, 9.f, 9.f };
	float tableVars_[sxws::kNumVars] = { 9.f, 9.f, 9.f, 9.f };
	float sentPosition_ = -1.f;
	float sentPeak_[2] = { -1.f, -1.f };
	bool  tablePending_ = true;
	int   curveCountdown_ = 0;
	int   curveInterval_ = 1470;
	int   tableCountdown_ = 0;
	int   tableInterval_ = 4410;
	std::vector<uint8_t> blob_;
};
