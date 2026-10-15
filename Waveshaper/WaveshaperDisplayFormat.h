#pragma once

/*
	Data the SXE Waveshaper sends to the SXE Waveshaper Display.

	"Display Table" - all functions of the table, sampled with the current A..D values.
		Sent when the table changes, and (throttled) when A..D change and the table uses them.
		TableBlobHeader
		int8_t   curves[count * points]     y over x = -1..+1, 63.5 per unit (-2..+2)
		count x FunctionRecord, each followed by name, formula and error text (UTF-8, not terminated)

	"Display Curve" - the curve currently in use (position-morphed), ~30x/s while something changes.
		CurveBlobHeader
		int16_t  curve[points]              y over x = -1..+1, 16383 per unit (-2..+2)
*/

#include <stdint.h>

namespace sxws
{
	constexpr uint32_t kTableMagic = 0x54575853; // 'SXWT'
	constexpr uint32_t kCurveMagic = 0x43575853; // 'SXWC'
	constexpr uint16_t kFormatVersion = 1;

	constexpr int   kDisplayTablePoints = 128;
	constexpr int   kDisplayCurvePoints = 256;
	constexpr float kTableScale = 63.5f;
	constexpr float kCurveScale = 16383.f;

#pragma pack(push, 1)
	struct TableBlobHeader
	{
		uint32_t magic;
		uint16_t version;
		uint16_t count;      // functions
		uint16_t points;     // points per curve
		uint16_t reserved;
		uint32_t serial;     // changes when the function list changes
	};

	struct FunctionRecord
	{
		uint8_t  varMask;    // bit i = A..D used
		uint8_t  reserved;
		int16_t  errorPos;   // character position of the error, -1 = none
		float    lo[4];      // range of A..D (first use)
		float    hi[4];
		uint16_t nameBytes;
		uint16_t formulaBytes;
		uint16_t errorBytes;
		uint16_t reserved2;
	};

	struct CurveBlobHeader
	{
		uint32_t magic;
		uint16_t version;
		uint16_t points;
		float    position;   // 0..1 over the table
		uint16_t count;      // functions in the table
		uint16_t reserved;
		uint32_t serial;     // matches TableBlobHeader::serial
		float    vars[4];    // A..D, normalised -1..+1 (= -5..+5 V)
		float    peakL;      // input peak since the last update, in x units (1 = 5 V)
		float    peakR;
	};
#pragma pack(pop)
}
