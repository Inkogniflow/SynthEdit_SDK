#pragma once

/*
	Wavetable Osc - SynthEdit SEM v3 module

	Pins:
		Pitch            audio in, 1 V/oct, 5 V = 440 Hz
		Position         audio in, 0..10 V = first..last frame (morphs smoothly between frames)
		Sync             audio in, rising edge through 0 V restarts the cycle (hard sync)
		Table            enum: 6 built-in tables or "WAV File"
		Wavetable File   .wav wavetable (Serum style, 2048 samples per frame), used when Table = WAV File
		Output           audio out, +/-5 V
		Display Table    blob out -> connect to Wavetable Display "Table"
		Display Wave     blob out -> connect to Wavetable Display "Wave"
*/

#include <memory>
#include <vector>
#include "../se_sdk3/mp_sdk_audio.h"
#include "WavetableEngine.h"

class WavetableOsc : public MpBase2
{
public:
	WavetableOsc();

	int32_t MP_STDCALL open() override;
	void onSetPins() override;
	void subProcess(int sampleFrames);

private:
	void loadTable();
	bool readFile(const std::wstring& shortName, std::vector<uint8_t>& bytes, std::string& fullName);
	void sendDisplayTable();
	void sendDisplayWave(float position);
	float pitchToIncrement(float pitch) const;

	AudioInPin   pinPitch;
	AudioInPin   pinPosition;
	AudioInPin   pinSync;
	EnumInPin    pinTable;
	StringInPin  pinFile;
	AudioOutPin  pinOutput;
	BlobOutPin   pinDisplayTable;
	BlobOutPin   pinDisplayWave;

	std::shared_ptr<const fvwt::Wavetable> table_;

	float sampleRate_ = 44100.f;
	float phase_ = 0.f;
	float increment_ = 0.f;
	int   level_ = 0;
	float prevSync_ = 0.f;

	// display throttling (~30 updates per second)
	int   displayCountdown_ = 0;
	int   displayInterval_ = 1470;
	float lastPosition_ = 0.f;
	float lastSentPosition_ = -1.f;

	std::vector<uint8_t> blobBuffer_;
};
