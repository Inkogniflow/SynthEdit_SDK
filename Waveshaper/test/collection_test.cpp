// Validates the 64-function "Collection" table against the real FormulaEngine:
//  compile, 8 x 8 layout, explicit multiplication, finite output, level, zero at origin, continuity in x,
//  continuity in A..D, DC of a sine through the curve, block == scalar evaluation.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include "../FormulaEngine.h"
using namespace sxws;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// token lint: two operands next to each other without an operator = implicit multiplication
static bool hasImplicitMul(const std::string& f)
{
	enum K { None, Operand, Op, Open, Close, Func };
	K prev = None;
	size_t i = 0;
	while (i < f.size())
	{
		const char c = f[i];
		if (isspace((unsigned char)c)) { ++i; continue; }
		K k;
		if (isdigit((unsigned char)c) || c == '.')
		{
			while (i < f.size() && (isdigit((unsigned char)f[i]) || f[i] == '.')) ++i;
			k = Operand;
		}
		else if (isalpha((unsigned char)c))
		{
			size_t j = i;
			while (j < f.size() && isalnum((unsigned char)f[j])) ++j;
			const bool call = j < f.size() && f[j] == '(';
			i = j;
			k = call ? Func : Operand;
		}
		else if (c == '(') { k = Open; ++i; }
		else if (c == ')') { k = Close; ++i; }
		else { k = Op; ++i; if (c == ',') {} }
		const bool prevEnds = (prev == Operand || prev == Close);
		const bool nextStarts = (k == Operand || k == Func || k == Open);
		if (prevEnds && nextStarts) return true;
		prev = k;
	}
	return false;
}

int main()
{
	auto set = getCollection();
	CHECK(set->functions.size() == 64, "collection has %zu functions (want 64)", set->functions.size());
	static const char* cats[8] = { "Dist", "Clip", "Sat", "Fold", "Harm", "Digi", "Asym", "Exp" };

	const int N = 2001;
	std::vector<float> xs(N), y(N), ya(N);
	for (int i = 0; i < N; ++i) xs[i] = -1.f + 2.f * i / (N - 1);
	const float lv[5] = { -1.f, -0.5f, 0.f, 0.5f, 1.f };

	printf("%-3s %-26s %5s %7s %7s %8s %8s %7s\n", "#", "name", "nonfin", "max|y|", "|y(0)|", "stepX", "stepVar", "dcSine");
	for (size_t fi = 0; fi < set->functions.size(); ++fi)
	{
		const Function& f = set->functions[fi];
		CHECK(f.program.ok(), "[%zu] %s: %s", fi + 1, f.name.c_str(), f.program.error().c_str());
		CHECK(f.name.compare(0, strlen(cats[fi / 8]), cats[fi / 8]) == 0, "[%zu] %s not in category %s", fi + 1, f.name.c_str(), cats[fi / 8]);
		CHECK(!hasImplicitMul(f.formula), "[%zu] %s uses implicit multiplication: %s", fi + 1, f.name.c_str(), f.formula.c_str());
		for (size_t g = 0; g < fi; ++g)
			CHECK(set->functions[g].name != f.name, "duplicate name %s", f.name.c_str());

		int nonfinite = 0;
		float maxY = 0, y0 = 0, stepX = 0, stepVar = 0, dc = 0;
		float v[4];
		for (int a = 0; a < 5; ++a) for (int b = 0; b < 5; ++b) for (int c = 0; c < 5; ++c) for (int d = 0; d < 5; ++d)
		{
			std::vector<float> va(N, lv[a]), vb(N, lv[b]), vc(N, lv[c]), vd(N, lv[d]);
			const float* vars[4] = { va.data(), vb.data(), vc.data(), vd.data() };
			f.program.eval(xs.data(), vars, y.data(), N);
			for (int i = 0; i < N; ++i)
			{
				if (!std::isfinite(y[i])) { ++nonfinite; continue; }
				maxY = std::fmax(maxY, std::fabs(y[i]));
				if (i > 0 && std::isfinite(y[i - 1])) stepX = std::fmax(stepX, std::fabs(y[i] - y[i - 1]));
			}
			y0 = std::fmax(y0, std::fabs(y[N / 2]));
			// DC through the curve for sines of amplitude 1, 0.5, 0.1
			for (float amp : { 1.f, 0.5f, 0.1f })
			{
				std::vector<float> s(4096), o(4096);
				for (int i = 0; i < 4096; ++i) s[i] = amp * sinf(6.2831853f * i / 4096.f);
				std::vector<float> A(4096, lv[a]), B(4096, lv[b]), C(4096, lv[c]), D(4096, lv[d]);
				const float* vv[4] = { A.data(), B.data(), C.data(), D.data() };
				f.program.eval(s.data(), vv, o.data(), 4096);
				double m = 0; for (float q : o) m += std::isfinite(q) ? q : 0;
				dc = std::fmax(dc, (float)std::fabs(m / 4096));
			}
		}
		// continuity in A..D: sweep one input, others fixed
		const int NV = 4001;
		std::vector<float> sweep(NV), xc(NV), oy(NV);
		for (int i = 0; i < NV; ++i) sweep[i] = -1.f + 2.f * i / (NV - 1);
		for (int vi = 0; vi < 4; ++vi)
			for (float xv : { -0.9f, -0.5f, -0.1f, 0.1f, 0.5f, 0.9f })
				for (int o3 = 0; o3 < 27; ++o3)
				{
					std::vector<float> flat[3];
					int t = o3;
					const float* vars[4]; int k = 0;
					for (int q = 0; q < 4; ++q)
					{
						if (q == vi) { vars[q] = sweep.data(); continue; }
						flat[k].assign(NV, lv[(t % 3) * 2]); t /= 3; vars[q] = flat[k].data(); ++k;
					}
					std::fill(xc.begin(), xc.end(), xv);
					f.program.eval(xc.data(), vars, oy.data(), NV);
					for (int i = 1; i < NV; ++i)
						if (std::isfinite(oy[i]) && std::isfinite(oy[i - 1]))
							stepVar = std::fmax(stepVar, std::fabs(oy[i] - oy[i - 1]));
				}
		(void)v;
		printf("%-3zu %-26s %5d %7.3f %7.3f %8.3f %8.3f %7.3f\n", fi + 1, f.name.c_str(), nonfinite, maxY, y0, stepX, stepVar, dc);
		CHECK(nonfinite == 0, "[%zu] %s produced NaN/inf", fi + 1, f.name.c_str());
		CHECK(maxY <= 1.15f, "[%zu] %s level %.3f > 1.15", fi + 1, f.name.c_str(), maxY);
		CHECK(y0 <= 1e-3f, "[%zu] %s output at x=0 is %.4f (DC at silence)", fi + 1, f.name.c_str(), y0);
	}

	// block evaluation equals scalar evaluation, also across the 256-sample block boundary
	{
		std::vector<float> x(700), a(700), b(700), c(700), d(700), yb(700);
		for (int i = 0; i < 700; ++i) { x[i] = sinf(i * 0.07f); a[i] = sinf(i * 0.013f); b[i] = cosf(i * 0.021f); c[i] = sinf(i * 0.3f); d[i] = cosf(i * 0.11f); }
		const float* vars[4] = { a.data(), b.data(), c.data(), d.data() };
		for (auto& f : set->functions)
		{
			f.program.eval(x.data(), vars, yb.data(), 700);
			for (int i = 0; i < 700; i += 37)
			{
				const float vv[4] = { a[i], b[i], c[i], d[i] };
				const float ys = f.program.evalOne(x[i], vv);
				CHECK(fabsf(ys - yb[i]) < 1e-5f || (!std::isfinite(ys) && !std::isfinite(yb[i])), "%s block != scalar at %d", f.name.c_str(), i);
			}
		}
	}

	printf("\n%s (%d)\n", fails ? "FAILED" : "ALL PASSED", fails);
	return fails;
}
