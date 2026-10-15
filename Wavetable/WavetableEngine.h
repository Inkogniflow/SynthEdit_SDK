#pragma once

/*
	Wavetable engine: builds band-limited wavetables and shares them between all voices/instances.

	Each frame is stored as 21 "mip levels", one every half octave. Level j holds at most
	floor(1024 / 2^(j/2)) harmonics, so the oscillator can always pick a level with no
	harmonics above Nyquist (no aliasing) and loses at most the top half-octave of the spectrum.
	Level tables are 8x oversampled (power-of-two size, 64..4096) and read with 4-point Hermite
	interpolation, which keeps interpolation images below about -80 dB.
*/

#include <stdint.h>
#include <vector>
#include <memory>
#include <string>
#include <math.h>

namespace fvwt
{
	constexpr int kNumLevels       = 21;
	constexpr int kMaxFrames       = 256;
	constexpr int kMaxHarmonics    = 1023;

	enum BuiltIn
	{
		Classic,        // sine -> triangle -> saw -> square
		Pwm,            // pulse width 50% -> 3%
		HarmonicSweep,  // 1 harmonic -> 256 harmonics (opening filter)
		Formant,        // vowels A -> E -> I -> O -> U
		SyncSweep,      // hard-synced saw, ratio 1 -> 8
		Wavefold,       // sine through a wavefolder, gain 1 -> 10
		NumBuiltIns
	};

	struct Wavetable
	{
		int frames = 0;
		int frameStride = 0;                 // floats per frame (all levels)
		int levelSize[kNumLevels] = {};      // samples per cycle (power of two)
		int levelOffset[kNumLevels] = {};    // each level stores 1 + levelSize + 2 samples (wrap-around guards)
		std::vector<float> data;

		// level-0 copy at display resolution, used to build display blobs
		std::vector<float> displayFrames;    // frames * kDisplayWavePoints

		uint32_t serial = 0;

		// Pointer to sample 0 of a level; p[-1], p[size] and p[size + 1] are valid (wrapped).
		inline const float* level(int frame, int lvl) const
		{
			return data.data() + (size_t)frame * frameStride + levelOffset[lvl] + 1;
		}
	};

	// Mip-level choice. levelX() is the continuous level for a phase increment (cycles/sample);
	// detuned voices can add 2 * log2(ratio) to it instead of calling log2 again.
	// need harmonics(j) <= 0.5 / inc  ->  j >= 2 * log2(2048 * inc). Small margin keeps us strictly below Nyquist.
	inline float levelX(float inc)
	{
		return inc > 1.0e-9f ? 2.0f * log2f(2048.0f * 1.001f * inc) : -1000.0f;
	}

	inline int levelFromX(float x)
	{
		const int j = (int)ceilf(x);
		return j < 0 ? 0 : (j >= kNumLevels ? kNumLevels - 1 : j);
	}

	inline int levelForIncrement(float inc)
	{
		return levelFromX(levelX(inc));
	}

	// 4-point, 3rd-order Hermite interpolation. p points at x0, t in [0,1).
	inline float hermite(const float* p, float t)
	{
		const float xm1 = p[-1], x0 = p[0], x1 = p[1], x2 = p[2];
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * t + c2) * t + c1) * t + x0;
	}

	// Built-in tables are generated once per process and shared.
	std::shared_ptr<const Wavetable> getBuiltIn(int index);

	// Parses a .wav wavetable (Serum-style: frames of 2048 samples, or the size in a 'clm ' chunk).
	// Shared by cacheKey (use the full path) while any voice holds it.
	std::shared_ptr<const Wavetable> loadWav(const std::vector<uint8_t>& bytes, const std::string& cacheKey, std::string& error);

	// Builds one table from several files, in order (e.g. a folder of single-cycle waves).
	// A file without a 'clm ' chunk counts as one single cycle, unless its length is an exact
	// multiple of 2048 samples (then it is a multi-frame table). Max kMaxFrames frames in total.
	std::shared_ptr<const Wavetable> loadWavList(const std::vector<std::vector<uint8_t>>& files, const std::string& cacheKey, std::string& error, int& filesUsed);

	// Returns a table previously built with this key, if it is still in use somewhere.
	std::shared_ptr<const Wavetable> findCached(const std::string& cacheKey);

	// Fill 'points' samples of the frame at 'position' (0..1), interpolated between frames.
	void renderDisplayWave(const Wavetable& wt, float position, float* out, int points);
}
