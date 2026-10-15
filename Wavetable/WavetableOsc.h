#pragma once

/*
	Wavetable Osc - SynthEdit SEM v3 module

	Pins:
		Pitch            audio in, 1 V/oct, 5 V = 440 Hz
		Position         audio in, 0..10 V = first..last frame (morphs smoothly between frames)
		Phase            audio in, 0..10 V = 0..360 degree phase offset (also usable for phase modulation)
		Sync             audio in, rising edge through 0 V restarts the cycle (hard sync)
		Table            enum: 6 built-in tables, "WAV File" or "File List"
		Wavetable File   .wav wavetable (Serum style, 2048 samples per frame), used when Table = WAV File
		File List        folder, or files separated by ';' - joined into one table, used when Table = File List
		Voices           unison voices, 1..16
		Detune           0..10 V, unison detune (outer voices up to +/-100 cents, curved for fine control)
		Width            0..10 V, stereo spread of the unison voices
		Pan              -5..+5 V, left..right
		On               off = silence and no CPU
		Left, Right      audio out, +/-5 V per channel at centre
		Display Table    blob out -> connect to Wavetable Display "Table"
		Display Wave     blob out -> connect to Wavetable Display "Wave"
*/

#include <memory>
#include <vector>
#include <string>
#include "../se_sdk3/mp_sdk_audio.h"
#include "WavetableEngine.h"

class WavetableOsc : public MpBase2
{
public:
	static constexpr int kMaxVoices = 16;

	WavetableOsc();

	int32_t MP_STDCALL open() override;
	void onSetPins() override;
	void subProcess(int sampleFrames);
	void subProcessOff(int sampleFrames);

private:
	struct Voice
	{
		float phase = 0.f;
		float startPhase = 0.f;
		float ratio = 1.f;        // detune
		float levelOffset = 0.f;  // 2 * log2(ratio), shifts the mip-level choice
		float gainL = 1.f;
		float gainR = 1.f;
	};

	void loadTable();
	std::shared_ptr<const fvwt::Wavetable> loadFileList(const std::wstring& list);
	bool readFile(const std::wstring& shortName, std::vector<uint8_t>& bytes, std::string& fullName);
	void sendDisplayTable();
	void sendDisplayWave(float position);
	float pitchToIncrement(float pitch) const;
	void updateUnison(float detune, float width, float pan);
	void resetPhases();

	AudioInPin   pinPitch;
	AudioInPin   pinPosition;
	AudioInPin   pinPhase;
	AudioInPin   pinSync;
	EnumInPin    pinTable;
	StringInPin  pinFile;
	StringInPin  pinFileList;
	IntInPin     pinVoices;
	AudioInPin   pinDetune;
	AudioInPin   pinWidth;
	AudioInPin   pinPan;
	BoolInPin    pinOn;
	AudioOutPin  pinLeft;
	AudioOutPin  pinRight;
	BlobOutPin   pinDisplayTable;
	BlobOutPin   pinDisplayWave;

	std::shared_ptr<const fvwt::Wavetable> table_;

	float sampleRate_ = 44100.f;
	float increment_ = 0.f;
	float prevSync_ = 0.f;
	bool  wasOn_ = true;

	int   numVoices_ = 1;
	Voice voices_[kMaxVoices];
	float unisonGain_ = 1.f;

	// last values used for the unison set-up (recalculated when they change)
	float lastDetune_ = -1.f;
	float lastWidth_ = -1.f;
	float lastPan_ = -100.f;

	// display throttling (~30 updates per second)
	int   displayCountdown_ = 0;
	int   displayInterval_ = 1470;
	float lastPosition_ = 0.f;
	float lastSentPosition_ = -1.f;

	std::vector<uint8_t> blobBuffer_;
};
