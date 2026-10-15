#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>
#include <map>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include "WavetableEngine.h"
#include "WavetableDisplayFormat.h"

namespace fvwt
{
	using cd = std::complex<double>;
	using Spectrum = std::vector<cd>; // index k = harmonic k (k = 1..kMaxHarmonics)

	namespace
	{
		std::atomic<uint32_t> g_serial{ 1 };

		// In-place radix-2 FFT. Inverse is unscaled.
		void fft(std::vector<cd>& a, bool inverse)
		{
			const size_t n = a.size();
			for (size_t i = 1, j = 0; i < n; ++i)
			{
				size_t bit = n >> 1;
				for (; j & bit; bit >>= 1)
					j ^= bit;
				j ^= bit;
				if (i < j)
					std::swap(a[i], a[j]);
			}
			for (size_t len = 2; len <= n; len <<= 1)
			{
				const double ang = 2.0 * M_PI / (double)len * (inverse ? 1.0 : -1.0);
				const cd wlen(cos(ang), sin(ang));
				for (size_t i = 0; i < n; i += len)
				{
					cd w(1.0);
					for (size_t k = 0; k < len / 2; ++k)
					{
						const cd u = a[i + k];
						const cd v = a[i + k + len / 2] * w;
						a[i + k] = u + v;
						a[i + k + len / 2] = u - v;
						w *= wlen;
					}
				}
			}
		}

		int harmonicsForLevel(int j)
		{
			const int h = (int)floor(1024.0 * pow(2.0, -0.5 * j) + 1e-9);
			return (std::max)(1, (std::min)(h, kMaxHarmonics));
		}

		int nextPow2(int v)
		{
			int p = 1;
			while (p < v)
				p <<= 1;
			return p;
		}

		void initLayout(Wavetable& wt)
		{
			int offset = 0;
			for (int j = 0; j < kNumLevels; ++j)
			{
				const int size = (std::min)(4096, (std::max)(64, nextPow2(8 * harmonicsForLevel(j))));
				wt.levelSize[j] = size;
				wt.levelOffset[j] = offset;
				offset += size + 3; // guards: 1 before, 2 after (for Hermite interpolation)
			}
			wt.frameStride = offset;
		}

		// Spectrum of one cycle of N real samples (N power of two).
		Spectrum spectrumFromCycle(const std::vector<double>& cycle)
		{
			const size_t n = cycle.size();
			std::vector<cd> buf(n);
			for (size_t i = 0; i < n; ++i)
				buf[i] = cycle[i];
			fft(buf, false);

			Spectrum s(kMaxHarmonics + 1, cd(0.0));
			const int maxK = (std::min)(kMaxHarmonics, (int)n / 2 - 1);
			for (int k = 1; k <= maxK; ++k)
				s[k] = buf[k] * (2.0 / (double)n);
			return s; // DC (k = 0) dropped on purpose
		}

		std::shared_ptr<Wavetable> buildFromSpectra(const std::vector<Spectrum>& spectra, bool normalizePerFrame)
		{
			auto wt = std::make_shared<Wavetable>();
			wt->frames = (int)spectra.size();
			initLayout(*wt);
			wt->data.assign((size_t)wt->frames * wt->frameStride, 0.0f);

			std::vector<cd> buf;
			std::vector<float> framePeak(wt->frames, 0.0f);

			for (int f = 0; f < wt->frames; ++f)
			{
				for (int j = 0; j < kNumLevels; ++j)
				{
					const int size = wt->levelSize[j];
					const int harmonics = (std::min)(harmonicsForLevel(j), size / 2 - 1);

					buf.assign(size, cd(0.0));
					for (int k = 1; k <= harmonics; ++k)
						buf[k] = spectra[f][k];
					fft(buf, true);

					float* dst = wt->data.data() + (size_t)f * wt->frameStride + wt->levelOffset[j] + 1;
					for (int i = 0; i < size; ++i)
						dst[i] = (float)buf[i].real();
					dst[-1] = dst[size - 1];
					dst[size] = dst[0];
					dst[size + 1] = dst[1];

					if (j == 0)
					{
						for (int i = 0; i < size; ++i)
							framePeak[f] = (std::max)(framePeak[f], fabsf(dst[i]));
					}
				}
			}

			// Normalise.
			const float globalPeak = *std::max_element(framePeak.begin(), framePeak.end());
			for (int f = 0; f < wt->frames; ++f)
			{
				const float peak = normalizePerFrame ? framePeak[f] : globalPeak;
				if (peak > 1.0e-9f)
				{
					const float gain = 1.0f / peak;
					float* p = wt->data.data() + (size_t)f * wt->frameStride;
					for (int i = 0; i < wt->frameStride; ++i)
						p[i] *= gain;
				}
			}

			// Display copy (level 0, resampled to kDisplayWavePoints).
			wt->displayFrames.resize((size_t)wt->frames * kDisplayWavePoints);
			const int size0 = wt->levelSize[0];
			for (int f = 0; f < wt->frames; ++f)
			{
				const float* src = wt->level(f, 0);
				for (int i = 0; i < kDisplayWavePoints; ++i)
					wt->displayFrames[(size_t)f * kDisplayWavePoints + i] = src[(size_t)i * size0 / kDisplayWavePoints];
			}

			wt->serial = g_serial++;
			return wt;
		}

		// ---- Built-in tables ----------------------------------------------------

		constexpr int kBuiltInFrames = 64;
		constexpr int kGenSize = 16384; // generate 8x oversampled, then keep the first 1023 harmonics

		template<typename Fn>
		std::vector<Spectrum> fromTimeDomain(Fn shape)
		{
			std::vector<Spectrum> spectra;
			std::vector<double> cycle(kGenSize);
			for (int f = 0; f < kBuiltInFrames; ++f)
			{
				const double t = f / (double)(kBuiltInFrames - 1);
				for (int i = 0; i < kGenSize; ++i)
					cycle[i] = shape(t, i / (double)kGenSize);
				spectra.push_back(spectrumFromCycle(cycle));
			}
			return spectra;
		}

		double frac(double x) { return x - floor(x); }

		std::vector<Spectrum> makeBuiltIn(int index)
		{
			switch (index)
			{
			default:
			case Classic:
				return fromTimeDomain([](double t, double x)
				{
					const double sine = sin(2.0 * M_PI * x);
					const double tri = x < 0.25 ? 4.0 * x : (x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0);
					const double saw = 2.0 * frac(x + 0.5) - 1.0;
					const double sqr = x < 0.5 ? 1.0 : -1.0;
					const double shapes[4] = { sine, tri, saw, sqr };
					const double s = t * 3.0;
					const int seg = (std::min)((int)s, 2);
					const double u = s - seg;
					return shapes[seg] * (1.0 - u) + shapes[seg + 1] * u;
				});

			case Pwm:
				return fromTimeDomain([](double t, double x)
				{
					const double duty = 0.5 - 0.47 * t;
					return x < duty ? 1.0 : -1.0;
				});

			case SyncSweep:
				return fromTimeDomain([](double t, double x)
				{
					const double ratio = 1.0 + 7.0 * t;
					return 2.0 * frac(x * ratio) - 1.0;
				});

			case Wavefold:
				return fromTimeDomain([](double t, double x)
				{
					const double gain = 1.0 + 9.0 * t;
					return sin(0.5 * M_PI * gain * sin(2.0 * M_PI * x));
				});

			case HarmonicSweep:
			{
				std::vector<Spectrum> spectra;
				for (int f = 0; f < kBuiltInFrames; ++f)
				{
					const double t = f / (double)(kBuiltInFrames - 1);
					const double cutoff = pow(256.0, t); // 1 .. 256 harmonics
					Spectrum s(kMaxHarmonics + 1, cd(0.0));
					for (int k = 1; k <= kMaxHarmonics; ++k)
					{
						const double a = (1.0 / k) / (1.0 + pow(k / cutoff, 8.0));
						s[k] = cd(0.0, -a); // sine phase
					}
					spectra.push_back(s);
				}
				return spectra;
			}

			case Formant:
			{
				// Formant frequencies (Hz) and gains for A E I O U, sung at ~110 Hz.
				const double vowels[5][3] = {
					{ 800, 1150, 2900 }, // A
					{ 400, 2000, 2800 }, // E
					{ 270, 2250, 3000 }, // I
					{ 450,  800, 2830 }, // O
					{ 325,  700, 2530 }, // U
				};
				const double gains[3] = { 1.0, 0.5, 0.25 };
				const double widths[3] = { 90.0, 110.0, 160.0 };
				const double f0 = 110.0;

				std::vector<Spectrum> spectra;
				for (int f = 0; f < kBuiltInFrames; ++f)
				{
					const double t = f / (double)(kBuiltInFrames - 1);
					const double s4 = t * 4.0;
					const int seg = (std::min)((int)s4, 3);
					const double u = s4 - seg;

					double formants[3];
					for (int i = 0; i < 3; ++i)
						formants[i] = vowels[seg][i] * (1.0 - u) + vowels[seg + 1][i] * u;

					Spectrum s(kMaxHarmonics + 1, cd(0.0));
					for (int k = 1; k <= kMaxHarmonics; ++k)
					{
						const double hz = k * f0;
						double env = 0.01;
						for (int i = 0; i < 3; ++i)
						{
							const double d = (hz - formants[i]) / widths[i];
							env += gains[i] * exp(-0.5 * d * d);
						}
						const double a = env / sqrt((double)k);
						s[k] = cd(0.0, -a);
					}
					spectra.push_back(s);
				}
				return spectra;
			}
			}
		}

		// ---- Cache ------------------------------------------------------------

		std::mutex& cacheMutex()
		{
			static std::mutex m;
			return m;
		}

		std::map<std::string, std::weak_ptr<const Wavetable>>& cache()
		{
			static std::map<std::string, std::weak_ptr<const Wavetable>> c;
			return c;
		}

		// ---- WAV parsing ------------------------------------------------------

		uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
		uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
	}

	std::shared_ptr<const Wavetable> getBuiltIn(int index)
	{
		index = (std::max)(0, (std::min)(index, NumBuiltIns - 1));
		const std::string key = "builtin:" + std::to_string(index);

		std::lock_guard<std::mutex> lock(cacheMutex());
		if (auto existing = cache()[key].lock())
			return existing;

		std::shared_ptr<const Wavetable> wt = buildFromSpectra(makeBuiltIn(index), true);
		cache()[key] = wt;
		return wt;
	}

	namespace
	{
		// Decodes a WAV file and appends its cycles (as spectra) to 'out', at most 'maxFrames'.
		// listMode: a file without 'clm ' chunk is one single cycle unless it is an exact multiple of 2048 samples.
		bool decodeWav(const std::vector<uint8_t>& bytes, bool listMode, int maxFrames, std::vector<Spectrum>& out, std::string& error)
		{
			const uint8_t* p = bytes.data();
			const size_t n = bytes.size();
			if (n < 12 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0)
			{
				error = "not a WAV file";
				return false;
			}

			int format = 0, channels = 0, bits = 0;
			const uint8_t* data = nullptr;
			size_t dataSize = 0;
			int clmFrameSize = 0;

			size_t pos = 12;
			while (pos + 8 <= n)
			{
				const uint8_t* id = p + pos;
				const size_t size = rd32(p + pos + 4);
				const uint8_t* body = p + pos + 8;
				const size_t avail = (std::min)(size, n - (pos + 8));

				if (memcmp(id, "fmt ", 4) == 0 && avail >= 16)
				{
					format = rd16(body);
					channels = rd16(body + 2);
					bits = rd16(body + 14);
					if (format == 0xFFFE && avail >= 26)
						format = rd16(body + 24); // WAVE_FORMAT_EXTENSIBLE sub-format
				}
				else if (memcmp(id, "data", 4) == 0)
				{
					data = body;
					dataSize = avail;
				}
				else if (memcmp(id, "clm ", 4) == 0)
				{
					// Serum writes e.g. "<!>2048 ..." - the number is the frame size.
					std::string s((const char*)body, avail);
					const auto at = s.find("<!>");
					if (at != std::string::npos)
						clmFrameSize = atoi(s.c_str() + at + 3);
				}

				pos += 8 + size + (size & 1);
			}

			if (!data || channels < 1 || !(format == 1 || format == 3))
			{
				error = "unsupported WAV format (use PCM or 32-bit float)";
				return false;
			}

			const int bytesPerSample = bits / 8;
			if (bytesPerSample < 1 || (format == 3 && bits != 32 && bits != 64) || (format == 1 && bits > 32))
			{
				error = "unsupported WAV bit depth";
				return false;
			}

			const size_t stride = (size_t)bytesPerSample * channels;
			const size_t total = dataSize / stride;
			if (total < 8)
			{
				error = "WAV file is too short";
				return false;
			}

			// First channel only.
			std::vector<double> samples(total);
			for (size_t i = 0; i < total; ++i)
			{
				const uint8_t* s = data + i * stride;
				double v = 0.0;
				if (format == 3)
				{
					if (bits == 32) { float f; memcpy(&f, s, 4); v = f; }
					else { double d; memcpy(&d, s, 8); v = d; }
				}
				else switch (bits)
				{
				case 8:  v = (s[0] - 128) / 128.0; break;
				case 16: v = (int16_t)rd16(s) / 32768.0; break;
				case 24: v = (int32_t)((uint32_t)(s[0] << 8 | s[1] << 16 | (uint32_t)s[2] << 24)) / 2147483648.0; break;
				case 32: v = (int32_t)rd32(s) / 2147483648.0; break;
				}
				samples[i] = std::isfinite(v) ? v : 0.0;
			}

			size_t frameSize;
			if (clmFrameSize > 0)
				frameSize = (size_t)clmFrameSize;
			else if (listMode)
				frameSize = (total > 2048 && total % 2048 == 0) ? 2048 : total;
			else
				frameSize = 2048;

			if (total < frameSize)
				frameSize = total; // single-cycle file

			const int frames = (int)(std::min)((size_t)maxFrames, total / frameSize);

			// Resample each frame to 2048 samples (exact copy when it already is 2048).
			constexpr int kCycle = 2048;
			std::vector<double> cycle(kCycle);
			for (int f = 0; f < frames; ++f)
			{
				const double* src = samples.data() + (size_t)f * frameSize;
				for (int i = 0; i < kCycle; ++i)
				{
					const double x = i * (double)frameSize / kCycle;
					const size_t i0 = (size_t)x;
					const size_t i1 = (i0 + 1) % frameSize;
					const double t = x - i0;
					cycle[i] = src[i0] + t * (src[i1] - src[i0]);
				}
				out.push_back(spectrumFromCycle(cycle));
			}
			return frames > 0;
		}
	}

	std::shared_ptr<const Wavetable> findCached(const std::string& cacheKey)
	{
		std::lock_guard<std::mutex> lock(cacheMutex());
		auto it = cache().find(cacheKey);
		return it == cache().end() ? nullptr : it->second.lock();
	}

	std::shared_ptr<const Wavetable> loadWav(const std::vector<uint8_t>& bytes, const std::string& cacheKey, std::string& error)
	{
		if (auto existing = findCached(cacheKey))
			return existing;

		std::vector<Spectrum> spectra;
		if (!decodeWav(bytes, false, kMaxFrames, spectra, error))
			return {};

		std::shared_ptr<const Wavetable> wt = buildFromSpectra(spectra, false);

		std::lock_guard<std::mutex> lock(cacheMutex());
		cache()[cacheKey] = wt;
		return wt;
	}

	std::shared_ptr<const Wavetable> loadWavList(const std::vector<std::vector<uint8_t>>& files, const std::string& cacheKey, std::string& error, int& filesUsed)
	{
		filesUsed = 0;
		if (auto existing = findCached(cacheKey))
			return existing;

		std::vector<Spectrum> spectra;
		for (const auto& bytes : files)
		{
			const int room = kMaxFrames - (int)spectra.size();
			if (room <= 0)
				break;

			std::string fileError;
			if (decodeWav(bytes, true, room, spectra, fileError))
				++filesUsed;
		}

		if (spectra.empty())
		{
			error = "no usable WAV files";
			return {};
		}

		// Files come from different sources - normalise each frame so the morph stays even in level.
		std::shared_ptr<const Wavetable> wt = buildFromSpectra(spectra, true);

		std::lock_guard<std::mutex> lock(cacheMutex());
		cache()[cacheKey] = wt;
		return wt;
	}

	void renderDisplayWave(const Wavetable& wt, float position, float* out, int points)
	{
		if (wt.frames <= 0)
		{
			for (int i = 0; i < points; ++i)
				out[i] = 0.0f;
			return;
		}

		position = (std::max)(0.0f, (std::min)(position, 1.0f));
		const float fpos = position * (wt.frames - 1);
		const int f0 = (std::min)((int)fpos, wt.frames - 1);
		const int f1 = (std::min)(f0 + 1, wt.frames - 1);
		const float ft = fpos - f0;

		const float* a = wt.displayFrames.data() + (size_t)f0 * kDisplayWavePoints;
		const float* b = wt.displayFrames.data() + (size_t)f1 * kDisplayWavePoints;
		for (int i = 0; i < points; ++i)
		{
			const int idx = (int)((size_t)i * kDisplayWavePoints / points);
			out[i] = a[idx] + ft * (b[idx] - a[idx]);
		}
	}
}
