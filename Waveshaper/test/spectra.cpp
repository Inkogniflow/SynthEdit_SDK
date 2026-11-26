// Harmonic-spectrum distinctness of the Collection (real FormulaEngine). Not part of run_tests.sh.
#include <cstdio>
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>
#include <random>
#include "../FormulaEngine.h"
using namespace sxws;
static const int N = 4096, H = 16;
int main()
{
	auto set = getCollection();
	const int nf = 64;
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1.f, 1.f);
	std::vector<std::array<float, 4>> S; // settings
	S.push_back({ 0, 0, 0, 0 });
	for (int i = 0; i < 11; ++i) S.push_back({ u(rng), u(rng), u(rng), u(rng) });
	const float amps[2] = { 0.8f, 0.25f };
	// feature[f][setting] = vector of 2*H dB values
	std::vector<std::vector<std::vector<float>>> F(nf, std::vector<std::vector<float>>(S.size()));
	for (int f = 0; f < nf; ++f)
		for (size_t s = 0; s < S.size(); ++s)
			for (float amp : amps)
			{
				std::vector<float> in(N), out(N), a(N, S[s][0]), b(N, S[s][1]), c(N, S[s][2]), d(N, S[s][3]);
				for (int i = 0; i < N; ++i) in[i] = amp * sinf(2.f * 3.14159265f * i / 256.f); // 16 periods
				const float* v[4] = { a.data(), b.data(), c.data(), d.data() };
				set->functions[f].program.eval(in.data(), v, out.data(), N);
				for (int h = 1; h <= H; ++h)
				{
					double re = 0, im = 0;
					for (int i = 0; i < N; ++i) { const double ph = 2 * M_PI * 16 * h * i / N; re += out[i] * cos(ph); im += out[i] * sin(ph); }
					const double mag = 2 * sqrt(re * re + im * im) / N;
					F[f][s].push_back((float)std::max(-80.0, 20 * log10(mag + 1e-9)));
				}
				// DC bin
			}
	struct P { double dMid, dAvg; int i, j; };
	std::vector<P> ps;
	for (int i = 0; i < nf; ++i) for (int j = i + 1; j < nf; ++j)
	{
		double avg = 0, mid = 0;
		for (size_t s = 0; s < S.size(); ++s)
		{
			double e = 0; for (size_t k = 0; k < F[i][s].size(); ++k) { const double dd = F[i][s][k] - F[j][s][k]; e += dd * dd; }
			e = sqrt(e / F[i][s].size());
			if (s == 0) mid = e; avg += e;
		}
		ps.push_back({ mid, avg / S.size(), i, j });
	}
	std::sort(ps.begin(), ps.end(), [](const P& a, const P& b) { return a.dAvg < b.dAvg; });
	printf("closest pairs by mean harmonic-spectrum distance (dB RMS over 12 A-D settings x 2 levels x 16 harmonics); mid = at 0 V\n");
	for (int k = 0; k < 20; ++k)
		printf("%5.1f (mid %5.1f)  %-26s | %s\n", ps[k].dAvg, ps[k].dMid, set->functions[ps[k].i].name.c_str(), set->functions[ps[k].j].name.c_str());
	printf("median pair distance %.1f dB\n", ps[ps.size() / 2].dAvg);
}
