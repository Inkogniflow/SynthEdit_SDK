#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else
	#include <dirent.h>
	#include <sys/stat.h>
#endif

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>
#include <algorithm>
#include "./WavetableOsc.h"
#include "./WavetableDisplayFormat.h"
#include "../shared/unicode_conversion.h"
#include "../se_sdk3/mp_sdk_common.h"

using namespace fvwt;

REGISTER_PLUGIN2(WavetableOsc, L"FV Wavetable Osc");

namespace
{
	constexpr int kWavFileTable  = NumBuiltIns;     // enum entries after the built-ins
	constexpr int kFileListTable = NumBuiltIns + 1;
	constexpr float kOutputLevel = 0.5f;            // +/-5 V, like SynthEdit's own oscillators

#ifdef _WIN32
	constexpr wchar_t kPathSep = L'\\';
#else
	constexpr wchar_t kPathSep = L'/';
#endif

	std::wstring trim(const std::wstring& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && (iswspace(s[a]) || s[a] == L'"')) ++a;
		while (b > a && (iswspace(s[b - 1]) || s[b - 1] == L'"')) --b;
		return s.substr(a, b - a);
	}

	std::vector<std::wstring> splitList(const std::wstring& list)
	{
		std::vector<std::wstring> items;
		std::wstring current;
		for (wchar_t c : list)
		{
			if (c == L';' || c == L'|' || c == L'\n' || c == L'\r')
			{
				current = trim(current);
				if (!current.empty())
					items.push_back(current);
				current.clear();
			}
			else
			{
				current += c;
			}
		}
		current = trim(current);
		if (!current.empty())
			items.push_back(current);
		return items;
	}

	// "wave2" before "wave10", case-insensitive.
	bool naturalLess(const std::wstring& a, const std::wstring& b)
	{
		size_t i = 0, j = 0;
		while (i < a.size() && j < b.size())
		{
			if (iswdigit(a[i]) && iswdigit(b[j]))
			{
				size_t i2 = i, j2 = j;
				while (i2 < a.size() && iswdigit(a[i2])) ++i2;
				while (j2 < b.size() && iswdigit(b[j2])) ++j2;
				const std::wstring na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
				const std::wstring ta = na.substr((std::min)(na.find_first_not_of(L'0'), na.size()));
				const std::wstring tb = nb.substr((std::min)(nb.find_first_not_of(L'0'), nb.size()));
				if (ta.size() != tb.size())
					return ta.size() < tb.size();
				if (ta != tb)
					return ta < tb;
				i = i2;
				j = j2;
			}
			else
			{
				const wchar_t ca = towlower(a[i]), cb = towlower(b[j]);
				if (ca != cb)
					return ca < cb;
				++i;
				++j;
			}
		}
		return a.size() - i < b.size() - j;
	}

	bool hasWavExtension(const std::wstring& name)
	{
		if (name.size() < 4)
			return false;
		std::wstring ext = name.substr(name.size() - 4);
		for (auto& c : ext)
			c = towlower(c);
		return ext == L".wav";
	}

	bool isDirectory(const std::wstring& path)
	{
		if (path.empty())
			return false;
#ifdef _WIN32
		const DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
		struct stat st;
		const auto utf8 = JmUnicodeConversions::WStringToUtf8(path);
		return stat(utf8.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
	}

	// Full paths of the .wav files in a folder, in natural order. Hidden files are skipped.
	std::vector<std::wstring> listWavFiles(std::wstring folder)
	{
		while (!folder.empty() && (folder.back() == L'/' || folder.back() == L'\\'))
			folder.pop_back();

		std::vector<std::wstring> names;
#ifdef _WIN32
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((folder + L"\\*.wav").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) && fd.cFileName[0] != L'.')
					names.push_back(fd.cFileName);
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
#else
		const auto utf8 = JmUnicodeConversions::WStringToUtf8(folder);
		if (DIR* dir = opendir(utf8.c_str()))
		{
			while (dirent* e = readdir(dir))
			{
				if (e->d_name[0] == '.') // hidden, and macOS "._" resource files
					continue;
				const auto name = JmUnicodeConversions::Utf8ToWstring(e->d_name);
				if (hasWavExtension(name))
					names.push_back(name);
			}
			closedir(dir);
		}
#endif
		std::sort(names.begin(), names.end(), naturalLess);

		std::vector<std::wstring> paths;
		for (const auto& n : names)
			paths.push_back(folder + kPathSep + n);
		return paths;
	}
}

WavetableOsc::WavetableOsc()
{
	// Order must match the <Audio> pins in Wavetable.xml.
	initializePin(pinPitch);
	initializePin(pinPosition);
	initializePin(pinPhase);
	initializePin(pinSync);
	initializePin(pinTable);
	initializePin(pinFile);
	initializePin(pinFileList);
	initializePin(pinVoices);
	initializePin(pinDetune);
	initializePin(pinWidth);
	initializePin(pinPan);
	initializePin(pinOn);
	initializePin(pinLeft);
	initializePin(pinRight);
	initializePin(pinDisplayTable);
	initializePin(pinDisplayWave);
}

int32_t WavetableOsc::open()
{
	sampleRate_ = getSampleRate();
	displayInterval_ = (std::max)(64, (int)(sampleRate_ / 30.f));
	resetPhases();

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

void WavetableOsc::resetPhases()
{
	for (int v = 0; v < kMaxVoices; ++v)
	{
		// Unison voices start spread over the cycle (golden ratio), voice 1 at 0.
		const float start = numVoices_ > 1 ? (float)fmod(v * 0.6180339887, 1.0) : 0.f;
		voices_[v].startPhase = start;
		voices_[v].phase = start;
	}
}

void WavetableOsc::updateUnison(float detune, float width, float pan)
{
	lastDetune_ = detune;
	lastWidth_ = width;
	lastPan_ = pan;

	detune = (std::max)(0.f, (std::min)(1.f, detune));        // 0..10 V
	width  = (std::max)(0.f, (std::min)(1.f, width));         // 0..10 V
	pan    = (std::max)(-1.f, (std::min)(1.f, pan * 2.f));    // -5..+5 V

	const int n = numVoices_;
	const float maxCents = 100.f * detune * detune;           // curved: 5 V = 25 cents
	unisonGain_ = 1.f / sqrtf((float)n);

	for (int v = 0; v < n; ++v)
	{
		const float spread = n > 1 ? 2.f * v / (n - 1) - 1.f : 0.f; // -1..+1
		const float cents = maxCents * spread;

		Voice& voice = voices_[v];
		voice.ratio = exp2f(cents / 1200.f);
		voice.levelOffset = 2.f * cents / 1200.f;

		// balance pan law: centre = 0 dB on both sides, the far side fades out, no boost at the edges
		const float p = (std::max)(-1.f, (std::min)(1.f, pan + width * spread));
		voice.gainL = p > 0.f ? 1.f - p : 1.f;
		voice.gainR = p < 0.f ? 1.f + p : 1.f;
	}
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

std::shared_ptr<const Wavetable> WavetableOsc::loadFileList(const std::wstring& list)
{
	// Expand folders, keep explicit files in the given order.
	std::vector<std::wstring> files;
	for (const auto& item : splitList(list))
	{
		std::wstring folder;
		if (isDirectory(item))
			folder = item;
		else
		{
			const auto resolved = host.resolveFilename_old(item); // relative to SynthEdit's default folder
			if (isDirectory(resolved))
				folder = resolved;
		}

		if (!folder.empty())
		{
			for (const auto& f : listWavFiles(folder))
				files.push_back(f);
		}
		else
		{
			files.push_back(item);
		}

		if (files.size() >= (size_t)kMaxFrames)
			break;
	}

	if (files.empty())
		return {};

	std::string key = "list:";
	for (const auto& f : files)
		key += JmUnicodeConversions::WStringToUtf8(f) + "\n";

	if (auto cached = findCached(key))
		return cached;

	std::vector<std::vector<uint8_t>> contents;
	for (const auto& f : files)
	{
		std::vector<uint8_t> bytes;
		std::string fullName;
		if (readFile(f, bytes, fullName))
			contents.push_back(std::move(bytes));
	}

	std::string error;
	int used = 0;
	return loadWavList(contents, key, error, used);
}

void WavetableOsc::loadTable()
{
	std::shared_ptr<const Wavetable> newTable;

	const int choice = pinTable.getValue();

	if (choice == kWavFileTable && !pinFile.getValue().empty())
	{
		std::vector<uint8_t> bytes;
		std::string fullName;
		if (readFile(pinFile.getValue(), bytes, fullName))
		{
			std::string error;
			newTable = loadWav(bytes, "file:" + fullName, error);
		}
	}
	else if (choice == kFileListTable && !pinFileList.getValue().empty())
	{
		newTable = loadFileList(pinFileList.getValue());
	}

	if (!newTable)
	{
		// Built-in table, or fallback when the file(s) are missing/unreadable.
		newTable = getBuiltIn(choice >= NumBuiltIns ? Classic : choice);
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
	if (pinTable.isUpdated() || pinFile.isUpdated() || pinFileList.isUpdated() || !table_)
	{
		loadTable();
	}

	if (pinVoices.isUpdated())
	{
		numVoices_ = (std::max)(1, (std::min)((int)pinVoices.getValue(), kMaxVoices));
		lastDetune_ = -1.f; // force unison recalculation
		resetPhases();
	}

	if (!pinPitch.isStreaming())
	{
		increment_ = pitchToIncrement(pinPitch.getValue());
	}

	if (pinOn.getValue())
	{
		if (!wasOn_)
		{
			resetPhases();
			wasOn_ = true;
		}
		pinLeft.setStreaming(true);
		pinRight.setStreaming(true);
		SET_PROCESS2(&WavetableOsc::subProcess);
	}
	else
	{
		wasOn_ = false;
		pinLeft.setStreaming(false);
		pinRight.setStreaming(false);
		SET_PROCESS2(&WavetableOsc::subProcessOff);
	}
}

void WavetableOsc::subProcessOff(int sampleFrames)
{
	float* outL = getBuffer(pinLeft);
	float* outR = getBuffer(pinRight);
	for (int s = 0; s < sampleFrames; ++s)
	{
		outL[s] = 0.f;
		outR[s] = 0.f;
	}
}

void WavetableOsc::subProcess(int sampleFrames)
{
	const float* pitch = getBuffer(pinPitch);
	const float* position = getBuffer(pinPosition);
	const float* phaseIn = getBuffer(pinPhase);
	const float* sync = getBuffer(pinSync);
	float* outL = getBuffer(pinLeft);
	float* outR = getBuffer(pinRight);

	const Wavetable* wt = table_.get();
	if (!wt || wt->frames < 1)
	{
		subProcessOff(sampleFrames);
		return;
	}

	// Unison detune / width / pan are updated once per block.
	{
		const float detune = getBuffer(pinDetune)[0];
		const float width = getBuffer(pinWidth)[0];
		const float pan = getBuffer(pinPan)[0];
		if (detune != lastDetune_ || width != lastWidth_ || pan != lastPan_)
			updateUnison(detune, width, pan);
	}

	const bool pitchModulated = pinPitch.isStreaming();
	const int lastFrame = wt->frames - 1;
	const int numVoices = numVoices_;
	const float outGain = kOutputLevel * unisonGain_;

	float increment = increment_;
	float baseX = levelX(increment);
	float prevSync = prevSync_;
	float pos = lastPosition_;

	for (int s = 0; s < sampleFrames; ++s)
	{
		if (pitchModulated)
		{
			increment = pitchToIncrement(pitch[s]);
			baseX = levelX(increment);
		}

		// hard sync on rising edge
		const float sy = sync[s];
		if (sy > 0.f && prevSync <= 0.f)
		{
			for (int v = 0; v < numVoices; ++v)
				voices_[v].phase = voices_[v].startPhase;
		}
		prevSync = sy;

		// frame position, 0..10 V
		pos = position[s];
		pos = pos > 0.f ? (pos < 1.f ? pos : 1.f) : 0.f;
		const float fpos = pos * lastFrame;
		const int f0 = (int)fpos;
		const int f1 = f0 < lastFrame ? f0 + 1 : lastFrame;
		const float ft = fpos - f0;

		// phase offset, 0..10 V = one cycle
		const float phaseOffset = phaseIn[s];

		float left = 0.f, right = 0.f;
		for (int v = 0; v < numVoices; ++v)
		{
			Voice& voice = voices_[v];

			float inc = increment * voice.ratio;
			if (inc > 0.45f)
				inc = 0.45f;
			const int level = levelFromX(baseX + voice.levelOffset);

			float readPhase = voice.phase + phaseOffset;
			readPhase -= floorf(readPhase);

			const int size = wt->levelSize[level];
			const float idx = readPhase * size;
			int i = (int)idx;
			if (i >= size) i = size - 1; // guards float rounding at the very end of the cycle
			const float t = idx - i;

			// both frames at the same mip level, Hermite interpolation, then morph
			const float va = hermite(wt->level(f0, level) + i, t);
			const float vb = hermite(wt->level(f1, level) + i, t);
			const float value = va + ft * (vb - va);

			left += value * voice.gainL;
			right += value * voice.gainR;

			voice.phase += inc;
			if (voice.phase >= 1.f)
				voice.phase -= 1.f;
		}

		outL[s] = left * outGain;
		outR[s] = right * outGain;
	}

	increment_ = increment;
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
