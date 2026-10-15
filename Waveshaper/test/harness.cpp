// Minimal SynthEdit-like host for the SXE Waveshaper modules: loads them through MP_GetFactory,
// drives DSP and GUI parts, records drawing calls (incl. text) for rendering.
#include "harness_infra.inc"

// Waveshaper pin ids (order in Waveshaper.xml)
enum { P_INL, P_INR, P_POS, P_A, P_B, P_C, P_D, P_MIX, P_TABLE, P_FUNCS, P_OS, P_DC, P_OUTL, P_OUTR, P_DTABLE, P_DCURVE };
static const std::vector<int> kAudioPins = { P_INL, P_INR, P_POS, P_A, P_B, P_C, P_D, P_MIX, P_OUTL, P_OUTR };
static const wchar_t* kId = L"Sxynthedit Waveshaper";

static void init(DspModule& m, int table = 0, int os = 2, bool dc = true, const std::wstring& funcs = L"")
{
	for (int p : { P_INL, P_INR, P_POS, P_A, P_B, P_C, P_D }) m.setAudio(p, 0.f);
	m.setAudio(P_MIX, 1.f);
	m.setInt(P_TABLE, table); m.setString(P_FUNCS, funcs); m.setInt(P_OS, os); m.setBool(P_DC, dc);
}

// fills inputs with a function of the absolute sample index, runs, collects both outputs
static void runFn(DspModule& m, int samples, std::function<void(int, float&, float&)> in, std::vector<float>& outL, std::vector<float>& outR,
                  std::function<void(int)> perSample = nullptr)
{
	for (int done = 0; done < samples; done += kBlock)
	{
		for (int i = 0; i < kBlock; ++i)
		{
			float l = 0, r = 0;
			in(done + i, l, r);
			m.buffers[P_INL][i] = l; m.buffers[P_INR][i] = r;
			if (perSample) perSample(i + done);
		}
		m.process();
		outL.insert(outL.end(), m.buffers[P_OUTL].begin(), m.buffers[P_OUTL].end());
		outR.insert(outR.end(), m.buffers[P_OUTR].begin(), m.buffers[P_OUTR].end());
	}
}

static double toneLevel(const std::vector<float>& v, size_t from, double hz)
{
	std::complex<double> acc = 0; size_t n = v.size() - from;
	for (size_t i = from; i < v.size(); ++i)
	{
		const double w = 0.5 - 0.5 * std::cos(2 * M_PI * (i - from) / (n - 1)); // Hann
		acc += std::polar((double)v[i] * w, -2 * M_PI * hz * (i - from) / kRate);
	}
	return std::abs(acc) / n * 4.0; // amplitude (Hann gain 0.5)
}

static int tableCount(TestDspHost& h)
{
	auto& b = h.pinValues[P_DTABLE];
	if (b.size() < sizeof(sxws::TableBlobHeader)) return -1;
	sxws::TableBlobHeader hd; memcpy(&hd, b.data(), sizeof(hd));
	return hd.magic == sxws::kTableMagic ? hd.count : -2;
}

static bool curveHeader(TestDspHost& h, sxws::CurveBlobHeader& hd)
{
	auto& b = h.pinValues[P_DCURVE];
	if (b.size() < sizeof(hd)) return false;
	memcpy(&hd, b.data(), sizeof(hd));
	return hd.magic == sxws::kCurveMagic;
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	char b[200];

	printf("SXE Waveshaper (DSP), host block %d\n", kBlock);
	DspModule ws;
	CHECK(ws.create(kId, kAudioPins), "created via factory, setHost, open");
	init(ws, 0, 2, false);
	auto sine = [](double hz, float amp) { return [=](int i, float& l, float& r) { l = r = amp * (float)std::sin(2 * M_PI * hz * i / kRate); }; };
	{
		std::vector<float> l, r;
		runFn(ws, 4800, sine(440, 0.4f), l, r);
		double err = 0; for (size_t i = 32; i < l.size(); ++i) err = std::max(err, (double)std::fabs(l[i] - 0.4f * (float)std::sin(2 * M_PI * 440 * (i - 32) / kRate)));
		snprintf(b, sizeof b, "Saturation pos 0 (Clean: x), 4x: output = input delayed 32 samples (max err %.5f)", err);
		CHECK(err < 2e-3, b);
		CHECK(ws.host.streaming[P_OUTL] && ws.host.streaming[P_OUTR], "outputs flagged streaming");
		snprintf(b, sizeof b, "Display Table sent: %d functions (expect 8)", tableCount(ws.host)); CHECK(tableCount(ws.host) == 8, b);
		sxws::CurveBlobHeader ch; CHECK(curveHeader(ws.host, ch) && ch.points == sxws::kDisplayCurvePoints, "Display Curve sent");
		snprintf(b, sizeof b, "Display Curve input peak %.3f (expect 0.8 = 4 V)", ch.peakL); CHECK(std::fabs(ch.peakL - 0.8f) < 0.02f, b);
	}

	// oversampling off: exact identity, no latency
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 0, 0, false);
		std::vector<float> l, r; runFn(m, 1000, sine(1000, 0.45f), l, r);
		double err = 0; for (size_t i = 0; i < l.size(); ++i) err = std::max(err, (double)std::fabs(l[i] - 0.45f * (float)std::sin(2 * M_PI * 1000 * i / kRate)));
		snprintf(b, sizeof b, "Oversampling off: identity, no latency (max err %.2g)", err); CHECK(err < 1e-6, b);
	}

	// A(-1.5,3.3): -5 V -> -1.5, 0 V -> 0.9, +5 V -> 3.3, clamped outside -5..+5 V
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 0, false, L"A(-1.5,3.3)");
		struct { float volts, expect; } cases[] = { { -5, -1.5f }, { 0, 0.9f }, { 5, 3.3f }, { 8, 3.3f }, { -9, -1.5f }, { 2.5f, 2.1f } };
		bool ok = true; std::string info;
		for (auto& c : cases)
		{
			m.setAudio(P_A, c.volts / 10.f);
			std::vector<float> l, r; runFn(m, 200, [](int, float& a, float& bb) { a = bb = 0; }, l, r);
			const float y = l.back() * 2.f; // volts / 5
			char t[60]; snprintf(t, 60, " %+.1fV->%.3f", c.volts, y); info += t;
			if (std::fabs(y - c.expect) > 1e-4f) ok = false;
		}
		snprintf(b, sizeof b, "A(-1.5,3.3) scales the +/-5 V input:%s", info.c_str()); CHECK(ok, b);
	}

	// B, C, D and the default range
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 0, false, L"B(0,1) + C(10,20) + D");
		m.setAudio(P_B, 0.5f); m.setAudio(P_C, -0.5f); m.setAudio(P_D, 0.25f);
		std::vector<float> l, r; runFn(m, 200, [](int, float& a, float& bb) { a = bb = 0; }, l, r);
		// 1 + 10 + 0.5 = 11.5 -> clamped at 4 (20 V)
		snprintf(b, sizeof b, "B, C, D inputs; output clamped to +/-20 V (got %.2f V)", l.back() * 10); CHECK(std::fabs(l.back() - 2.f) < 1e-5f, b);
		m.setString(P_FUNCS, L"(B(0,1) + C(-1,1) + D)/4");
		l.clear(); r.clear(); runFn(m, 200, [](int, float& a, float& bb) { a = bb = 0; }, l, r);
		snprintf(b, sizeof b, "(B(0,1)+C(-1,1)+D)/4 with B=+5V, C=-5V, D=2.5V -> %.4f (expect 0.125)", l.back() * 2); CHECK(std::fabs(l.back() * 2 - 0.125f) < 1e-5f, b);
	}

	// Position morph
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 0, false, L"x; -x; 0.5");
		struct { float pos, expect; } cases[] = { { 0.f, 0.4f }, { 0.25f, 0.f }, { 0.5f, -0.4f }, { 0.75f, 0.05f }, { 1.f, 0.5f }, { 1.5f, 0.5f } };
		bool ok = true; std::string info;
		for (auto& c : cases)
		{
			m.setAudio(P_POS, c.pos);
			std::vector<float> l, r; runFn(m, 200, [](int, float& a, float& bb) { a = bb = 0.2f; }, l, r);
			const float y = l.back() * 2.f;
			char t[60]; snprintf(t, 60, " %.1fV->%.2f", c.pos * 10, y); info += t;
			if (std::fabs(y - c.expect) > 1e-5f) ok = false;
		}
		snprintf(b, sizeof b, "Position morphs x -> -x -> 0.5 (x=0.4):%s", info.c_str()); CHECK(ok, b);
	}

	// Stereo independence
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 0, 2, true);
		m.setAudio(P_POS, 1.f / 7 * 1); // Soft Clip
		std::vector<float> l, r; runFn(m, 4800, [](int i, float& a, float& bb) { a = 0.5f * (float)std::sin(2 * M_PI * 300 * i / kRate); bb = 0; }, l, r);
		snprintf(b, sizeof b, "stereo: L driven (peak %.2f V), R silent (peak %.2g V)", peak(l) * 10, peak(r) * 10);
		CHECK(peak(l) > 0.3f && peak(r) < 1e-6f, b);
	}

	// Aliasing: tanh drive 9 on a 5 kHz full-scale sine, alias components vs fundamental
	{
		auto aliasDb = [&](int os)
		{
			DspModule m; m.create(kId, kAudioPins); init(m, 5, os, false, L"tanh(x*9)");
			std::vector<float> l, r; runFn(m, 48000, sine(5000, 0.5f), l, r);
			const double fund = toneLevel(l, 4000, 5000);
			double worst = 0;
			for (int h = 3; h <= 31; h += 2)
			{
				double f = std::fmod(5000.0 * h, kRate); if (f > kRate / 2) f = kRate - f;
				if (std::fabs(std::fmod(f, 5000.0)) < 1 || std::fabs(std::fmod(f, 5000.0) - 5000) < 1) continue; // lands on a real harmonic
				worst = std::max(worst, toneLevel(l, 4000, f));
			}
			return 20 * std::log10(worst / fund);
		};
		const double off = aliasDb(0), x2 = aliasDb(1), x4 = aliasDb(2), x8 = aliasDb(3);
		snprintf(b, sizeof b, "aliasing tanh(9x) @5 kHz, worst alias: off %.0f dB, 2x %.0f dB, 4x %.0f dB, 8x %.0f dB", off, x2, x4, x8);
		CHECK(x4 < off - 20 && x8 < x4 && x8 < -60, b);
	}

	// Latency-compensated dry/wet: with "x" any mix must equal the delayed input (no comb filtering)
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 3, false, L"x"); m.setAudio(P_MIX, 0.5f);
		std::vector<float> l, r; runFn(m, 4800, sine(3000, 0.4f), l, r);
		double err = 0; for (size_t i = 32; i < l.size(); ++i) err = std::max(err, (double)std::fabs(l[i] - 0.4f * (float)std::sin(2 * M_PI * 3000 * (i - 32) / kRate)));
		snprintf(b, sizeof b, "Mix 5 V at 8x: dry aligned with wet (max err %.4f)", err); CHECK(err < 3e-3, b);
	}

	// DC block on an asymmetric curve
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 1, true, L"abs(x)");
		std::vector<float> l, r; runFn(m, 48000, sine(200, 0.5f), l, r);
		double mean = 0; for (size_t i = 24000; i < l.size(); ++i) mean += l[i]; mean /= 24000;
		snprintf(b, sizeof b, "DC Block: abs(x) output mean %.4f V", mean * 10); CHECK(std::fabs(mean) < 0.002, b);
	}

	// Robustness: NaN/inf producing formulas, audio-rate modulation everywhere
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 2, true, L"log(x); 1/x; sqrt(x)*A(0,1e30); tan(x*A(1,50)); x^B(-3,3)");
		m.stream(P_POS, true); m.stream(P_A, true); m.stream(P_B, true);
		std::vector<float> l, r;
		runFn(m, 48000, sine(220, 0.5f), l, r, [&](int i) {
			const int k = i % kBlock; const float t = i / kRate;
			m.buffers[P_POS][k] = 0.5f + 0.5f * (float)std::sin(2 * M_PI * 3 * t);
			m.buffers[P_A][k] = 0.5f * (float)std::sin(2 * M_PI * 1000 * t);
			m.buffers[P_B][k] = 0.6f * (float)std::sin(2 * M_PI * 77 * t);
		});
		snprintf(b, sizeof b, "NaN/inf formulas + audio-rate Position/A/B: finite, |out| <= 20 V (peak %.1f V)", std::max(peak(l), peak(r)) * 10);
		CHECK(finite(l) && finite(r) && peak(l) <= 2.0001f && peak(r) <= 2.0001f, b);
	}

	// Formula errors: entry passes x through, error text in the table blob
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 0, false, L"Broken: sin(x");
		std::vector<float> l, r; runFn(m, 200, [](int, float& a, float& bb) { a = bb = 0.3f; }, l, r);
		CHECK(std::fabs(l.back() - 0.3f) < 1e-6f, "formula error: input passed through unchanged");
		auto& blob = m.host.pinValues[P_DTABLE];
		std::string s(blob.begin(), blob.end());
		CHECK(s.find("Broken") != std::string::npos && s.find("expected ')'") != std::string::npos, "formula error: name and message in the table blob");
	}

	// Display table follows A..D (curves change with the variables), throttled
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 0, 2, true);
		std::vector<float> l, r; runFn(m, 4800, sine(100, 0.3f), l, r);
		const int before = m.host.pinSetCount[P_DTABLE];
		runFn(m, 48000, sine(100, 0.3f), l, r, [&](int i) { m.buffers[P_A][i % kBlock] = 0.5f * (float)std::sin(2 * M_PI * 0.5 * i / kRate); });
		const int sent = m.host.pinSetCount[P_DTABLE] - before, curves = m.host.pinSetCount[P_DCURVE];
		snprintf(b, sizeof b, "A sweeping for 1 s: table re-sent %d x (<= 10/s), curve %d x (<= 30/s)", sent, curves);
		CHECK(sent >= 3 && sent <= 11 && curves <= 35, b);
	}

	// every built-in table runs
	{
		bool ok = true; std::string info;
		for (int t = 0; t < sxws::NumBuiltIns; ++t)
		{
			DspModule m; m.create(kId, kAudioPins); init(m, t, 2, true);
			for (int p : { P_A, P_B, P_C, P_D }) m.setAudio(p, 0.3f);
			m.stream(P_POS, true);
			std::vector<float> l, r;
			runFn(m, 24000, sine(150, 0.5f), l, r, [&](int i) { m.buffers[P_POS][i % kBlock] = (i % 24000) / 24000.f; });
			char c[40]; snprintf(c, 40, " %d:%.1fV", t, peak(l) * 10); info += c;
			if (!finite(l) || peak(l) > 2.0001f || peak(l) < 0.05f || tableCount(m.host) != 8) ok = false;
		}
		snprintf(b, sizeof b, "all 5 built-in tables swept: finite, audible, 8 functions each (peak%s)", info.c_str()); CHECK(ok, b);
	}

	// CPU: heaviest built-in at 8x
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 2, 3, true); m.setAudio(P_POS, 5.f / 7);
		for (int p : { P_A, P_B, P_C, P_D }) m.setAudio(p, 0.3f);
		std::vector<float> l, r;
		auto t0 = std::chrono::steady_clock::now();
		runFn(m, 48000 * 3, sine(150, 0.5f), l, r);
		const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 3;
		snprintf(b, sizeof b, "CPU (debug build -O1): 'Blend 2-5' 8x stereo = %.1f%% of one core", sec * 100); CHECK(sec < 0.5, b);
	}

	// ---------------------------------------------------------- Display: DSP part
	printf("\nSXE Waveshaper Display (DSP)\n");
	// demo state: Saturation table, between Arctan and Rational, A = +2 V, input 4 V
	DspModule demo; demo.create(kId, kAudioPins); init(demo, 0, 2, true);
	demo.setAudio(P_POS, 2.6f / 7); demo.setAudio(P_A, 0.2f); demo.setAudio(P_B, -0.1f);
	{ std::vector<float> l, r; runFn(demo, 9600, sine(220, 0.35f), l, r); }

	DspModule disp;
	CHECK(disp.create(L"Sxynthedit Waveshaper Display", {}), "created via factory, setHost, open");
	disp.setBlob(0, demo.host.pinValues[P_DTABLE]);
	disp.setBlob(1, demo.host.pinValues[P_DCURVE]);
	disp.process();
	CHECK(disp.host.pinValues[2] == demo.host.pinValues[P_DTABLE], "Table blob forwarded to GUI parameter");
	CHECK(disp.host.pinValues[3] == demo.host.pinValues[P_DCURVE], "Curve blob forwarded to GUI parameter");

	// ---------------------------------------------------------- Display: GUI part
	printf("\nSXE Waveshaper Display (GUI)\n");
	GmpiDrawing_API::Context dc;
	TestGfxHost gfx; gfx.factory = &dc.factory;
	TestUiHost ui; ui.gfx = &gfx;

	auto renderGui = [&](const std::vector<uint8_t>* table, const std::vector<uint8_t>* curve, int view, const std::wstring& curveColor,
	                     float w, float h, const char* dumpName) -> bool
	{
		IMpFactory2* factory = nullptr; void* f = nullptr;
		MP_GetFactory(&f); ((IMpUnknown*)f)->queryInterface(MP_IID_FACTORY2, (void**)&factory);
		IMpUnknown* obj = nullptr;
		factory->createInstance2(L"Sxynthedit Waveshaper Display", MP_SUB_TYPE_GUI2, (void**)&obj);
		if (!obj) return false;
		IMpUserInterface2* gui = nullptr;
		obj->queryInterface(MP_IID_GUI_PLUGIN2, (void**)&gui);
		if (!gui) return false;
		gui->setHost(&ui);
		auto setStr = [&](int pin, const std::wstring& s) { gui->setPin(pin, 0, (int)(s.size() * sizeof(wchar_t)), s.data()); };
		int v = view; gui->setPin(3, 0, 4, &v);
		setStr(4, L"FF14181F"); setStr(5, L"FF2C3440"); setStr(6, curveColor); setStr(7, L"30FFB347"); setStr(8, L"60A88A5B");
		setStr(9, L"FFC8D0DC"); setStr(10, L"305BE7FF");
		float lw = 1.5f; gui->setPin(11, 0, 4, &lw);
		bool on = true; gui->setPin(12, 0, 1, &on); gui->setPin(13, 0, 1, &on);
		float fs = 11.f; gui->setPin(14, 0, 4, &fs);
		if (table) gui->setPin(0, 0, (int)table->size(), table->data());
		if (curve) gui->setPin(1, 0, (int)curve->size(), curve->data());
		gui->initialize();
		gmpi_gui_api::IMpGraphics4* gfxIf = nullptr;
		obj->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI4, (void**)&gfxIf);
		if (!gfxIf) return false;
		GmpiDrawing_API::MP1_SIZE avail{ w, h }, desired{};
		gfxIf->measure(avail, &desired);
		gfxIf->arrange({ 0, 0, desired.width, desired.height });
		g_ops.clear(); g_nonFinite = false; g_bounds = { 1e9f, 1e9f, -1e9f, -1e9f };
		gfxIf->OnRender(&dc);
		if (dumpName) dumpOps((dir + "/" + dumpName).c_str(), desired.width, desired.height);
		return true;
	};
	auto inside = [&](float w, float h) { return !g_nonFinite && g_bounds.left >= 0 && g_bounds.top >= 0 && g_bounds.right <= w && g_bounds.bottom <= h; };
	auto texts = [&]() { std::string all; for (auto& op : g_ops) if (op.kind == "text") all += op.text + " | "; return all; };

	auto& T = disp.host.pinValues[2]; auto& C = disp.host.pinValues[3];
	CHECK(renderGui(nullptr, nullptr, 0, L"FFFFB347", 360, 220, "gui_nodata.ops") && !g_ops.empty() && inside(360, 220), "GUI without data: renders, no crash");
	CHECK(renderGui(&T, &C, 0, L"FFFFB347", 360, 220, "gui2d.ops"), "GUI with data, 2D");
	{
		snprintf(b, sizeof b, "  %zu draw calls inside 360x220, text: %s", g_ops.size(), texts().c_str());
		CHECK(inside(360, 220) && g_ops.size() > 10, b);
		const std::string t = texts();
		CHECK(t.find("/ 8") != std::string::npos && t.find("A ") != std::string::npos, "  shows function index and A value");
	}
	CHECK(renderGui(&T, &C, 1, L"#5BE7FF", 360, 220, "gui3d.ops"), "GUI with data, 3D, colour '#5BE7FF'");
	{
		bool found = false; for (auto& op : g_ops) if (op.kind == "stroke" && op.width > 1.f && op.r < 0.2f && op.b > 0.9f) found = true;
		snprintf(b, sizeof b, "  %zu draw calls inside, curve colour pin applied", g_ops.size()); CHECK(found && inside(360, 220), b);
	}
	CHECK(renderGui(&T, &C, 0, L"FFFFB347", 60, 40, nullptr) && inside(60, 40), "tiny 60x40: text hidden, still inside");

	// error display
	{
		DspModule m; m.create(kId, kAudioPins); init(m, 5, 0, false, L"Good: tanh(3x); Broken: sin(x");
		m.setAudio(P_POS, 1.f);
		std::vector<float> l, r; runFn(m, 4800, sine(220, 0.3f), l, r);
		renderGui(&m.host.pinValues[P_DTABLE], &m.host.pinValues[P_DCURVE], 0, L"FFFFB347", 360, 220, "gui_error.ops");
		snprintf(b, sizeof b, "error shown in display: %s", texts().c_str());
		CHECK(texts().find("error:") != std::string::npos, b);
	}

	// stale/foreign blob is ignored
	{
		std::vector<uint8_t> junk(300, 0xAB);
		CHECK(renderGui(&junk, &junk, 1, L"FFFFB347", 360, 220, nullptr) && inside(360, 220), "garbage blobs ignored");
	}

	printf("\nUnimplemented host calls used by the modules: %zu\n", unimplementedCalls().size());
	for (auto& s : unimplementedCalls()) printf("  %s\n", s.c_str());
	printf("Unanswered queryInterface: %zu\n", unknownQueries().size());
	for (auto& s : unknownQueries()) printf("  %s\n", s.c_str());

	printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
	return g_failures ? 1 : 0;
}
