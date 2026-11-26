// CPU + aliasing benchmark of the Collection (real FormulaEngine + Oversampler). Not part of run_tests.sh.
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <algorithm>
#include "../FormulaEngine.h"
#include "../Oversampler.h"
using namespace sxws;
static const double kRate = 48000;

// full chain for one channel: up -> eval -> down, returns n base samples (no DC block)
static std::vector<float> chain(const Program& p, int F, const std::vector<float>& in, const float vv[4])
{
	Oversampler os; os.setFactor(F);
	const int n = (int)in.size();
	std::vector<float> out(n), up(32 * 8), y(32 * 8);
	std::vector<float> va(256, vv[0]), vb(256, vv[1]), vc(256, vv[2]), vd(256, vv[3]);
	for (int s = 0; s < n; s += 32)
	{
		const int c = std::min(32, n - s), m = c * F;
		os.upsample(in.data() + s, c, up.data());
		const float* vars[4] = { va.data(), vb.data(), vc.data(), vd.data() };
		p.eval(up.data(), vars, y.data(), m);
		for (int i = 0; i < m; ++i) y[i] = std::isfinite(y[i]) ? std::max(-4.f, std::min(4.f, y[i])) : 0.f;
		os.downsample(y.data(), c, out.data() + s);
	}
	return out;
}

int main()
{
	auto set = getCollection();
	// ---- CPU: eval cost per sample, converted to % of one core for stereo @48k
	const int N = 256, REPS = 4000;
	std::vector<float> x(N), a(N), b(N), c(N), d(N), y(N);
	for (int i = 0; i < N; ++i) { x[i] = 0.9f * sinf(i * 0.13f); a[i] = 0.5f * sinf(i * 0.05f); b[i] = 0.5f * sinf(i * 0.31f); c[i] = 0.3f * cosf(i * 0.011f); d[i] = 0.5f * cosf(i * 0.7f); }
	const float* vars[4] = { a.data(), b.data(), c.data(), d.data() };
	printf("%-3s %-26s %8s | %6s %6s %6s %6s (%% of 1 core, stereo, 48k, eval only) | alias dB vs 8x: %6s %6s %6s (mid | high drive)\n", "#", "name", "ns/samp", "Off", "2x", "4x", "8x", "2x", "4x", "8x");
	double sumNs = 0; std::vector<std::pair<double, std::string>> rank;
	for (size_t fi = 0; fi < set->functions.size(); ++fi)
	{
		const Program& p = set->functions[fi].program;
		float sink = 0;
		auto t0 = std::chrono::steady_clock::now();
		for (int r = 0; r < REPS; ++r) { p.eval(x.data(), vars, y.data(), N); sink += y[r % N]; }
		auto t1 = std::chrono::steady_clock::now();
		const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / ((double)REPS * N);
		sumNs += ns; rank.push_back({ ns, set->functions[fi].name });
		double cpu[4]; int Fs[4] = { 1, 2, 4, 8 };
		for (int k = 0; k < 4; ++k) cpu[k] = ns * 1e-9 * kRate * Fs[k] * 2 * 100.0;

		// aliasing: 1337 Hz sine, 0.9 amplitude, reference = 8x
		double al[2][3];
		const float settings[2][4] = { { 0, 0, 0, 0 }, { 0.8f, 0.8f, 0.8f, 0.8f } };
		for (int s = 0; s < 2; ++s)
		{
			const int len = 9600;
			std::vector<float> in(len);
			for (int i = 0; i < len; ++i) in[i] = 0.9f * sinf(2.f * 3.14159265f * 1337.f * i / (float)kRate);
			auto ref = chain(p, 8, in, settings[s]);
			for (int k = 1; k < 4; ++k)
			{
				auto o = chain(p, Fs[k], in, settings[s]);
				double e = 0, r = 0;
				for (int i = 600; i < len; ++i) { const double dd = o[i] - ref[i]; e += dd * dd; r += (double)ref[i] * ref[i]; }
				al[s][k - 1] = r > 1e-12 ? 10 * log10(std::max(e / r, 1e-12)) : -120;
			}
		}
		printf("%-3zu %-26s %8.1f | %6.2f %6.2f %6.2f %6.2f | %5.0f|%-4.0f %5.0f|%-4.0f %5.0f|%-4.0f%s\n", fi + 1, set->functions[fi].name.c_str(), ns, cpu[0], cpu[1], cpu[2], cpu[3],
			al[0][0], al[1][0], al[0][1], al[1][1], al[0][2], al[1][2], sink == 12345.f ? "!" : "");
	}
	std::sort(rank.rbegin(), rank.rend());
	printf("\nmean %.1f ns/sample; most expensive:\n", sumNs / 64);
	for (int i = 0; i < 8; ++i) printf("  %-26s %.1f ns\n", rank[i].second.c_str(), rank[i].first);
	printf("  cheapest: %-20s %.1f ns\n", rank.back().second.c_str(), rank.back().first);
}
