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
#include "WaveshaperDisplayFormat.h"
#include "FormulaEngine.h"
#include <chrono>
#include <complex>

extern "C" int32_t MP_STDCALL MP_GetFactory(void** returnInterface);

using namespace gmpi;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); ++g_failures; } } while (0)

constexpr int kBlock = 100; // deliberately not a multiple of the 32-sample inner chunk
constexpr float kRate = 48000.f;

// ---------------------------------------------------------------- DSP host
struct TestDspHost : Stub_IMpHost
{
	std::map<int, std::vector<uint8_t>> pinValues;
	std::map<int, int> pinSetCount;
	std::map<int, bool> streaming;
	std::map<std::wstring, std::vector<uint8_t>> shared;

	int32_t onQuery(const MpGuid& iid, void** r) override
	{
		if (iid == MP_IID_HOST || iid == MP_IID_UNKNOWN) { *r = static_cast<IMpHost*>(this); addRef(); return MP_OK; }
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
struct Op { std::string kind; float r, g, b, a, width; std::vector<std::vector<GmpiDrawing_API::MP1_POINT>> figs; bool closed = false;
	std::string text; float size = 0; int align = 0, palign = 0; GmpiDrawing_API::MP1_RECT rect{}; };
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
	struct TextFmt : Stub_IMpTextFormat
	{
		float size; int align = 0, palign = 0;
		explicit TextFmt(float s) : size(s) {}
		int32_t onQuery(const gmpi::MpGuid& iid, void** r) override
		{
			if (iid == SE_IID_TEXTFORMAT_MPGUI || iid == gmpi::MP_IID_UNKNOWN) { *r = static_cast<IMpTextFormat*>(this); addRef(); return gmpi::MP_OK; }
			return Stub_IMpTextFormat::onQuery(iid, r);
		}
		int32_t MP_STDCALL SetTextAlignment(MP1_TEXT_ALIGNMENT a) override { align = (int)a; return gmpi::MP_OK; }
		int32_t MP_STDCALL SetParagraphAlignment(MP1_PARAGRAPH_ALIGNMENT a) override { palign = (int)a; return gmpi::MP_OK; }
		int32_t MP_STDCALL SetWordWrapping(MP1_WORD_WRAPPING) override { return gmpi::MP_OK; }
		void MP_STDCALL GetTextExtentU(const char*, int32_t n, MP1_SIZE* s) override { s->width = n * size * 0.55f; s->height = size * 1.2f; }
		int32_t MP_STDCALL GetFontMetrics(MP1_FONT_METRICS* m) override { *m = {}; m->ascent = size * 0.8f; m->descent = size * 0.2f; m->capHeight = size * 0.7f; return gmpi::MP_OK; }
	};
	struct Factory : Stub_IMpFactory
	{
		int32_t MP_STDCALL CreateTextFormat(const char*, void*, MP1_FONT_WEIGHT, MP1_FONT_STYLE, MP1_FONT_STRETCH, float size, void*, IMpTextFormat** tf) override { *tf = new TextFmt(size); return gmpi::MP_OK; }
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
		void MP_STDCALL DrawTextU(const char* s, int32_t n, const IMpTextFormat* tf, const MP1_RECT* r, const IMpBrush* brush, int32_t) override
		{
			auto* f = (TextFmt*)static_cast<const IMpTextFormat*>(tf);
			Op op = opFor("text", brush, 0); op.text.assign(s, n); op.size = f->size; op.align = f->align; op.palign = f->palign; op.rect = *r;
			track({ r->left, r->top }); track({ r->right, r->bottom });
			g_ops.push_back(op);
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
		if (op.kind == "text")
		{
			f << "text " << op.r << " " << op.g << " " << op.b << " " << op.a << " " << op.size << " " << op.align << " " << op.palign << " "
			  << op.rect.left << " " << op.rect.top << " " << op.rect.right << " " << op.rect.bottom << "\n" << op.text << "\n";
			continue;
		}
		f << op.kind << " " << op.r << " " << op.g << " " << op.b << " " << op.a << " " << op.width << " " << op.figs.size() << "\n";
		for (auto& fig : op.figs)
		{
			f << fig.size();
			for (auto& p : fig) f << " " << p.x << " " << p.y;
			f << "\n";
		}
	}
}

