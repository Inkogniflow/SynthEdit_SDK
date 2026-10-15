#include <stdio.h>
#include <string.h>
#include <wctype.h>
#include <math.h>
#include <cmath>
#include <algorithm>
#include "./WaveshaperDisplayGui.h"
#include "./WaveshaperDisplayFormat.h"

using namespace gmpi;
using namespace gmpi_gui;
using namespace GmpiDrawing;
using namespace sxws;

GMPI_REGISTER_GUI(MP_SUB_TYPE_GUI2, WaveshaperDisplayGui, L"Sxynthedit Waveshaper Display");

namespace
{
	// Default colours (AARRGGBB), also used when a colour pin is empty or invalid.
	constexpr uint32_t kDefaultBackground = 0xFF14181F;
	constexpr uint32_t kDefaultGrid       = 0xFF2C3440;
	constexpr uint32_t kDefaultCurve      = 0xFFFFB347;
	constexpr uint32_t kDefaultFill       = 0x30FFB347;
	constexpr uint32_t kDefaultFrames     = 0x60A88A5B;
	constexpr uint32_t kDefaultText       = 0xFFC8D0DC;
	constexpr uint32_t kDefaultLevel      = 0x305BE7FF;
	constexpr uint32_t kErrorColor        = 0xFFFF5A5A;

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

	float displayRange(const std::vector<float>& curve)
	{
		float m = 0.f;
		for (float v : curve)
			m = (std::max)(m, fabsf(v));
		return m <= 1.05f ? 1.f : (m <= 1.55f ? 1.5f : 2.f);
	}
}

WaveshaperDisplayGui::WaveshaperDisplayGui()
{
	// Pin ids match the <GUI> pins in Waveshaper.xml.
	initializePin(0, pinTable, static_cast<MpGuiBaseMemberIndexedPtr2>(&WaveshaperDisplayGui::onDataChanged));
	initializePin(1, pinCurve, static_cast<MpGuiBaseMemberIndexedPtr2>(&WaveshaperDisplayGui::onDataChanged));
	initializePin(2, pinGate,  static_cast<MpGuiBaseMemberIndexedPtr2>(&WaveshaperDisplayGui::onGateChanged));
	initializePin(3, pinView,            static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(4, pinBackgroundColor, static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(5, pinGridColor,       static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(6, pinCurveColor,      static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(7, pinFillColor,       static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(8, pinFramesColor,     static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(9, pinTextColor,       static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(10, pinLevelColor,     static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(11, pinLineWidth,      static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(12, pinShowGrid,       static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(13, pinShowText,       static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));
	initializePin(14, pinFontSize,       static_cast<MpGuiBaseMemberPtr2>(&WaveshaperDisplayGui::onRedraw));

	// No host calls here: the graphics host is only connected after construction.
}

void WaveshaperDisplayGui::redraw()
{
	if (getGuiHost())
		invalidateRect();
}

void WaveshaperDisplayGui::onDataChanged(int voice)
{
	// Follow the most recently updated voice, but don't steal focus from a held note.
	const bool currentHeld = pinGate.getValue(newestVoice_) > 0.f;
	const bool thisHeld = pinGate.getValue(voice) > 0.f;
	if (!currentHeld || thisHeld)
		newestVoice_ = voice;
	redraw();
}

void WaveshaperDisplayGui::onGateChanged(int voice)
{
	if (pinGate.getValue(voice) > 0.f)
	{
		newestVoice_ = voice;
		redraw();
	}
}

void WaveshaperDisplayGui::onRedraw()
{
	redraw();
}

bool WaveshaperDisplayGui::getTable(TableView& out)
{
	auto tryVoice = [&](int v) -> bool
	{
		const int size = pinTable.rawSize(v);
		if (size < (int)sizeof(TableBlobHeader))
			return false;

		const uint8_t* base = (const uint8_t*)pinTable.rawData(v);
		TableBlobHeader h;
		memcpy(&h, base, sizeof(h));
		if (h.magic != kTableMagic || h.version != kFormatVersion || h.count < 1 || h.points < 2)
			return false;

		size_t offset = sizeof(h) + (size_t)h.count * h.points;
		if ((size_t)size < offset)
			return false;

		std::vector<FunctionInfo> info(h.count);
		for (auto& fi : info)
		{
			FunctionRecord r;
			if ((size_t)size < offset + sizeof(r))
				return false;
			memcpy(&r, base + offset, sizeof(r));
			offset += sizeof(r);
			const size_t textBytes = (size_t)r.nameBytes + r.formulaBytes + r.errorBytes;
			if ((size_t)size < offset + textBytes)
				return false;

			fi.varMask = r.varMask;
			for (int i = 0; i < 4; ++i)
			{
				fi.lo[i] = r.lo[i];
				fi.hi[i] = r.hi[i];
			}
			const char* t = (const char*)base + offset;
			fi.name.assign(t, r.nameBytes);
			fi.formula.assign(t + r.nameBytes, r.formulaBytes);
			fi.error.assign(t + r.nameBytes + r.formulaBytes, r.errorBytes);
			offset += textBytes;
		}

		out.count = h.count;
		out.points = h.points;
		out.serial = h.serial;
		out.data = (const int8_t*)(base + sizeof(h));
		out.info = std::move(info);
		return true;
	};

	if (tryVoice(newestVoice_))
		return true;
	for (int v = 0; v < pinTable.size(); ++v)
		if (v != newestVoice_ && tryVoice(v))
			return true;
	return false;
}

bool WaveshaperDisplayGui::getCurve(CurveView& out)
{
	auto tryVoice = [&](int v) -> bool
	{
		const int size = pinCurve.rawSize(v);
		if (size < (int)sizeof(CurveBlobHeader))
			return false;

		CurveBlobHeader h;
		memcpy(&h, pinCurve.rawData(v), sizeof(h));
		if (h.magic != kCurveMagic || h.version != kFormatVersion || h.points < 2)
			return false;
		if (size < (int)(sizeof(h) + (size_t)h.points * sizeof(int16_t)))
			return false;

		out.points = h.points;
		out.count = h.count;
		out.serial = h.serial;
		out.position = (std::max)(0.f, (std::min)(1.f, h.position));
		for (int i = 0; i < 4; ++i)
			out.vars[i] = std::isfinite(h.vars[i]) ? (std::max)(-1.f, (std::min)(1.f, h.vars[i])) : 0.f;
		const float pk = (std::max)(h.peakL, h.peakR);
		out.peak = std::isfinite(pk) ? (std::max)(0.f, pk) : 0.f;
		out.data = (const int16_t*)((const uint8_t*)pinCurve.rawData(v) + sizeof(h));
		return true;
	};

	if (tryVoice(newestVoice_))
		return true;
	for (int v = 0; v < pinCurve.size(); ++v)
		if (v != newestVoice_ && tryVoice(v))
			return true;
	return false;
}

int32_t WaveshaperDisplayGui::measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize)
{
	const float minSize = 30.f;
	returnDesiredSize->width = (std::max)(minSize, availableSize.width);
	returnDesiredSize->height = (std::max)(minSize, availableSize.height);
	return gmpi::MP_OK;
}

int32_t WaveshaperDisplayGui::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext)
{
	Graphics g(drawingContext);
	const Rect r = getRect();

	backgroundColor_ = parseColor(pinBackgroundColor.getValue(), kDefaultBackground);
	gridColor_       = parseColor(pinGridColor.getValue(), kDefaultGrid);
	curveColor_      = parseColor(pinCurveColor.getValue(), kDefaultCurve);
	fillColor_       = parseColor(pinFillColor.getValue(), kDefaultFill);
	framesColor_     = parseColor(pinFramesColor.getValue(), kDefaultFrames);
	textColor_       = parseColor(pinTextColor.getValue(), kDefaultText);
	levelColor_      = parseColor(pinLevelColor.getValue(), kDefaultLevel);

	g.FillRectangle(r, g.CreateSolidColorBrush(backgroundColor_));

	TableView table;
	CurveView curveView;
	const bool hasTable = getTable(table);
	const bool hasCurve = getCurve(curveView);

	// Current curve: from the Curve blob, else function 1 of the table, else identity.
	std::vector<float> curve;
	float position = 0.f, peak = 0.f;
	if (hasCurve)
	{
		curve.resize(curveView.points);
		for (int i = 0; i < curveView.points; ++i)
			curve[i] = curveView.data[i] * (1.f / kCurveScale);
		position = curveView.position;
		peak = curveView.peak;
	}
	else if (hasTable)
	{
		curve.resize(table.points);
		for (int i = 0; i < table.points; ++i)
			curve[i] = table.data[i] * (1.f / kTableScale);
	}
	else
	{
		curve = { -1.f, 1.f };
	}

	// Layout: one text line on top (name, index), one at the bottom (formula or A..D values).
	const float fontSize = (std::max)(6.f, (std::min)(40.f, pinFontSize.getValue() > 0.f ? pinFontSize.getValue() : 11.f));
	const float lineH = ceilf(fontSize * 1.35f);
	const bool showText = pinShowText.getValue() && r.getHeight() >= lineH * 2.f + 30.f;

	Rect plot = r;
	Rect header(r.left, r.top, r.right, r.top);
	Rect footer(r.left, r.bottom, r.right, r.bottom);
	if (showText)
	{
		header = Rect(r.left + 4.f, r.top + 1.f, r.right - 4.f, r.top + 1.f + lineH);
		footer = Rect(r.left + 4.f, r.bottom - 1.f - lineH, r.right - 4.f, r.bottom - 1.f);
		plot = Rect(r.left, header.bottom, r.right, footer.top);
	}

	float range = displayRange(curve);

	if (pinView.getValue() == 1 && hasTable && table.count > 1)
	{
		// range over the whole table so the stack doesn't jump while scrolling
		float m = 0.f;
		for (int i = 0; i < table.count * table.points; ++i)
			m = (std::max)(m, fabsf(table.data[i] * (1.f / kTableScale)));
		range = (std::max)(range, m <= 1.05f ? 1.f : (m <= 1.55f ? 1.5f : 2.f));
		draw3D(g, plot, table, curve, position, range);
	}
	else
	{
		draw2D(g, plot, curve, range, peak);
	}

	if (showText)
		drawText(g, header, footer, hasTable ? &table : nullptr, hasCurve ? &curveView : nullptr);

	return gmpi::MP_OK;
}

void WaveshaperDisplayGui::drawGrid2D(Graphics& g, const Rect& plot, float range)
{
	const float margin = 4.f;
	const float left = plot.left + margin, right = plot.right - margin;
	const float top = plot.top + margin, bottom = plot.bottom - margin;
	const float midX = (left + right) * 0.5f, midY = (top + bottom) * 0.5f;
	const float px = 0.5f;

	auto gridBrush = g.CreateSolidColorBrush(gridColor_);
	auto faintBrush = g.CreateSolidColorBrush(withAlpha(gridColor_, 0.5f));

	// vertical: x = -0.5, 0, +0.5 ; edges at -1, +1
	for (int i = 0; i <= 4; ++i)
	{
		const float x = floorf(left + (right - left) * i / 4.f) + px;
		g.DrawLine(Point(x, top), Point(x, bottom), i == 2 ? gridBrush : faintBrush, 1.f);
	}
	// horizontal: y = 0, +/-1 (and +/-0.5 when range is 1)
	auto yPix = [&](float y) { return floorf(midY - y / range * (bottom - top) * 0.5f) + px; };
	g.DrawLine(Point(left, yPix(0.f)), Point(right, yPix(0.f)), gridBrush, 1.f);
	for (float y : { -1.f, 1.f, -0.5f, 0.5f, -2.f, 2.f, -1.5f, 1.5f })
	{
		if (fabsf(y) > range + 1.0e-3f || (range > 1.f && fabsf(y) == 0.5f))
			continue;
		g.DrawLine(Point(left, yPix(y)), Point(right, yPix(y)), faintBrush, 1.f);
	}
	// identity y = x as reference
	g.DrawLine(Point(left, midY + 1.f / range * (bottom - top) * 0.5f), Point(right, midY - 1.f / range * (bottom - top) * 0.5f), faintBrush, 1.f);
	(void)midX;
}

void WaveshaperDisplayGui::draw2D(Graphics& g, const Rect& plot, const std::vector<float>& curve, float range, float peak)
{
	const float margin = 4.f;
	const float left = plot.left + margin, right = plot.right - margin;
	const float top = plot.top + margin, bottom = plot.bottom - margin;
	const float width = right - left;
	const float midX = (left + right) * 0.5f, midY = (top + bottom) * 0.5f;
	const float amp = (bottom - top) * 0.5f / range;

	// input level band (-peak..+peak)
	if (peak > 0.001f)
	{
		const float p = (std::min)(1.f, peak);
		g.FillRectangle(Rect(midX - p * width * 0.5f, top, midX + p * width * 0.5f, bottom), g.CreateSolidColorBrush(levelColor_));
	}

	if (pinShowGrid.getValue())
		drawGrid2D(g, plot, range);

	const int n = (int)curve.size();
	std::vector<Point> pts(n);
	for (int i = 0; i < n; ++i)
	{
		const float y = (std::max)(-range, (std::min)(range, curve[i]));
		pts[i] = Point(left + width * i / (float)(n - 1), midY - y * amp);
	}

	// area between curve and zero
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

	// outline
	{
		auto geometry = g.GetFactory().CreatePathGeometry();
		auto sink = geometry.Open();
		sink.BeginFigure(pts[0], FigureBegin::Hollow);
		sink.AddLines(pts.data() + 1, (uint32_t)(n - 1));
		sink.EndFigure(FigureEnd::Open);
		sink.Close();
		auto brush = g.CreateSolidColorBrush(curveColor_);
		g.DrawGeometry(geometry, brush, (std::max)(0.5f, pinLineWidth.getValue()));
	}

	// marker where the input peak hits the curve (right side)
	if (peak > 0.001f && n > 1)
	{
		const float p = (std::min)(1.f, peak);
		const float fi = (p + 1.f) * 0.5f * (n - 1);
		const int i0 = (std::min)((int)fi, n - 2);
		const float t = fi - i0;
		const float y = pts[i0].y + t * (pts[i0 + 1].y - pts[i0].y);
		const float x = midX + p * width * 0.5f;
		const float s = 2.5f;
		g.FillRectangle(Rect(x - s, y - s, x + s, y + s), g.CreateSolidColorBrush(curveColor_));
	}
}

void WaveshaperDisplayGui::draw3D(Graphics& g, const Rect& plot, const TableView& table, const std::vector<float>& curve, float position, float range)
{
	// Functions are stacked diagonally: first function front-left, last one back-right.
	const float margin = 4.f;
	const float w = plot.getWidth() - 2.f * margin;
	const float h = plot.getHeight() - 2.f * margin;

	const float curveWidth = w * 0.70f;
	const float depthX = w - curveWidth;
	const float amp = h * 0.24f / range;
	const float depthY = h - 2.f * amp * range - 2.f;
	const float frontBaseY = plot.top + margin + h - amp * range - 1.f;
	const float frontLeft = plot.left + margin;

	const int frames = table.count;
	auto baseOf = [&](float depth01, float& x0, float& y0)
	{
		x0 = frontLeft + depthX * depth01;
		y0 = frontBaseY - depthY * depth01;
	};

	if (pinShowGrid.getValue())
	{
		auto gridBrush = g.CreateSolidColorBrush(gridColor_);
		float fx, fy, bx, by;
		baseOf(0.f, fx, fy);
		baseOf(1.f, bx, by);
		g.DrawLine(Point(fx, fy), Point(fx + curveWidth, fy), gridBrush, 1.f);
		g.DrawLine(Point(bx, by), Point(bx + curveWidth, by), gridBrush, 1.f);
		g.DrawLine(Point(fx, fy), Point(bx, by), gridBrush, 1.f);
		g.DrawLine(Point(fx + curveWidth, fy), Point(bx + curveWidth, by), gridBrush, 1.f);
		g.DrawLine(Point(fx + curveWidth * 0.5f, fy), Point(bx + curveWidth * 0.5f, by), g.CreateSolidColorBrush(withAlpha(gridColor_, 0.5f)), 1.f);
	}

	auto makePath = [&](const float* values, int n, float depth01, bool filled)
	{
		float x0, y0;
		baseOf(depth01, x0, y0);
		auto clampY = [&](float v) { return (std::max)(-range, (std::min)(range, v)); };

		auto geometry = g.GetFactory().CreatePathGeometry();
		auto sink = geometry.Open();
		if (filled)
		{
			sink.BeginFigure(Point(x0, y0), FigureBegin::Filled);
			for (int i = 0; i < n; ++i)
				sink.AddLine(Point(x0 + curveWidth * i / (float)(n - 1), y0 - clampY(values[i]) * amp));
			sink.AddLine(Point(x0 + curveWidth, y0));
			sink.EndFigure(FigureEnd::Closed);
		}
		else
		{
			sink.BeginFigure(Point(x0, y0 - clampY(values[0]) * amp), FigureBegin::Hollow);
			for (int i = 1; i < n; ++i)
				sink.AddLine(Point(x0 + curveWidth * i / (float)(n - 1), y0 - clampY(values[i]) * amp));
			sink.EndFigure(FigureEnd::Open);
		}
		sink.Close();
		return geometry;
	};

	std::vector<float> frame(table.points);
	auto framesBrush = g.CreateSolidColorBrush(framesColor_);
	auto drawFrame = [&](int f)
	{
		for (int i = 0; i < table.points; ++i)
			frame[i] = table.data[(size_t)f * table.points + i] * (1.f / kTableScale);
		auto path = makePath(frame.data(), table.points, f / (float)(frames - 1), false);
		g.DrawGeometry(path, framesBrush, 1.f);
	};

	// back to front: behind the current one, the current one, then in front
	for (int f = frames - 1; f >= 0; --f)
		if (f / (float)(frames - 1) > position + 1.0e-4f)
			drawFrame(f);

	g.FillGeometry(makePath(curve.data(), (int)curve.size(), position, true), g.CreateSolidColorBrush(fillColor_));
	auto curveBrush = g.CreateSolidColorBrush(curveColor_);
	auto outline = makePath(curve.data(), (int)curve.size(), position, false);
	g.DrawGeometry(outline, curveBrush, (std::max)(0.5f, pinLineWidth.getValue()));

	for (int f = frames - 1; f >= 0; --f)
		if (f / (float)(frames - 1) < position - 1.0e-4f)
			drawFrame(f);
}

void WaveshaperDisplayGui::drawText(Graphics& g, const Rect& header, const Rect& footer, const TableView* table, const CurveView* curve)
{
	const float fontSize = (std::max)(6.f, (std::min)(40.f, pinFontSize.getValue() > 0.f ? pinFontSize.getValue() : 11.f));
	auto format = g.GetFactory().CreateTextFormat(fontSize);
	if (format.isNull())
		return;
	format.SetWordWrapping(WordWrapping::NoWrap);
	format.SetParagraphAlignment(ParagraphAlignment::Center);

	auto textBrush = g.CreateSolidColorBrush(textColor_);
	auto dimBrush = g.CreateSolidColorBrush(withAlpha(textColor_, 0.6f));

	if (!table)
	{
		format.SetTextAlignment(TextAlignment::Leading);
		g.DrawTextU("no data - connect Table and Curve", format, header, dimBrush, DrawTextOptions::Clip);
		return;
	}

	const float position = curve ? curve->position : 0.f;
	const int count = table->count;
	const int index = (std::max)(0, (std::min)(count - 1, (int)lrintf(position * (count - 1))));
	const FunctionInfo& fi = table->info[index];

	// header: name (left), index (right)
	char counter[32];
	snprintf(counter, sizeof(counter), "%d / %d", index + 1, count);
	const float counterW = fontSize * 0.62f * (float)strlen(counter) + 4.f;

	format.SetTextAlignment(TextAlignment::Trailing);
	g.DrawTextU(counter, format, header, dimBrush, DrawTextOptions::Clip);

	format.SetTextAlignment(TextAlignment::Leading);
	Rect nameRect = header;
	nameRect.right = (std::max)(nameRect.left, header.right - counterW);
	g.DrawTextU(fi.name, format, nameRect, textBrush, DrawTextOptions::Clip);

	// footer: error, or A..D values when used, else the formula
	if (!fi.error.empty())
	{
		g.DrawTextU("error: " + fi.error, format, footer, g.CreateSolidColorBrush(Color::FromArgb(kErrorColor)), DrawTextOptions::Clip);
		return;
	}

	std::string vars;
	if (curve)
	{
		for (int v = 0; v < 4; ++v)
		{
			if (!(fi.varMask & (1u << v)))
				continue;
			const float value = fi.lo[v] + (curve->vars[v] + 1.f) * 0.5f * (fi.hi[v] - fi.lo[v]);
			char buf[32];
			snprintf(buf, sizeof(buf), "%s%c %.2f", vars.empty() ? "" : "   ", 'A' + v, value);
			vars += buf;
		}
	}

	if (vars.empty())
	{
		g.DrawTextU(fi.formula, format, footer, dimBrush, DrawTextOptions::Clip);
	}
	else
	{
		// values right-aligned, formula in the remaining space on the left
		const float varsW = fontSize * 0.6f * (float)vars.size() + 6.f;
		format.SetTextAlignment(TextAlignment::Trailing);
		g.DrawTextU(vars, format, footer, textBrush, DrawTextOptions::Clip);
		if (footer.getWidth() - varsW > fontSize * 6.f)
		{
			format.SetTextAlignment(TextAlignment::Leading);
			Rect fr = footer;
			fr.right = footer.right - varsW;
			g.DrawTextU(fi.formula, format, fr, dimBrush, DrawTextOptions::Clip);
		}
	}
}
