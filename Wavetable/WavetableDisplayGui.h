#pragma once

/*
	Wavetable Display - GUI part (cross-platform, GmpiDrawing).

	Pins:
		Table, Wave      (hidden) data from the DSP part, one per voice
		View             2D (current frame) or 3D (all frames, current one highlighted)
		Background Color AARRGGBB hex, e.g. FF14181F
		Grid Color       grid lines and 3D floor
		Wave Color       current waveform outline
		Fill Color       area under the current waveform (use alpha, e.g. 405BE7FF)
		Frames Color     the other frames in the 3D view
		Line Width       outline width of the current waveform, in pixels
		Show Grid        on/off
*/

#include <vector>
#include "../se_sdk3/mp_sdk_gui2.h"
#include "../se_sdk3/Drawing.h"

class WavetableDisplayGui : public gmpi_gui::MpGuiGfxBase
{
public:
	WavetableDisplayGui();

	int32_t MP_STDCALL measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t MP_STDCALL OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;

private:
	void onDataChanged(int voice);
	void onGateChanged(int voice);
	void onColorsChanged();
	void onRedraw();

	struct TableView
	{
		int frames = 0;
		int points = 0;
		int totalFrames = 0;
		const int8_t* data = nullptr;
	};
	struct WaveView
	{
		int points = 0;
		float position = 0.f;
		const int16_t* data = nullptr;
	};

	bool getTable(TableView& out);
	bool getWave(WaveView& out);

	void draw2D(GmpiDrawing::Graphics& g, const GmpiDrawing::Rect& r, const std::vector<float>& wave);
	void draw3D(GmpiDrawing::Graphics& g, const GmpiDrawing::Rect& r, const TableView& table, const std::vector<float>& wave, float position);

	BlobArrayGuiPin  pinTable;
	BlobArrayGuiPin  pinWave;
	FloatArrayGuiPin pinGate;
	IntGuiPin        pinView;
	StringGuiPin     pinBackgroundColor;
	StringGuiPin     pinGridColor;
	StringGuiPin     pinWaveColor;
	StringGuiPin     pinFillColor;
	StringGuiPin     pinFramesColor;
	FloatGuiPin      pinLineWidth;
	BoolGuiPin       pinShowGrid;

	int newestVoice_ = 0;

	GmpiDrawing::Color backgroundColor_;
	GmpiDrawing::Color gridColor_;
	GmpiDrawing::Color waveColor_;
	GmpiDrawing::Color fillColor_;
	GmpiDrawing::Color framesColor_;
};
