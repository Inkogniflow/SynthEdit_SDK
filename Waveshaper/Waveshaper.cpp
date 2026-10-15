#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <string.h>
#include <algorithm>
#include "./Waveshaper.h"
#include "./WaveshaperDisplayFormat.h"
#include "../shared/unicode_conversion.h"

using namespace sxws;

REGISTER_PLUGIN2(Waveshaper, L"Sxynthedit Waveshaper");

namespace
{
	constexpr int kCustomTable = NumBuiltIns; // enum entry after the built-ins
	constexpr float kMaxY = 4.f;              // +/-20 V, keeps runaway formulas bounded

	inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

Waveshaper::Waveshaper()
{
	// Order must match the <Audio> pins in Waveshaper.xml.
	initializePin(pinInL);
	initializePin(pinInR);
	initializePin(pinPosition);
	for (auto& p : pinVar)
		initializePin(p);
	initializePin(pinMix);
	initializePin(pinTable);
	initializePin(pinFunctions);
	initializePin(pinOversampling);
	initializePin(pinDcBlock);
	initializePin(pinOutL);
	initializePin(pinOutR);
	initializePin(pinDisplayTable);
	initializePin(pinDisplayCurve);
}

int32_t Waveshaper::open()
{
	sampleRate_ = getSampleRate();
	curveInterval_ = (std::max)(64, (int)(sampleRate_ / 30.f));
	tableInterval_ = (std::max)(64, (int)(sampleRate_ / 10.f));
	dcCoeff_ = 1.f - (float)(2.0 * M_PI * 8.0 / sampleRate_); // ~8 Hz high-pass
	setOversampling(1);

	SET_PROCESS2(&Waveshaper::subProcess);
	return MpBase2::open();
}

void Waveshaper::setOversampling(int factor)
{
	for (auto& o : os_)
		o.setFactor(factor);
	factor_ = os_[0].factor();
	latency_ = os_[0].latency();
	for (auto& d : dryDelay_) d.reset();
	for (auto& d : varDelay_) d.reset();
	posDelay_.reset();
}

void Waveshaper::loadFunctions()
{
	const int choice = pinTable.getValue();
	std::shared_ptr<const FunctionSet> newSet;

	if (choice >= kCustomTable)
		newSet = parseFunctionList(JmUnicodeConversions::WStringToUtf8(pinFunctions.getValue()));
	else
		newSet = getBuiltIn(choice < 0 ? 0 : choice);

	if (newSet != set_)
	{
		set_ = newSet;
		tablePending_ = true;
		tableCountdown_ = 0;
		curveCountdown_ = 0;
		sentPosition_ = -1.f;
	}
}

void Waveshaper::onSetPins()
{
	if (pinTable.isUpdated() || pinFunctions.isUpdated() || !set_)
		loadFunctions();

	if (pinOversampling.isUpdated())
	{
		static const int kFactors[] = { 1, 2, 4, 8 };
		const int i = (std::max)(0, (std::min)(3, (int)pinOversampling.getValue()));
		if (kFactors[i] != factor_)
			setOversampling(kFactors[i]);
	}

	pinOutL.setStreaming(true);
	pinOutR.setStreaming(true);
}

// Evaluates the table at the per-sample positions fpos (0..count-1) and morphs between neighbours.
void Waveshaper::shapeChannel(const float* x, float* y, int m, const float* const* vars, const float* fpos)
{
	const auto& fns = set_->functions;
	const int count = (int)fns.size();
	const int last = count - 1;

	if (count == 1)
	{
		fns[0].program.eval(x, vars, y, m);
		return;
	}

	int start = 0;
	while (start < m)
	{
		const int f0 = (std::min)((int)fpos[start], last);
		int end = start + 1;
		bool morph = fpos[start] - f0 > 0.f;
		while (end < m && (std::min)((int)fpos[end], last) == f0)
		{
			morph = morph || fpos[end] - f0 > 0.f;
			++end;
		}
		const int len = end - start;

		const float* subVars[kNumVars];
		for (int v = 0; v < kNumVars; ++v)
			subVars[v] = vars[v] + start;

		fns[f0].program.eval(x + start, subVars, y + start, len);

		if (morph && f0 < last)
		{
			fns[f0 + 1].program.eval(x + start, subVars, tmp2_, len);
			for (int i = 0; i < len; ++i)
			{
				const float t = fpos[start + i] - f0;
				y[start + i] += t * (tmp2_[i] - y[start + i]);
			}
		}
		start = end;
	}
}

void Waveshaper::processChunk(int offset, int n)
{
	const int L = factor_;
	const int m = n * L;
	const int d = latency_;
	const int count = (int)set_->functions.size();

	const float* in[2] = { getBuffer(pinInL) + offset, getBuffer(pinInR) + offset };
	const float* position = getBuffer(pinPosition) + offset;
	const float* mix = getBuffer(pinMix) + offset;
	float* out[2] = { getBuffer(pinOutL) + offset, getBuffer(pinOutR) + offset };

	// 1. input -> x units (5 V = 1), peak for the display, upsample
	for (int ch = 0; ch < 2; ++ch)
	{
		float pk = peak_[ch];
		for (int k = 0; k < n; ++k)
		{
			const float xv = in[ch][k] * 2.f;
			tmp_[k] = xv;
			const float a = fabsf(xv);
			if (a > pk) pk = a;
		}
		peak_[ch] = pk;
		os_[ch].upsample(tmp_, n, xs_[ch]);
	}

	// 2. A..D: clamp to -5..+5 V, normalise to -1..+1, delay by the up-sampler latency, interpolate to the oversampled rate
	const unsigned mask = set_->varMask;
	for (int v = 0; v < kNumVars; ++v)
	{
		const float* src = getBuffer(pinVar[v]) + offset;
		float prev = prevVar_[v];
		for (int k = 0; k < n; ++k)
		{
			const float cur = varDelay_[v].process(clampf(src[k] * 2.f, -1.f, 1.f), d);
			if (mask & (1u << v))
			{
				if (L == 1)
					varOs_[v][k] = cur;
				else
				{
					const float step = (cur - prev) / L;
					for (int p = 0; p < L; ++p)
						varOs_[v][k * L + p] = prev + step * (p + 1);
				}
			}
			prev = cur;
		}
		prevVar_[v] = prev;
		lastVars_[v] = prev;
	}

	// 3. position (held per base sample)
	float pos = lastPosition_;
	for (int k = 0; k < n; ++k)
	{
		pos = posDelay_.process(clampf(position[k], 0.f, 1.f), d);
		const float fp = pos * (count - 1);
		for (int p = 0; p < L; ++p)
			fposOs_[k * L + p] = fp;
	}
	lastPosition_ = pos;

	// 4. shape, sanitise, back to volts, downsample
	const float* vars[kNumVars] = { varOs_[0], varOs_[1], varOs_[2], varOs_[3] };
	const bool dcBlock = pinDcBlock.getValue();
	for (int ch = 0; ch < 2; ++ch)
	{
		shapeChannel(xs_[ch], ys_[ch], m, vars, fposOs_);

		float* y = ys_[ch];
		for (int i = 0; i < m; ++i)
		{
			const float v = y[i];
			y[i] = (v > -kMaxY && v < kMaxY) ? v * 0.5f : (v >= kMaxY ? kMaxY * 0.5f : (v <= -kMaxY ? -kMaxY * 0.5f : 0.f)); // NaN -> 0
		}

		os_[ch].downsample(y, n, wet_[ch]);

		if (dcBlock)
		{
			float x1 = dcX1_[ch], y1 = dcY1_[ch];
			for (int k = 0; k < n; ++k)
			{
				const float xv = wet_[ch][k];
				y1 = xv - x1 + dcCoeff_ * y1;
				x1 = xv;
				wet_[ch][k] = y1;
			}
			// flush denormals
			if (fabsf(y1) < 1.0e-20f) y1 = 0.f;
			dcX1_[ch] = x1;
			dcY1_[ch] = y1;
		}

		// the anti-alias filter and the DC blocker can overshoot a clipped signal
		for (int k = 0; k < n; ++k)
			wet_[ch][k] = clampf(wet_[ch][k], -kMaxY * 0.5f, kMaxY * 0.5f);

		// 5. dry/wet, dry delayed by the oversampler latency
		for (int k = 0; k < n; ++k)
		{
			const float dry = dryDelay_[ch].process(in[ch][k], d);
			const float w = clampf(mix[k], 0.f, 1.f);
			out[ch][k] = dry + w * (wet_[ch][k] - dry);
		}
	}
}

void Waveshaper::subProcess(int sampleFrames)
{
	if (!set_)
		loadFunctions();

	for (int done = 0; done < sampleFrames; done += kChunk)
		processChunk(done, (std::min)(kChunk, sampleFrames - done));

	// display, throttled
	tableCountdown_ -= sampleFrames;
	curveCountdown_ -= sampleFrames;

	if (tablePending_ || tableCountdown_ <= 0)
	{
		bool varsMoved = false;
		for (int v = 0; v < kNumVars; ++v)
			if ((set_->varMask & (1u << v)) && fabsf(lastVars_[v] - tableVars_[v]) > 0.002f)
				varsMoved = true;

		if (tablePending_ || varsMoved)
		{
			sendDisplayTable();
			tableCountdown_ = tableInterval_;
		}
	}

	if (curveCountdown_ <= 0)
	{
		bool changed = fabsf(lastPosition_ - sentPosition_) > 0.0005f;
		for (int v = 0; v < kNumVars; ++v)
			if (fabsf(lastVars_[v] - sentVars_[v]) > 0.002f)
				changed = true;
		for (int ch = 0; ch < 2; ++ch)
			if (fabsf(peak_[ch] - sentPeak_[ch]) > 0.005f)
				changed = true;

		if (changed)
			sendDisplayCurve();
		curveCountdown_ = curveInterval_;
		peak_[0] = peak_[1] = 0.f; // new peak window
	}
}

void Waveshaper::computeDisplayCurve(float position, float* y, int points)
{
	const auto& fns = set_->functions;
	const int count = (int)fns.size();

	float xs[kDisplayCurvePoints];
	float vbuf[kNumVars][kDisplayCurvePoints];
	const float* vars[kNumVars];
	points = (std::min)(points, (int)kDisplayCurvePoints);
	for (int i = 0; i < points; ++i)
		xs[i] = -1.f + 2.f * i / (points - 1);
	for (int v = 0; v < kNumVars; ++v)
	{
		std::fill(vbuf[v], vbuf[v] + points, lastVars_[v]);
		vars[v] = vbuf[v];
	}

	const float fp = clampf(position, 0.f, 1.f) * (count - 1);
	const int f0 = (std::min)((int)fp, count - 1);
	const float t = fp - f0;
	fns[f0].program.eval(xs, vars, y, points);
	if (t > 0.f && f0 + 1 < count)
	{
		float y1[kDisplayCurvePoints];
		fns[f0 + 1].program.eval(xs, vars, y1, points);
		for (int i = 0; i < points; ++i)
			y[i] += t * (y1[i] - y[i]);
	}
}

void Waveshaper::sendDisplayTable()
{
	tablePending_ = false;
	for (int v = 0; v < kNumVars; ++v)
		tableVars_[v] = lastVars_[v];

	const auto& fns = set_->functions;
	const int count = (int)fns.size();
	const int points = kDisplayTablePoints;

	size_t size = sizeof(TableBlobHeader) + (size_t)count * points;
	for (const auto& f : fns)
		size += sizeof(FunctionRecord) + f.name.size() + f.formula.size() + f.program.error().size();
	blob_.resize(size);

	TableBlobHeader h{};
	h.magic = kTableMagic;
	h.version = kFormatVersion;
	h.count = (uint16_t)count;
	h.points = (uint16_t)points;
	h.serial = set_->serial;
	uint8_t* p = blob_.data();
	memcpy(p, &h, sizeof(h));
	p += sizeof(h);

	float xs[kDisplayTablePoints], ys[kDisplayTablePoints];
	float vbuf[kNumVars][kDisplayTablePoints];
	const float* vars[kNumVars];
	for (int i = 0; i < points; ++i)
		xs[i] = -1.f + 2.f * i / (points - 1);
	for (int v = 0; v < kNumVars; ++v)
	{
		std::fill(vbuf[v], vbuf[v] + points, lastVars_[v]);
		vars[v] = vbuf[v];
	}

	for (const auto& f : fns)
	{
		f.program.eval(xs, vars, ys, points);
		for (int i = 0; i < points; ++i)
		{
			const float v = std::isfinite(ys[i]) ? ys[i] : 0.f;
			*p++ = (uint8_t)(int8_t)clampf(roundf(v * kTableScale), -127.f, 127.f);
		}
	}

	for (const auto& f : fns)
	{
		FunctionRecord r{};
		r.varMask = (uint8_t)f.program.varMask();
		r.errorPos = (int16_t)(std::min)(f.program.errorPos(), 32767);
		for (int v = 0; v < kNumVars; ++v)
		{
			float lo = -1.f, hi = 1.f;
			f.program.varRange(v, lo, hi);
			r.lo[v] = lo;
			r.hi[v] = hi;
		}
		r.nameBytes = (uint16_t)f.name.size();
		r.formulaBytes = (uint16_t)f.formula.size();
		r.errorBytes = (uint16_t)f.program.error().size();
		memcpy(p, &r, sizeof(r));
		p += sizeof(r);
		memcpy(p, f.name.data(), f.name.size());
		p += f.name.size();
		memcpy(p, f.formula.data(), f.formula.size());
		p += f.formula.size();
		memcpy(p, f.program.error().data(), f.program.error().size());
		p += f.program.error().size();
	}

	pinDisplayTable.setValueRaw(blob_.size(), blob_.data());
	pinDisplayTable.sendPinUpdate(getBlockPosition());

	sentPosition_ = -1.f; // the curve depends on the same values
	curveCountdown_ = 0;
}

void Waveshaper::sendDisplayCurve()
{
	const int points = kDisplayCurvePoints;
	float curve[kDisplayCurvePoints];
	computeDisplayCurve(lastPosition_, curve, points);

	blob_.resize(sizeof(CurveBlobHeader) + points * sizeof(int16_t));

	CurveBlobHeader h{};
	h.magic = kCurveMagic;
	h.version = kFormatVersion;
	h.points = (uint16_t)points;
	h.position = lastPosition_;
	h.count = (uint16_t)set_->functions.size();
	h.serial = set_->serial;
	for (int v = 0; v < kNumVars; ++v)
		h.vars[v] = lastVars_[v];
	h.peakL = peak_[0];
	h.peakR = peak_[1];
	memcpy(blob_.data(), &h, sizeof(h));

	int16_t samples[kDisplayCurvePoints];
	for (int i = 0; i < points; ++i)
	{
		const float v = std::isfinite(curve[i]) ? curve[i] : 0.f;
		samples[i] = (int16_t)clampf(roundf(v * kCurveScale), -32767.f, 32767.f);
	}
	memcpy(blob_.data() + sizeof(h), samples, sizeof(samples));

	pinDisplayCurve.setValueRaw(blob_.size(), blob_.data());
	pinDisplayCurve.sendPinUpdate(getBlockPosition());

	sentPosition_ = lastPosition_;
	for (int v = 0; v < kNumVars; ++v)
		sentVars_[v] = lastVars_[v];
	sentPeak_[0] = peak_[0];
	sentPeak_[1] = peak_[1];
}
