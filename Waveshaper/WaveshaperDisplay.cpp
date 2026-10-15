/*
	SXE Waveshaper Display - DSP part.
	Receives the waveshaper's display blobs and passes them to the GUI part
	(via private parameters, one per voice).
*/

#include "../se_sdk3/mp_sdk_audio.h"

class WaveshaperDisplay : public MpBase2
{
	BlobInPin  pinTableIn;
	BlobInPin  pinCurveIn;
	BlobOutPin pinTableOut;
	BlobOutPin pinCurveOut;

public:
	WaveshaperDisplay()
	{
		// Order must match the <Audio> pins in Waveshaper.xml.
		initializePin(pinTableIn);
		initializePin(pinCurveIn);
		initializePin(pinTableOut);
		initializePin(pinCurveOut);
	}

	void onSetPins() override
	{
		if (pinTableIn.isUpdated())
		{
			pinTableOut.setValueRaw((size_t)pinTableIn.rawSize(), pinTableIn.rawData());
			pinTableOut.sendPinUpdate();
		}

		if (pinCurveIn.isUpdated())
		{
			pinCurveOut.setValueRaw((size_t)pinCurveIn.rawSize(), pinCurveIn.rawData());
			pinCurveOut.sendPinUpdate();
		}
	}
};

REGISTER_PLUGIN2(WaveshaperDisplay, L"Sxynthedit Waveshaper Display");
