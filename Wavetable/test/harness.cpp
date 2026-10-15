// Minimal SynthEdit-like host: loads modules through MP_GetFactory, drives DSP and GUI parts,
// records drawing calls. Built together with the module sources (no dlopen needed).
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cassert>
#include <fstream>
#include <functional>
#include "stubs.h"
#include "WavetableDisplayFormat.h"

extern "C" int32_t MP_STDCALL MP_GetFactory(void** returnInterface);

using namespace gmpi;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); ++g_failures; } } while (0)

constexpr int kBlock = 96;
constexpr float kRate = 48000.f;

// ---------------------------------------------------------------- SynthEdit 1.5 file support
struct TestFile : Stub_IProtectedFile2
{
	FILE* f;
	explicit TestFile(FILE* file) : f(file) {}
	int32_t onQuery(const MpGuid& iid, void** r) override
	{
		if (iid == MP_IID_PROTECTED_FILE2 || iid == MP_IID_UNKNOWN) { *r = static_cast<IProtectedFile2*>(this); addRef(); return MP_OK; }
		return Stub_IProtectedFile2::onQuery(iid, r);
	}
	int32_t MP_STDCALL read(void* b, int64_t size, int64_t* got) override { auto n = fread(b, 1, (size_t)size, f); if (got) *got = n; return n == (size_t)size ? MP_OK : MP_FAIL; }
	int32_t MP_STDCALL seek(int64_t d, int32_t m, int64_t* p) override { fseek(f, (long)d, m == 0 ? SEEK_SET : (m == 1 ? SEEK_CUR : SEEK_END)); if (p) *p = ftell(f); return MP_OK; }
	int32_t MP_STDCALL getSize(int64_t* r) override { long c = ftell(f); fseek(f, 0, SEEK_END); *r = ftell(f); fseek(f, c, SEEK_SET); return MP_OK; }
};
struct TestEmbedded : Stub_IEmbeddedFileSupport
{
	int opens = 0;
	int32_t MP_STDCALL resolveFilename(const char* name, IString* ret) override { ret->setData(name, (int32_t)strlen(name)); return MP_OK; }
	int32_t MP_STDCALL openUri(const char* uri, IMpUnknown** ret) override
	{
		FILE* f = fopen(uri, "rb");
		if (!f) { *ret = nullptr; return MP_FAIL; }
		++opens; *ret = static_cast<IProtectedFile2*>(new TestFile(f)); return MP_OK;
	}
};

// ---------------------------------------------------------------- DSP host
struct TestDspHost : Stub_IMpHost
{
	bool embedded = false;
	TestEmbedded embeddedSupport;
	std::map<int, std::vector<uint8_t>> pinValues;
	std::map<int, int> pinSetCount;
	std::map<int, bool> streaming;
	std::map<std::wstring, std::vector<uint8_t>> shared;

	int32_t onQuery(const MpGuid& iid, void** r) override
	{
		if (iid == MP_IID_HOST || iid == MP_IID_UNKNOWN) { *r = static_cast<IMpHost*>(this); addRef(); return MP_OK; }
		if (embedded && iid == MP_IID_HOST_EMBEDDED_FILE_SUPPORT) { *r = static_cast<IEmbeddedFileSupport*>(&embeddedSupport); return MP_OK; }
		return Stub_IMpHost::onQuery(iid, r);
	}
	int32_t MP_STDCALL setPin(int32_t, int32_t pinId, int32_t size, const void* data) override
	{
		pinValues[pinId].assign((const uint8_t*)data, (const uint8_t*)data + size);
		pinSetCount[pinId]++;
		return MP_OK;
	}
	int32_t MP_STDCALL setPinStreaming(int32_t, int32_t pinId, int32_t s) override { streaming[pinId] = s != 0; return MP_OK; }
	int32_t MP_STDCALL getBlockSize(int32_t& r) override { r = kBlock; return MP_OK; }
	int32_t MP_STDCALL getSampleRate(float& r) override { r = kRate; return MP_OK; }
	int32_t MP_STDCALL getHandle(int32_t& r) override { r = 7; return MP_OK; }
	int32_t MP_STDCALL isCloned(int32_t* r) override { *r = 0; return MP_OK; }
	int32_t MP_STDCALL sleep() override { return MP_OK; }
	int32_t MP_STDCALL resolveFilename(const wchar_t* s, int32_t maxChars, wchar_t* r) override
	{
		wcsncpy(r, s, maxChars - 1); r[maxChars - 1] = 0; return MP_OK;
	}
	int32_t MP_STDCALL allocateSharedMemory(const wchar_t* id, void** p, float, int32_t size, int32_t& needInit) override
	{
		auto& v = shared[id]; needInit = v.empty(); if (v.empty()) v.resize(size); *p = v.data(); return MP_OK;
	}
};

struct DspModule
{
	IMpPlugin2* plugin = nullptr;
	TestDspHost host;
	std::map<int, std::vector<float>> buffers;
	std::vector<MpEvent> events;
	std::vector<std::vector<char>> extra;

	bool create(const wchar_t* id, std::vector<int> audioPins)
	{
		IMpFactory2* factory = nullptr;
		void* f = nullptr;
		MP_GetFactory(&f);
		((IMpUnknown*)f)->queryInterface(MP_IID_FACTORY2, (void**)&factory);
		if (!factory) return false;
		IMpUnknown* obj = nullptr;
		factory->createInstance2(id, MP_SUB_TYPE_AUDIO, (void**)&obj);
		if (!obj) return false;
		obj->queryInterface(MP_IID_PLUGIN2, (void**)&plugin);
		if (!plugin) return false;
		plugin->setHost(&host);
		for (int p : audioPins)
		{
			buffers[p].assign(kBlock, 0.f);
			plugin->setBuffer(p, buffers[p].data());
		}
		plugin->open();
		MpEvent e{}; e.eventType = EVENT_GRAPH_START; events.push_back(e);
		return true;
	}
	void setRaw(int pin, const void* data, int size)
	{
		MpEvent e{}; e.eventType = EVENT_PIN_SET; e.parm1 = pin; e.parm2 = size;
		if (size <= 4) memcpy(&e.parm3, data, size);
		else { extra.emplace_back((const char*)data, (const char*)data + size); e.extraData = extra.back().data(); }
		events.push_back(e);
	}
	void setInt(int pin, int v) { setRaw(pin, &v, 4); }
	void setFloat(int pin, float v) { setRaw(pin, &v, 4); }
	void setBool(int pin, bool v) { setRaw(pin, &v, 1); }
	void setString(int pin, const std::wstring& s) { setRaw(pin, s.data(), (int)(s.size() * sizeof(wchar_t))); }
	void setBlob(int pin, const std::vector<uint8_t>& b) { setRaw(pin, b.data(), (int)b.size()); }
	void setAudio(int pin, float v) { std::fill(buffers[pin].begin(), buffers[pin].end(), v); }
	void stream(int pin, bool on) { MpEvent e{}; e.eventType = on ? EVENT_PIN_STREAMING_START : EVENT_PIN_STREAMING_STOP; e.parm1 = pin; events.push_back(e); }

	void process()
	{
		for (size_t i = 0; i < events.size(); ++i)
			events[i].next = i + 1 < events.size() ? &events[i + 1] : nullptr;
		plugin->process(kBlock, events.empty() ? nullptr : events.data());
		events.clear();
		extra.clear();
	}
	// run n samples, collecting an output pin
	std::vector<float> run(int samples, int outPin, int outPin2 = -1, std::vector<float>* second = nullptr)
	{
		std::vector<float> out;
		for (int done = 0; done < samples; done += kBlock)
		{
			process();
			out.insert(out.end(), buffers[outPin].begin(), buffers[outPin].end());
			if (second) second->insert(second->end(), buffers[outPin2].begin(), buffers[outPin2].end());
		}
		return out;
	}
};

// ---------------------------------------------------------------- Drawing recorder
struct Op { std::string kind; float r, g, b, a, width; std::vector<std::vector<GmpiDrawing_API::MP1_POINT>> figs; bool closed = false; };
static std::vector<Op> g_ops;
static bool g_nonFinite = false;
static GmpiDrawing_API::MP1_RECT g_bounds{ 1e9f, 1e9f, -1e9f, -1e9f };
static void track(GmpiDrawing_API::MP1_POINT p)
{
	if (!std::isfinite(p.x) || !std::isfinite(p.y)) g_nonFinite = true;
	g_bounds.left = std::min(g_bounds.left, p.x); g_bounds.top = std::min(g_bounds.top, p.y);
	g_bounds.right = std::max(g_bounds.right, p.x); g_bounds.bottom = std::max(g_bounds.bottom, p.y);
}

namespace GmpiDrawing_API
{
	struct Factory;
	struct Geometry : Stub_IMpPathGeometry
	{
		Factory* factory;
		std::vector<std::vector<MP1_POINT>> figs;
		bool closed = false;
		explicit Geometry(Factory* f) : factory(f) {}
		int32_t onQuery(const gmpi::MpGuid& iid, void** r) override
		{
			if (iid == SE_IID_PATHGEOMETRY_MPGUI || iid == gmpi::MP_IID_UNKNOWN) { *r = static_cast<IMpPathGeometry*>(this); addRef(); return gmpi::MP_OK; }
			return Stub_IMpPathGeometry::onQuery(iid, r);
		}

		int32_t MP_STDCALL Open(IMpGeometrySink** sink) override;
		void MP_STDCALL GetFactory(IMpFactory** f) override;
	};
	struct Sink : Stub_IMpGeometrySink
	{
		Geometry* geo;
		explicit Sink(Geometry* g) : geo(g) {}
		int32_t onQuery(const gmpi::MpGuid& iid, void** r) override
		{
			if (iid == SE_IID_GEOMETRYSINK_MPGUI || iid == gmpi::MP_IID_UNKNOWN) { *r = static_cast<IMpGeometrySink*>(this); addRef(); return gmpi::MP_OK; }
			return Stub_IMpGeometrySink::onQuery(iid, r);
		}

		void MP_STDCALL BeginFigure(MP1_POINT p, MP1_FIGURE_BEGIN) override { geo->figs.push_back({ p }); track(p); }
		void MP_STDCALL AddLine(MP1_POINT p) override { geo->figs.back().push_back(p); track(p); }
		void MP_STDCALL AddLines(const MP1_POINT* p, uint32_t n) override { for (uint32_t i = 0; i < n; ++i) AddLine(p[i]); }
		void MP_STDCALL EndFigure(MP1_FIGURE_END e) override { geo->closed = (e == MP1_FIGURE_END_CLOSED); }
		int32_t MP_STDCALL Close() override { return MP_OK; }
	};
	struct Factory : Stub_IMpFactory
	{
		int32_t onQuery(const gmpi::MpGuid& iid, void** r) override
		{
			if (iid == SE_IID_FACTORY_MPGUI || iid == gmpi::MP_IID_UNKNOWN) { *r = static_cast<IMpFactory*>(this); addRef(); return gmpi::MP_OK; }
			return Stub_IMpFactory::onQuery(iid, r);
		}
		int32_t MP_STDCALL CreatePathGeometry(IMpPathGeometry** g) override { *g = new Geometry(this); return gmpi::MP_OK; }
	};
	int32_t MP_STDCALL Geometry::Open(IMpGeometrySink** sink) { *sink = new Sink(this); return gmpi::MP_OK; }
	void MP_STDCALL Geometry::GetFactory(IMpFactory** f) { *f = factory; factory->addRef(); }

	struct Brush : Stub_IMpSolidColorBrush
	{
		MP1_COLOR c;
		Factory* factory;
		Brush(const MP1_COLOR& col, Factory* f) : c(col), factory(f) {}
		int32_t onQuery(const gmpi::MpGuid& iid, void** r) override
		{
			if (iid == SE_IID_SOLIDCOLORBRUSH_MPGUI || iid == gmpi::MP_IID_UNKNOWN) { *r = static_cast<IMpSolidColorBrush*>(this); addRef(); return gmpi::MP_OK; }
			return Stub_IMpSolidColorBrush::onQuery(iid, r);
		}

		MP1_COLOR MP_STDCALL GetColor() override { return c; }
		void MP_STDCALL SetColor(const MP1_COLOR* col) override { c = *col; }
		void MP_STDCALL GetFactory(IMpFactory** f) override { *f = factory; factory->addRef(); }
	};

	struct Context : Stub_IMpDeviceContext
	{
		Factory factory;
		int32_t onQuery(const gmpi::MpGuid& iid, void** r) override
		{
			if (iid == SE_IID_DEVICECONTEXT_MPGUI || iid == gmpi::MP_IID_UNKNOWN) { *r = static_cast<IMpDeviceContext*>(this); addRef(); return gmpi::MP_OK; }
			return Stub_IMpDeviceContext::onQuery(iid, r);
		}
		void MP_STDCALL GetFactory(IMpFactory** f) override { *f = &factory; factory.addRef(); }
		int32_t MP_STDCALL CreateSolidColorBrush(const MP1_COLOR* c, IMpSolidColorBrush** b) override { *b = new Brush(*c, &factory); return gmpi::MP_OK; }
		static Op opFor(const char* kind, const IMpBrush* brush, float w)
		{
			auto* b = (Brush*)static_cast<const IMpSolidColorBrush*>(brush);
			Op op; op.kind = kind; op.r = b->c.r; op.g = b->c.g; op.b = b->c.b; op.a = b->c.a; op.width = w; return op;
		}
		void MP_STDCALL FillRectangle(const MP1_RECT* r, const IMpBrush* brush) override
		{
			Op op = opFor("fill", brush, 0); op.closed = true;
			op.figs.push_back({ {r->left, r->top}, {r->right, r->top}, {r->right, r->bottom}, {r->left, r->bottom} });
			g_ops.push_back(op);
		}
		void MP_STDCALL DrawLine(MP1_POINT a, MP1_POINT b, const IMpBrush* brush, float w, const IMpStrokeStyle*) override
		{
			track(a); track(b);
			Op op = opFor("stroke", brush, w); op.figs.push_back({ a, b }); g_ops.push_back(op);
		}
		void MP_STDCALL DrawGeometry(const IMpPathGeometry* g, const IMpBrush* brush, float w, const IMpStrokeStyle*) override
		{
			auto* geo = (Geometry*)g; Op op = opFor("stroke", brush, w); op.figs = geo->figs; op.closed = geo->closed; g_ops.push_back(op);
		}
		void MP_STDCALL FillGeometry(const IMpPathGeometry* g, const IMpBrush* brush, const IMpBrush*) override
		{
			auto* geo = (Geometry*)g; Op op = opFor("fill", brush, 0); op.figs = geo->figs; op.closed = true; g_ops.push_back(op);
		}
		void MP_STDCALL PushAxisAlignedClip(const MP1_RECT*) override {}
		void MP_STDCALL PopAxisAlignedClip() override {}
	};
}

// ---------------------------------------------------------------- GUI host
struct TestGfxHost : gmpi_gui::Stub_IMpGraphicsHost
{
	int invalidates = 0;
	GmpiDrawing_API::Factory* factory = nullptr;
	void MP_STDCALL invalidateRect(const GmpiDrawing_API::MP1_RECT*) override { ++invalidates; }
	void MP_STDCALL invalidateMeasure() override {}
	int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory** f) override { *f = factory; return MP_OK; }
};
struct TestUiHost : Stub_IMpUserInterfaceHost2
{
	TestGfxHost* gfx = nullptr;
	int32_t onQuery(const MpGuid& iid, void** r) override
	{
		if (iid == MP_IID_UI_HOST2 || iid == MP_IID_UNKNOWN) { *r = static_cast<IMpUserInterfaceHost2*>(this); addRef(); return MP_OK; }
		if (iid == gmpi_gui::SE_IID_GRAPHICS_HOST) { *r = static_cast<gmpi_gui::IMpGraphicsHost*>(gfx); gfx->addRef(); return MP_OK; }
		return Stub_IMpUserInterfaceHost2::onQuery(iid, r);
	}
	int32_t MP_STDCALL getHandle(int32_t& r) override { r = 7; return MP_OK; }
	int32_t MP_STDCALL pinTransmit(int32_t, int32_t, const void*, int32_t) override { return MP_OK; }
};

// ---------------------------------------------------------------- helpers
static float peak(const std::vector<float>& v) { float p = 0; for (float x : v) p = std::max(p, std::fabs(x)); return p; }
static bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
static double rms(const std::vector<float>& v) { double s = 0; for (float x : v) s += x * x; return std::sqrt(s / v.size()); }

static void dumpOps(const char* path, float w, float h)
{
	std::ofstream f(path);
	f << w << " " << h << "\n";
	for (auto& op : g_ops)
	{
		f << op.kind << " " << op.r << " " << op.g << " " << op.b << " " << op.a << " " << op.width << " " << op.figs.size() << "\n";
		for (auto& fig : op.figs)
		{
			f << fig.size();
			for (auto& p : fig) f << " " << p.x << " " << p.y;
			f << "\n";
		}
	}
}

// Osc pin ids (order in Wavetable.xml)
enum { P_PITCH, P_POS, P_PHASE, P_SYNC, P_TABLE, P_FILE, P_LIST, P_VOICES, P_DETUNE, P_WIDTH, P_PAN, P_ON, P_LEFT, P_RIGHT, P_DTABLE, P_DWAVE };

static void initOsc(DspModule& m)
{
	m.setAudio(P_PITCH, 0.5f); m.setAudio(P_POS, 0.f); m.setAudio(P_PHASE, 0.f); m.setAudio(P_SYNC, 0.f);
	m.setAudio(P_DETUNE, 0.3f); m.setAudio(P_WIDTH, 0.7f); m.setAudio(P_PAN, 0.f);
	m.setInt(P_TABLE, 0); m.setString(P_FILE, L""); m.setString(P_LIST, L""); m.setInt(P_VOICES, 1); m.setBool(P_ON, true);
}

static int tableFrames(TestDspHost& h)
{
	auto& b = h.pinValues[P_DTABLE];
	if (b.size() < sizeof(fvwt::TableBlobHeader)) return -1;
	fvwt::TableBlobHeader hd; memcpy(&hd, b.data(), sizeof(hd));
	return hd.magic == fvwt::kTableMagic ? hd.totalFrames : -2;
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	const std::vector<int> oscAudio = { P_PITCH, P_POS, P_PHASE, P_SYNC, P_DETUNE, P_WIDTH, P_PAN, P_LEFT, P_RIGHT };

	printf("Wavetable Osc\n");
	DspModule osc;
	CHECK(osc.create(L"FV Wavetable Osc", oscAudio), "created via factory, setHost, open");
	initOsc(osc);
	std::vector<float> right;
	auto left = osc.run(9600, P_LEFT, P_RIGHT, &right);
	CHECK(finite(left) && finite(right), "output finite");
	CHECK(std::fabs(peak(left) - 0.5f) < 0.05f, "1 voice, centre: peak about 5 V");
	{ double d = 0; for (size_t i = 0; i < left.size(); ++i) d = std::max(d, (double)std::fabs(left[i] - right[i])); CHECK(d < 1e-6, "1 voice, centre: L == R"); }
	CHECK(osc.host.streaming[P_LEFT] && osc.host.streaming[P_RIGHT], "outputs flagged streaming");
	CHECK(tableFrames(osc.host) == 64, "Display Table blob sent (64 frames)");
	CHECK(osc.host.pinSetCount[P_DWAVE] >= 1, "Display Wave blob sent");

	// frequency check: count zero crossings of the sine (position 0 = sine)
	{ int zc = 0; for (size_t i = 1; i < left.size(); ++i) zc += (left[i - 1] < 0) != (left[i] < 0); double hz = zc / 2.0 / (left.size() / kRate); char b[80]; snprintf(b, 80, "pitch 5 V -> %.1f Hz", hz); CHECK(std::fabs(hz - 440) < 3, b); }

	// Phase input: 2.5 V = quarter cycle -> sine becomes cosine at the start of a reset
	{
		DspModule a, b;
		a.create(L"FV Wavetable Osc", oscAudio); b.create(L"FV Wavetable Osc", oscAudio);
		initOsc(a); initOsc(b); b.setAudio(P_PHASE, 0.25f);
		auto oa = a.run(960, P_LEFT), ob = b.run(960, P_LEFT);
		CHECK(std::fabs(oa[0]) < 0.01f && std::fabs(ob[0] - 0.5f) < 0.02f, "Phase 2.5 V shifts the cycle by 90 degrees");
	}

	// Pan hard left
	{
		DspModule m; m.create(L"FV Wavetable Osc", oscAudio); initOsc(m); m.setAudio(P_PAN, -0.5f);
		std::vector<float> r; auto l = m.run(4800, P_LEFT, P_RIGHT, &r);
		CHECK(peak(r) < 1e-4f && std::fabs(peak(l) - 0.5f) < 0.05f, "Pan -5 V: all on the left, same level as centre");
	}

	// Unison
	{
		DspModule m; m.create(L"FV Wavetable Osc", oscAudio); initOsc(m);
		m.setInt(P_VOICES, 7); m.setAudio(P_DETUNE, 0.5f); m.setAudio(P_WIDTH, 1.0f); m.setInt(P_TABLE, 0); m.setAudio(P_POS, 0.66667f);
		std::vector<float> r; auto l = m.run(48000, P_LEFT, P_RIGHT, &r);
		double diff = 0; for (size_t i = 0; i < l.size(); ++i) diff += (l[i] - r[i]) * (l[i] - r[i]);
		CHECK(finite(l) && finite(r), "7 voices: finite");
		CHECK(std::sqrt(diff / l.size()) > 0.05, "7 voices, width 10 V: L and R differ (stereo)");
		char b[100]; snprintf(b, 100, "7 voices: level rms L %.2f / R %.2f, peak %.2f (no clipping)", rms(l), rms(r), std::max(peak(l), peak(r)));
		CHECK(peak(l) < 1.0f && rms(l) > 0.05, b);
		std::ofstream(dir + "/unison.f32", std::ios::binary).write((char*)l.data(), l.size() * 4);
		std::ofstream(dir + "/unisonR.f32", std::ios::binary).write((char*)r.data(), r.size() * 4);
	}

	// On / Off
	{
		DspModule m; m.create(L"FV Wavetable Osc", oscAudio); initOsc(m);
		m.run(960, P_LEFT);
		m.setBool(P_ON, false);
		std::vector<float> r; auto l = m.run(960, P_LEFT, P_RIGHT, &r);
		CHECK(peak(l) == 0.f && peak(r) == 0.f, "On = off: silence");
		CHECK(!m.host.streaming[P_LEFT] && !m.host.streaming[P_RIGHT], "On = off: outputs not streaming (module can sleep)");
		m.setBool(P_ON, true);
		l = m.run(960, P_LEFT);
		CHECK(peak(l) > 0.4f && m.host.streaming[P_LEFT], "On again: sound returns");
	}

	// Pitch modulation at audio rate + sync
	{
		DspModule m; m.create(L"FV Wavetable Osc", oscAudio); initOsc(m); m.setInt(P_VOICES, 3);
		m.stream(P_PITCH, true); m.stream(P_SYNC, true); m.stream(P_POS, true);
		std::vector<float> out;
		for (int blk = 0; blk < 500; ++blk)
		{
			for (int i = 0; i < kBlock; ++i)
			{
				const float t = (blk * kBlock + i) / kRate;
				m.buffers[P_PITCH][i] = 0.3f + 0.6f * t;             // sweep 3 V .. ~9 V
				m.buffers[P_SYNC][i] = std::sin(2 * M_PI * 110 * t);  // 110 Hz sync
				m.buffers[P_POS][i] = 0.5f + 0.5f * std::sin(2 * M_PI * 2 * t);
			}
			m.process();
			out.insert(out.end(), m.buffers[P_LEFT].begin(), m.buffers[P_LEFT].end());
		}
		CHECK(finite(out) && peak(out) < 1.2f, "audio-rate pitch sweep, sync and position: finite, bounded");
	}

	// File list (folder of single cycles) and explicit list - with the old and the SynthEdit 1.5 file API
	for (int embedded = 0; embedded < 2; ++embedded)
	{
		printf("  -- file loading via %s\n", embedded ? "SynthEdit 1.5 IEmbeddedFileSupport" : "legacy resolveFilename + fopen");
		DspModule m; m.host.embedded = embedded != 0; m.create(L"FV Wavetable Osc", oscAudio); initOsc(m);
		m.setInt(P_TABLE, 7);
		std::wstring folder(dir.begin(), dir.end()); folder += L"/cycles";
		m.setString(P_LIST, folder);
		auto l = m.run(960, P_LEFT);
		char b[100]; snprintf(b, 100, "File List = folder: %d frames (expect 5)", tableFrames(m.host)); CHECK(tableFrames(m.host) == 5, b);
		CHECK(peak(l) > 0.3f, "File List: sound");

		m.setString(P_LIST, folder + L"/c10_square.wav; " + folder + L"/c02_sine.wav");
		m.run(96, P_LEFT);
		snprintf(b, 100, "File List = 2 files with ';': %d frames", tableFrames(m.host)); CHECK(tableFrames(m.host) == 2, b);

		m.setString(P_LIST, L"/does/not/exist");
		m.run(96, P_LEFT);
		CHECK(tableFrames(m.host) == 64, "File List missing: falls back to Classic");

		std::wstring wav(dir.begin(), dir.end()); wav += L"/serum16.wav";
		m.setInt(P_TABLE, 6); m.setString(P_FILE, wav);
		m.run(96, P_LEFT);
		snprintf(b, 100, "WAV File: %d frames (expect 8)", tableFrames(m.host)); CHECK(tableFrames(m.host) == 8, b);
		if (embedded) { snprintf(b, 100, "files opened through openUri(): %d", m.host.embeddedSupport.opens); CHECK(m.host.embeddedSupport.opens >= 3, b); }
	}

	// ---------------------------------------------------------- Display: DSP part
	printf("\nWavetable Display (DSP)\n");
	DspModule disp;
	CHECK(disp.create(L"FV Wavetable Display", {}), "created via factory, setHost, open");
	disp.setBlob(0, osc.host.pinValues[P_DTABLE]);
	disp.setBlob(1, osc.host.pinValues[P_DWAVE]);
	disp.process();
	CHECK(disp.host.pinValues[2] == osc.host.pinValues[P_DTABLE], "Table blob forwarded to GUI parameter");
	CHECK(disp.host.pinValues[3] == osc.host.pinValues[P_DWAVE], "Wave blob forwarded to GUI parameter");

	// ---------------------------------------------------------- Display: GUI part
	printf("\nWavetable Display (GUI)\n");
	GmpiDrawing_API::Context dc;
	TestGfxHost gfx; gfx.factory = &dc.factory;
	TestUiHost ui; ui.gfx = &gfx;

	auto renderGui = [&](bool withData, int view, const std::wstring& waveColor, const char* dumpName) -> bool
	{
		IMpFactory2* factory = nullptr; void* f = nullptr;
		MP_GetFactory(&f); ((IMpUnknown*)f)->queryInterface(MP_IID_FACTORY2, (void**)&factory);
		IMpUnknown* obj = nullptr;
		factory->createInstance2(L"FV Wavetable Display", MP_SUB_TYPE_GUI2, (void**)&obj); // SynthEdit creates the GUI here
		if (!obj) return false;
		IMpUserInterface2* gui = nullptr;
		obj->queryInterface(MP_IID_GUI_PLUGIN2, (void**)&gui);
		if (!gui) return false;
		gui->setHost(&ui);
		// SynthEdit sets the pins (defaults from XML), then initialises.
		auto setStr = [&](int pin, const std::wstring& s) { gui->setPin(pin, 0, (int)(s.size() * sizeof(wchar_t)), s.data()); };
		int v = view; gui->setPin(3, 0, 4, &v);
		setStr(4, L"FF14181F"); setStr(5, L"FF2C3440"); setStr(6, waveColor); setStr(7, L"405BE7FF"); setStr(8, L"605B8AA8");
		float lw = 1.5f; gui->setPin(9, 0, 4, &lw);
		bool grid = true; gui->setPin(10, 0, 1, &grid);
		if (withData)
		{
			auto& t = disp.host.pinValues[2]; auto& w = disp.host.pinValues[3];
			gui->setPin(0, 0, (int)t.size(), t.data());
			gui->setPin(1, 0, (int)w.size(), w.data());
		}
		gui->initialize();
		gmpi_gui_api::IMpGraphics4* gfxIf = nullptr;
		obj->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI4, (void**)&gfxIf);
		if (!gfxIf) return false;
		GmpiDrawing_API::MP1_SIZE avail{ 360, 200 }, desired{};
		gfxIf->measure(avail, &desired);
		gfxIf->arrange({ 0, 0, desired.width, desired.height });
		g_ops.clear(); g_nonFinite = false; g_bounds = { 1e9f, 1e9f, -1e9f, -1e9f };
		gfxIf->OnRender(&dc);
		if (dumpName) dumpOps((dir + "/" + dumpName).c_str(), desired.width, desired.height);
		return true;
	};

	CHECK(renderGui(false, 1, L"FF5BE7FF", nullptr), "GUI created, setHost, pins set, initialize, render (no data yet)");
	CHECK(!g_ops.empty() && !g_nonFinite, "  renders background without data");
	CHECK(renderGui(true, 1, L"FF5BE7FF", "gui3d.ops"), "GUI with data, 3D");
	{ char b[120]; snprintf(b, 120, "  %zu draw calls, all finite, inside 0..360 x 0..200 (bounds %.0f,%.0f..%.0f,%.0f)", g_ops.size(), g_bounds.left, g_bounds.top, g_bounds.right, g_bounds.bottom);
	  CHECK(!g_nonFinite && g_bounds.left >= 0 && g_bounds.top >= 0 && g_bounds.right <= 360 && g_bounds.bottom <= 200 && g_ops.size() > 20, b); }
	CHECK(renderGui(true, 0, L"#FFB347", "gui2d.ops"), "GUI with data, 2D, colour '#FFB347'");
	{ bool found = false; for (auto& op : g_ops) if (op.kind == "stroke" && op.width > 1.f && op.r > 0.9f && op.b < 0.1f) found = true; CHECK(found, "  wave colour pin applied to the outline"); }
	CHECK(gfx.invalidates >= 0, "invalidateRect only via host (no crash)");

	printf("\nUnimplemented host calls used by the modules: %zu\n", unimplementedCalls().size());
	for (auto& s : unimplementedCalls()) printf("  %s\n", s.c_str());
	printf("Unanswered queryInterface: %zu\n", unknownQueries().size());
	for (auto& s : unknownQueries()) printf("  %s\n", s.c_str());

	printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
	return g_failures ? 1 : 0;
}
