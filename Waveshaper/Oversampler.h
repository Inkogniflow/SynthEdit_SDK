#pragma once

/*
	Polyphase FIR oversampler for one channel.

	Linear-phase Kaiser-windowed sinc, 32 taps per phase (N = 32 * factor + 1), cut-off 0.45 * base rate.
	Up + down together delay the signal by exactly kLatency base-rate samples (factor > 1),
	so a dry signal can be aligned with a simple delay.
*/

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <vector>
#include <algorithm>

namespace sxws
{
	class Oversampler
	{
	public:
		static constexpr int kTapsPerPhase = 32;
		static constexpr int kLatency = kTapsPerPhase; // base samples, when factor > 1
		static constexpr int kMaxChunk = 32;           // largest n passed to upsample()/downsample()

		void setFactor(int factor)
		{
			factor = factor < 1 ? 1 : (factor > 8 ? 8 : factor);
			if (factor == factor_ && !coeffs_.empty())
				return;
			factor_ = factor;
			design();
			reset();
		}

		int factor() const { return factor_; }
		int latency() const { return factor_ > 1 ? kLatency : 0; }

		void reset()
		{
			std::fill(upHistory_.begin(), upHistory_.end(), 0.f);
			std::fill(downHistory_.begin(), downHistory_.end(), 0.f);
		}

		// in: n base samples -> out: n * factor samples
		void upsample(const float* in, int n, float* out)
		{
			const int L = factor_;
			if (L == 1)
			{
				memcpy(out, in, sizeof(float) * n);
				return;
			}
			const int P = kTapsPerPhase + 1;
			for (int k = 0; k < n; ++k)
			{
				// history: newest sample last
				memmove(upHistory_.data(), upHistory_.data() + 1, sizeof(float) * (P - 1));
				upHistory_[P - 1] = in[k];

				for (int p = 0; p < L; ++p)
				{
					const float* c = upPhases_.data() + (size_t)p * P; // reversed: c[P-1] pairs with newest
					float acc = 0.f;
					for (int j = 0; j < P; ++j)
						acc += c[j] * upHistory_[j];
					out[k * L + p] = acc;
				}
			}
		}

		// in: n * factor samples -> out: n base samples
		void downsample(const float* in, int n, float* out)
		{
			const int L = factor_;
			if (L == 1)
			{
				memcpy(out, in, sizeof(float) * n);
				return;
			}
			const int N = (int)coeffs_.size();
			const int H = N - 1;           // history length
			const int total = H + n * L;
			if ((int)work_.size() < total)
				work_.resize(total);

			memcpy(work_.data(), downHistory_.data(), sizeof(float) * H);
			memcpy(work_.data() + H, in, sizeof(float) * n * L);

			// y[k] = sum_i h[i] * x[kL - i]; x[m] is work_[H + m]
			for (int k = 0; k < n; ++k)
			{
				const float* x = work_.data() + H + k * L - (N - 1); // oldest sample used
				const float* c = downReversed_.data();
				float acc = 0.f;
				for (int i = 0; i < N; ++i)
					acc += c[i] * x[i];
				out[k] = acc;
			}

			memcpy(downHistory_.data(), work_.data() + n * L, sizeof(float) * H);
		}

	private:
		static double besselI0(double x)
		{
			double sum = 1.0, term = 1.0;
			for (int k = 1; k < 40; ++k)
			{
				term *= (x / (2.0 * k)) * (x / (2.0 * k));
				sum += term;
				if (term < 1.0e-12 * sum)
					break;
			}
			return sum;
		}

		void design()
		{
			const int L = factor_;
			coeffs_.clear();
			upPhases_.clear();
			downReversed_.clear();
			upHistory_.assign(kTapsPerPhase + 1, 0.f);
			if (L == 1)
			{
				coeffs_.push_back(1.f);
				downHistory_.clear();
				return;
			}

			const int N = kTapsPerPhase * L + 1;
			const double fc = 0.45 / L;  // cycles per oversampled sample
			const double beta = 8.0;
			const double mid = (N - 1) * 0.5;
			std::vector<double> h(N);
			double sum = 0.0;
			for (int i = 0; i < N; ++i)
			{
				const double t = i - mid;
				const double sinc = t == 0.0 ? 2.0 * fc : sin(2.0 * M_PI * fc * t) / (M_PI * t);
				const double r = t / mid;
				const double w = besselI0(beta * sqrt((std::max)(0.0, 1.0 - r * r))) / besselI0(beta);
				h[i] = sinc * w;
				sum += h[i];
			}
			coeffs_.resize(N);
			for (int i = 0; i < N; ++i)
				coeffs_[i] = (float)(h[i] / sum);

			// down: reversed so the dot product runs over ascending history
			downReversed_.resize(N);
			for (int i = 0; i < N; ++i)
				downReversed_[i] = coeffs_[N - 1 - i];
			downHistory_.assign(N - 1, 0.f);
			work_.assign((size_t)(N - 1) + (size_t)kMaxChunk * L, 0.f); // no allocation in downsample()

			// up: out[kL+p] = L * sum_j h[jL+p] * in[k-j], history newest last -> index P-1-j
			const int P = kTapsPerPhase + 1;
			upPhases_.assign((size_t)L * P, 0.f);
			for (int p = 0; p < L; ++p)
			{
				for (int j = 0; j < P; ++j)
				{
					const int idx = j * L + p;
					if (idx < N)
						upPhases_[(size_t)p * P + (P - 1 - j)] = coeffs_[idx] * L;
				}
			}
		}

		int factor_ = 1;
		std::vector<float> coeffs_;
		std::vector<float> upPhases_;
		std::vector<float> downReversed_;
		std::vector<float> upHistory_;
		std::vector<float> downHistory_;
		std::vector<float> work_;
	};
}
