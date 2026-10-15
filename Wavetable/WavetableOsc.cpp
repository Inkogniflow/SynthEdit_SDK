#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "./WavetableOsc.h"
#include "./WavetableDisplayFormat.h"
#include "../shared/unicode_conversion.h"
#include "../se_sdk3/mp_sdk_common.h"

using namespace fvwt;

REGISTER_PLUGIN2(WavetableOsc, L"FV Wavetable Osc");

namespace
{
	constexpr int kWavFileTable = NumBuiltIns; // last enum entry
	constexpr float kOutputLevel = 0.5f;        // +/-5 V, like SynthEdit's own oscillators
}

WavetableOsc::WavetableOsc()
{
	// Order must match the <Audio> pins in Wavetable.xml.
	initializePin(pinPitch);
	initializePin(pinPosition);
	initializePin(pinSync);
	initializePin(pinTable);
	initializePin(pinFile);
	initializePin(pinOutput);
	initializePin(pinDisplayTable);
	initializePin(pinDisplayWave);
}

int32_t WavetableOsc::open()
{
	sampleRate_ = getSampleRate();
	displayInterval_ = (std::max)(64, (int)(sampleRate_ / 30.f));

	SET_PROCESS2(&WavetableOsc::subProcess);

	return MpBase2::open();
}

float WavetableOsc::pitchToIncrement(float pitch) const
{
	// SynthEdit volts: pin value * 10. 1 V/oct, 5 V = 440 Hz.
	float volts = pitch * 10.f;
	if (!(volts < 15.f)) volts = 15.f; // reversed test also catches NaN
	if (volts < -10.f)   volts = -10.f;

	const float hz = 440.f * exp2f(volts - 5.f);
	return (std::min)(hz / sampleRate_, 0.45f);
}

bool WavetableOsc::readFile(const std::wstring& shortName, std::vector<uint8_t>& bytes, std::string& fullName)
{
	bytes.clear();

	// SynthEdit 1.5+: embedded-file aware loading (works for exported plugins too).
	gmpi_sdk::mp_shared_ptr<gmpi::IEmbeddedFileSupport> fileSupport;
	getHost()->queryInterface(gmpi::MP_IID_HOST_EMBEDDED_FILE_SUPPORT, fileSupport.asIMpUnknownPtr());

	if (fileSupport)
	{
		fullName = host.resolveFilename(shortName);
		auto file = host.openUri(fullName);
		if (!file)
			return false;

		const int64_t size = file.size();
		if (size <= 0 || size > 256 * 1024 * 1024)
			return false;

		bytes.resize((size_t)size);
		file.read((char*)bytes.data(), (size_t)size);
		return true;
	}

	// Older SynthEdit: resolve the path and open it directly.
	const std::wstring fullW = host.resolveFilename_old(shortName);
	fullName = JmUnicodeConversions::WStringToUtf8(fullW);

#ifdef _WIN32
	FILE* f = _wfopen(fullW.c_str(), L"rb");
#else
	FILE* f = fopen(fullName.c_str(), "rb");
#endif
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size > 0 && size < 256 * 1024 * 1024)
	{
		bytes.resize((size_t)size);
		bytes.resize(fread(bytes.data(), 1, (size_t)size, f));
	}
	fclose(f);
	return !bytes.empty();
}

void WavetableOsc::loadTable()
{
	std::shared_ptr<const Wavetable> newTable;

	const int choice = pinTable.getValue();
	const std::wstring fileName = pinFile.getValue();

	if (choice == kWavFileTable && !fileName.empty())
	{
		std::vector<uint8_t> bytes;
		std::string fullName;
		if (readFile(fileName, bytes, fullName))
		{
			std::string error;
			newTable = loadWav(bytes, "file:" + fullName, error);
		}
	}

	if (!newTable)
	{
		// Built-in table, or fallback when the file is missing/unreadable.
		newTable = getBuiltIn(choice == kWavFileTable ? Classic : choice);
	}

	if (newTable != table_)
	{
		table_ = newTable;
		sendDisplayTable();
		lastSentPosition_ = -1.f; // force a wave update
		displayCountdown_ = 0;
	}
}

void WavetableOsc::sendDisplayTable()
{
	if (!table_)
		return;

	const Wavetable& wt = *table_;
	const int frames = (std::min)(wt.frames, kDisplayTableMaxFrames);
	const int points = kDisplayTablePoints;

	blobBuffer_.resize(sizeof(TableBlobHeader) + (size_t)frames * points);

	TableBlobHeader header{};
	header.magic = kTableMagic;
	header.version = kFormatVersion;
	header.frames = (uint16_t)frames;
	header.points = (uint16_t)points;
	header.totalFrames = (uint16_t)wt.frames;
	header.serial = wt.serial;
	memcpy(blobBuffer_.data(), &header, sizeof(header));

	int8_t* dst = (int8_t*)(blobBuffer_.data() + sizeof(header));
	for (int i = 0; i < frames; ++i)
	{
		// spread the displayed frames evenly over the whole table
		const int f = frames > 1 ? (int)((int64_t)i * (wt.frames - 1) / (frames - 1)) : 0;
		const float* src = wt.displayFrames.data() + (size_t)f * kDisplayWavePoints;
		for (int p = 0; p < points; ++p)
		{
			const float v = src[(size_t)p * kDisplayWavePoints / points];
			*dst++ = (int8_t)(std::max)(-127.f, (std::min)(127.f, roundf(v * 127.f)));
		}
	}

	pinDisplayTable.setValueRaw(blobBuffer_.size(), blobBuffer_.data());
	pinDisplayTable.sendPinUpdate();
}

void WavetableOsc::sendDisplayWave(float position)
{
	if (!table_)
		return;

	float wave[kDisplayWavePoints];
	renderDisplayWave(*table_, position, wave, kDisplayWavePoints);

	blobBuffer_.resize(sizeof(WaveBlobHeader) + kDisplayWavePoints * sizeof(int16_t));

	WaveBlobHeader header{};
	header.magic = kWaveMagic;
	header.version = kFormatVersion;
	header.points = kDisplayWavePoints;
	header.position = position;
	header.serial = table_->serial;
	header.totalFrames = (uint16_t)table_->frames;
	memcpy(blobBuffer_.data(), &header, sizeof(header));

	int16_t samples[kDisplayWavePoints];
	for (int i = 0; i < kDisplayWavePoints; ++i)
		samples[i] = (int16_t)(std::max)(-32767.f, (std::min)(32767.f, roundf(wave[i] * 32767.f)));
	memcpy(blobBuffer_.data() + sizeof(header), samples, sizeof(samples));

	pinDisplayWave.setValueRaw(blobBuffer_.size(), blobBuffer_.data());
	pinDisplayWave.sendPinUpdate(getBlockPosition());

	lastSentPosition_ = position;
}

void WavetableOsc::onSetPins()
{
	if (pinTable.isUpdated() || pinFile.isUpdated() || !table_)
	{
		loadTable();
	}

	if (!pinPitch.isStreaming())
	{
		increment_ = pitchToIncrement(pinPitch.getValue());
		level_ = levelForIncrement(increment_);
	}

	pinOutput.setStreaming(true);
}

void WavetableOsc::subProcess(int sampleFrames)
{
	const float* pitch = getBuffer(pinPitch);
	const float* position = getBuffer(pinPosition);
	const float* sync = getBuffer(pinSync);
	float* out = getBuffer(pinOutput);

	const Wavetable* wt = table_.get();
	if (!wt || wt->frames < 1)
	{
		for (int s = 0; s < sampleFrames; ++s)
			out[s] = 0.f;
		return;
	}

	const bool pitchModulated = pinPitch.isStreaming();
	const int lastFrame = wt->frames - 1;

	float phase = phase_;
	float increment = increment_;
	int level = level_;
	float prevSync = prevSync_;
	float pos = lastPosition_;

	for (int s = 0; s < sampleFrames; ++s)
	{
		if (pitchModulated)
		{
			increment = pitchToIncrement(pitch[s]);
			level = levelForIncrement(increment);
		}

		// hard sync on rising edge
		const float sy = sync[s];
		if (sy > 0.f && prevSync <= 0.f)
			phase = 0.f;
		prevSync = sy;

		// frame position, 0..10 V
		pos = position[s];
		pos = pos > 0.f ? (pos < 1.f ? pos : 1.f) : 0.f;
		const float fpos = pos * lastFrame;
		const int f0 = (int)fpos;
		const int f1 = f0 < lastFrame ? f0 + 1 : lastFrame;
		const float ft = fpos - f0;

		// read both frames at the same mip level (Hermite interpolation)
		const int size = wt->levelSize[level];
		const float idx = phase * size;
		const int i = (int)idx;
		const float t = idx - i;

		const float va = hermite(wt->level(f0, level) + i, t);
		const float vb = hermite(wt->level(f1, level) + i, t);

		out[s] = kOutputLevel * (va + ft * (vb - va));

		phase += increment;
		if (phase >= 1.f)
			phase -= 1.f;
	}

	phase_ = phase;
	increment_ = increment;
	level_ = level;
	prevSync_ = prevSync;
	lastPosition_ = pos;

	// Display update, throttled.
	displayCountdown_ -= sampleFrames;
	if (displayCountdown_ <= 0)
	{
		displayCountdown_ = displayInterval_;
		if (fabsf(pos - lastSentPosition_) > 0.0005f)
			sendDisplayWave(pos);
	}
}
