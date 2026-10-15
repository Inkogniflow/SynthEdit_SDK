#include <string.h>
#include <wctype.h>
#include <algorithm>
#include "./WavetableDisplayGui.h"
#include "./WavetableDisplayFormat.h"

using namespace gmpi;
using namespace gmpi_gui;
using namespace GmpiDrawing;
using namespace fvwt;

GMPI_REGISTER_GUI(MP_SUB_TYPE_GUI2, WavetableDisplayGui, L"FV Wavetable Display");

namespace
{
	// Default colours (AARRGGBB), also used when a colour pin is empty.
	constexpr uint32_t kDefaultBackground = 0xFF14181F;
	constexpr uint32_t kDefaultGrid       = 0xFF2C3440;
	constexpr uint32_t kDefaultWave       = 0xFF5BE7FF;
	constexpr uint32_t kDefaultFill       = 0x405BE7FF;
	constexpr uint32_t kDefaultFrames     = 0x605B8AA8;

	// Accepts "AARRGGBB", "RRGGBB", with or without '#'.
	Color parseColor(const std::wstring& text, uint32_t fallback)
	{
		std::wstring hex;
		for (wchar_t c : text)
		{
			if (iswxdigit(c))
				hex += c;
		}

		if (hex.size() != 6 && hex.size() != 8)
			return Color::FromArgb(fallback);

		return Color::FromHexString(hex);
	}

	Color withAlpha(Color c, float alphaScale)
	{
		c.a *= alphaScale;
		return c;
	}
}

WavetableDisplayGui::WavetableDisplayGui()
{
	// Pin ids match the <GUI> pins in Wavetable.xml.
	initializePin(0, pinTable, static_cast<MpGuiBaseMemberIndexedPtr2>(&WavetableDisplayGui::onDataChanged));
	initializePin(1, pinWave,  static_cast<MpGuiBaseMemberIndexedPtr2>(&WavetableDisplayGui::onDataChanged));
	initializePin(2, pinGate,  static_cast<MpGuiBaseMemberIndexedPtr2>(&WavetableDisplayGui::onGateChanged));
	initializePin(3, pinView,            static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onRedraw));
	initializePin(4, pinBackgroundColor, static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onColorsChanged));
	initializePin(5, pinGridColor,       static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onColorsChanged));
	initializePin(6, pinWaveColor,       static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onColorsChanged));
	initializePin(7, pinFillColor,       static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onColorsChanged));
	initializePin(8, pinFramesColor,     static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onColorsChanged));
	initializePin(9, pinLineWidth,       static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onRedraw));
	initializePin(10, pinShowGrid,       static_cast<MpGuiBaseMemberPtr2>(&WavetableDisplayGui::onRedraw));

	// Note: no host calls here. The graphics host is only connected after construction (setHost),
	// so invalidateRect() in the constructor would crash SynthEdit when the module is inserted.
}

void WavetableDisplayGui::redraw()
{
	if (getGuiHost())
		invalidateRect();
}

void WavetableDisplayGui::onDataChanged(int voice)
{
	// Follow the most recently updated voice, but don't steal focus from a held note.
	const bool currentHeld = pinGate.getValue(newestVoice_) > 0.f;
	const bool thisHeld = pinGate.getValue(voice) > 0.f;
	if (!currentHeld || thisHeld)
		newestVoice_ = voice;

	redraw();
}

void WavetableDisplayGui::onGateChanged(int voice)
{
	if (pinGate.getValue(voice) > 0.f)
	{
		newestVoice_ = voice;
		redraw();
	}
}

void WavetableDisplayGui::onColorsChanged()
{
	redraw(); // colours are parsed in OnRender()
}

void WavetableDisplayGui::onRedraw()
{
	redraw();
}

bool WavetableDisplayGui::getTable(TableView& out)
{
	auto tryVoice = [&](int v) -> bool
	{
		const int size = pinTable.rawSize(v);
		if (size < (int)sizeof(TableBlobHeader))
			return false;

		TableBlobHeader h;
		memcpy(&h, pinTable.rawData(v), sizeof(h));
		if (h.magic != kTableMagic || h.version != kFormatVersion || h.frames < 1 || h.points < 2)
			return false;
		if (size < (int)(sizeof(h) + (size_t)h.frames * h.points))
			return false;

		out.frames = h.frames;
		out.points = h.points;
		out.totalFrames = h.totalFrames;
		out.data = (const int8_t*)((const uint8_t*)pinTable.rawData(v) + sizeof(h));
		return true;
	};

	if (tryVoice(newestVoice_))
		return true;

	for (int v = 0; v < pinTable.size(); ++v)
	{
		if (v != newestVoice_ && tryVoice(v))
			return true;
	}
	return false;
}

bool WavetableDisplayGui::getWave(WaveView& out)
{
	auto tryVoice = [&](int v) -> bool
	{
		const int size = pinWave.rawSize(v);
		if (size < (int)sizeof(WaveBlobHeader))
			return false;

		WaveBlobHeader h;
		memcpy(&h, pinWave.rawData(v), sizeof(h));
		if (h.magic != kWaveMagic || h.version != kFormatVersion || h.points < 2)
			return false;
		if (size < (int)(sizeof(h) + (size_t)h.points * sizeof(int16_t)))
			return false;

		out.points = h.points;
		out.position = (std::max)(0.f, (std::min)(1.f, h.position));
		out.data = (const int16_t*)((const uint8_t*)pinWave.rawData(v) + sizeof(h));
		return true;
	};

	if (tryVoice(newestVoice_))
		return true;

	for (int v = 0; v < pinWave.size(); ++v)
	{
		if (v != newestVoice_ && tryVoice(v))
			return true;
	}
	return false;
}

int32_t WavetableDisplayGui::measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize)
{
	const float minSize = 30.f;
	returnDesiredSize->width = (std::max)(minSize, availableSize.width);
	returnDesiredSize->height = (std::max)(minSize, availableSize.height);
	return gmpi::MP_OK;
}

int32_t WavetableDisplayGui::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext)
{
	Graphics g(drawingContext);
	const Rect r = getRect();

	backgroundColor_ = parseColor(pinBackgroundColor.getValue(), kDefaultBackground);
	gridColor_       = parseColor(pinGridColor.getValue(), kDefaultGrid);
	waveColor_       = parseColor(pinWaveColor.getValue(), kDefaultWave);
	fillColor_       = parseColor(pinFillColor.getValue(), kDefaultFill);
	framesColor_     = parseColor(pinFramesColor.getValue(), kDefaultFrames);

	g.FillRectangle(r, g.CreateSolidColorBrush(backgroundColor_));

	TableView table;
	WaveView wave;
	const bool hasTable = getTable(table);
	const bool hasWave = getWave(wave);

	// Current waveform: from the Wave blob, or frame 0 of the table, or silence.
	std::vector<float> current;
	float position = 0.f;
	if (hasWave)
	{
		current.resize(wave.points);
		for (int i = 0; i < wave.points; ++i)
			current[i] = wave.data[i] * (1.f / 32767.f);
		position = wave.position;
	}
	else if (hasTable)
	{
		current.resize(table.points);
		for (int i = 0; i < table.points; ++i)
			current[i] = table.data[i] * (1.f / 127.f);
	}
	else
	{
		current.assign(2, 0.f);
	}

	if (pinView.getValue() == 1 && hasTable && table.frames > 1)
		draw3D(g, r, table, current, position);
	else
		draw2D(g, r, current);

	return gmpi::MP_OK;
}

void WavetableDisplayGui::draw2D(Graphics& g, const Rect& r, const std::vector<float>& wave)
{
	const float margin = 4.f;
	const float left = r.left + margin;
	const float right = r.right - margin;
	const float width = right - left;
	const float midY = r.top + r.getHeight() * 0.5f;
	const float amp = (r.getHeight() * 0.5f - margin) * 0.92f;

	if (pinShowGrid.getValue())
	{
		auto gridBrush = g.CreateSolidColorBrush(gridColor_);
		auto faintBrush = g.CreateSolidColorBrush(withAlpha(gridColor_, 0.5f));
		const float px = 0.5f; // pixel-snap

		for (int i = 1; i < 4; ++i)
		{
			const float x = floorf(left + width * i / 4.f) + px;
			g.DrawLine(Point(x, r.top), Point(x, r.bottom), i == 2 ? gridBrush : faintBrush, 1.f);
		}
		g.DrawLine(Point(r.left, floorf(midY - amp * 0.5f) + px), Point(r.right, floorf(midY - amp * 0.5f) + px), faintBrush, 1.f);
		g.DrawLine(Point(r.left, floorf(midY + amp * 0.5f) + px), Point(r.right, floorf(midY + amp * 0.5f) + px), faintBrush, 1.f);
		g.DrawLine(Point(r.left, floorf(midY) + px), Point(r.right, floorf(midY) + px), gridBrush, 1.f);
	}

	const int n = (int)wave.size();
	std::vector<Point> pts(n);
	for (int i = 0; i < n; ++i)
		pts[i] = Point(left + width * i / (float)(n - 1), midY - wave[i] * amp);

	// Filled area between the wave and the centre line.
	{
		auto geometry = g.GetFactory().CreatePathGeometry();
		auto sink = geometry.Open();
		sink.BeginFigure(Point(left, midY), FigureBegin::Filled);
		sink.AddLines(pts.data(), (uint32_t)n);
		sink.AddLine(Point(right, midY));
		sink.EndFigure(FigureEnd::Closed);
		sink.Close();
		g.FillGeometry(geometry, g.CreateSolidColorBrush(fillColor_));
	}

	// Outline.
	{
		auto geometry = g.GetFactory().CreatePathGeometry();
		auto sink = geometry.Open();
		sink.BeginFigure(pts[0], FigureBegin::Hollow);
		sink.AddLines(pts.data() + 1, (uint32_t)(n - 1));
		sink.EndFigure(FigureEnd::Open);
		sink.Close();
		auto brush = g.CreateSolidColorBrush(waveColor_);
		g.DrawGeometry(geometry, brush, (std::max)(0.5f, pinLineWidth.getValue()));
	}
}

void WavetableDisplayGui::draw3D(Graphics& g, const Rect& r, const TableView& table, const std::vector<float>& wave, float position)
{
	// Frames are stacked diagonally: first frame front-left, last frame back-right.
	const float margin = 4.f;
	const float w = r.getWidth() - 2.f * margin;
	const float h = r.getHeight() - 2.f * margin;

	const float waveWidth = w * 0.72f;
	const float depthX = w - waveWidth;   // total shift from front to back
	const float depthY = h * 0.42f;
	const float amp = h * 0.20f;
	const float frontBaseY = r.top + margin + h - amp - 1.f;
	const float frontLeft = r.left + margin;

	const int frames = table.frames;
	auto baseOf = [&](float depth01, float& x0, float& y0)
	{
		x0 = frontLeft + depthX * depth01;
		y0 = frontBaseY - depthY * depth01;
	};

	// Floor outline.
	if (pinShowGrid.getValue())
	{
		auto gridBrush = g.CreateSolidColorBrush(gridColor_);
		float fx, fy, bx, by;
		baseOf(0.f, fx, fy);
		baseOf(1.f, bx, by);
		g.DrawLine(Point(fx, fy), Point(fx + waveWidth, fy), gridBrush, 1.f);
		g.DrawLine(Point(bx, by), Point(bx + waveWidth, by), gridBrush, 1.f);
		g.DrawLine(Point(fx, fy), Point(bx, by), gridBrush, 1.f);
		g.DrawLine(Point(fx + waveWidth, fy), Point(bx + waveWidth, by), gridBrush, 1.f);
	}

	auto makePath = [&](const float* values, int n, float depth01, bool filled)
	{
		float x0, y0;
		baseOf(depth01, x0, y0);

		auto geometry = g.GetFactory().CreatePathGeometry();
		auto sink = geometry.Open();
		if (filled)
		{
			sink.BeginFigure(Point(x0, y0), FigureBegin::Filled);
			for (int i = 0; i < n; ++i)
				sink.AddLine(Point(x0 + waveWidth * i / (float)(n - 1), y0 - values[i] * amp));
			sink.AddLine(Point(x0 + waveWidth, y0));
			sink.EndFigure(FigureEnd::Closed);
		}
		else
		{
			sink.BeginFigure(Point(x0, y0 - values[0] * amp), FigureBegin::Hollow);
			for (int i = 1; i < n; ++i)
				sink.AddLine(Point(x0 + waveWidth * i / (float)(n - 1), y0 - values[i] * amp));
			sink.EndFigure(FigureEnd::Open);
		}
		sink.Close();
		return geometry;
	};

	// Draw at most ~32 background frames so dense tables stay readable.
	const int step = (std::max)(1, (frames + 31) / 32);
	std::vector<float> frame(table.points);
	auto framesBrush = g.CreateSolidColorBrush(framesColor_);
	const float currentDepth = position;

	auto drawFrame = [&](int f)
	{
		for (int i = 0; i < table.points; ++i)
			frame[i] = table.data[(size_t)f * table.points + i] * (1.f / 127.f);
		const float depth = f / (float)(frames - 1);
		auto path = makePath(frame.data(), table.points, depth, false);
		g.DrawGeometry(path, framesBrush, 1.f);
	};

	// Back to front: frames behind the current one, then the current one, then the ones in front.
	for (int f = frames - 1; f >= 0; f -= step)
	{
		if (f / (float)(frames - 1) > currentDepth + 1.0e-4f)
			drawFrame(f);
	}

	g.FillGeometry(makePath(wave.data(), (int)wave.size(), currentDepth, true), g.CreateSolidColorBrush(fillColor_));
	auto waveBrush = g.CreateSolidColorBrush(waveColor_);
	auto outline = makePath(wave.data(), (int)wave.size(), currentDepth, false);
	g.DrawGeometry(outline, waveBrush, (std::max)(0.5f, pinLineWidth.getValue()));

	for (int f = frames - 1; f >= 0; f -= step)
	{
		if (f / (float)(frames - 1) < currentDepth - 1.0e-4f)
			drawFrame(f);
	}
}
