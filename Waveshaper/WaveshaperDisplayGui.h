#pragma once

/*
	SXE Waveshaper Display - GUI part (cross-platform, GmpiDrawing).

	Pins:
		Table, Curve      (hidden) data from the DSP part, one per voice
		View              2D (current transfer curve) or 3D (all functions, current one highlighted)
		Background Color  AARRGGBB or RRGGBB hex, '#' allowed
		Grid Color        grid, identity diagonal, 3D floor
		Curve Color       current curve outline
		Fill Color        area between the curve and zero (use alpha)
		Frames Color      the other functions in the 3D view
		Text Color        function name, formula and A..D values
		Level Color       band showing the input level (peak of both channels)
		Line Width        outline width of the current curve, in pixels
		Show Grid, Show Text
		Font Size         in pixels
*/

#include <string>
#include <vector>
#include "../se_sdk3/mp_sdk_gui2.h"
#include "../se_sdk3/Drawing.h"

class WaveshaperDisplayGui : public gmpi_gui::MpGuiGfxBase
{
public:
	WaveshaperDisplayGui();

	int32_t MP_STDCALL measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t MP_STDCALL OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;

private:
	void onDataChanged(int voice);
	void onGateChanged(int voice);
	void onRedraw();
	void redraw();

	struct FunctionInfo
	{
		unsigned varMask = 0;
		float lo[4] = {};
		float hi[4] = {};
		std::string name;
		std::string formula;
		std::string error;
	};
	struct TableView
	{
		int count = 0;
		int points = 0;
		uint32_t serial = 0;
		const int8_t* data = nullptr;
		std::vector<FunctionInfo> info;
	};
	struct CurveView
	{
		int points = 0;
		int count = 0;
		uint32_t serial = 0;
		float position = 0.f;
		float vars[4] = {};
		float peak = 0.f;
		const int16_t* data = nullptr;
	};

	bool getTable(TableView& out);
	bool getCurve(CurveView& out);

	void drawGrid2D(GmpiDrawing::Graphics& g, const GmpiDrawing::Rect& plot, float range);
	void draw2D(GmpiDrawing::Graphics& g, const GmpiDrawing::Rect& plot, const std::vector<float>& curve, float range, float peak);
	void draw3D(GmpiDrawing::Graphics& g, const GmpiDrawing::Rect& plot, const TableView& table, const std::vector<float>& curve, float position, float range);
	void drawText(GmpiDrawing::Graphics& g, const GmpiDrawing::Rect& header, const GmpiDrawing::Rect& footer,
	              const TableView* table, const CurveView* curve);

	BlobArrayGuiPin  pinTable;
	BlobArrayGuiPin  pinCurve;
	FloatArrayGuiPin pinGate;
	IntGuiPin        pinView;
	StringGuiPin     pinBackgroundColor;
	StringGuiPin     pinGridColor;
	StringGuiPin     pinCurveColor;
	StringGuiPin     pinFillColor;
	StringGuiPin     pinFramesColor;
	StringGuiPin     pinTextColor;
	StringGuiPin     pinLevelColor;
	FloatGuiPin      pinLineWidth;
	BoolGuiPin       pinShowGrid;
	BoolGuiPin       pinShowText;
	FloatGuiPin      pinFontSize;

	int newestVoice_ = 0;

	GmpiDrawing::Color backgroundColor_;
	GmpiDrawing::Color gridColor_;
	GmpiDrawing::Color curveColor_;
	GmpiDrawing::Color fillColor_;
	GmpiDrawing::Color framesColor_;
	GmpiDrawing::Color textColor_;
	GmpiDrawing::Color levelColor_;
};
