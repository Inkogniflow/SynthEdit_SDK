/*
	Wavetable Display - DSP part.
	Receives the oscillator's display blobs and passes them to the GUI part
	(via private parameters, one per voice).
*/

#include "../se_sdk3/mp_sdk_audio.h"

class WavetableDisplay : public MpBase2
{
	BlobInPin  pinTableIn;
	BlobInPin  pinWaveIn;
	BlobOutPin pinTableOut;
	BlobOutPin pinWaveOut;

public:
	WavetableDisplay()
	{
		// Order must match the <Audio> pins in Wavetable.xml.
		initializePin(pinTableIn);
		initializePin(pinWaveIn);
		initializePin(pinTableOut);
		initializePin(pinWaveOut);
	}

	void onSetPins() override
	{
		if (pinTableIn.isUpdated())
		{
			pinTableOut.setValueRaw((size_t)pinTableIn.rawSize(), pinTableIn.rawData());
			pinTableOut.sendPinUpdate();
		}

		if (pinWaveIn.isUpdated())
		{
			pinWaveOut.setValueRaw((size_t)pinWaveIn.rawSize(), pinWaveIn.rawData());
			pinWaveOut.sendPinUpdate();
		}
	}
};

REGISTER_PLUGIN2(WavetableDisplay, L"FV Wavetable Display");
