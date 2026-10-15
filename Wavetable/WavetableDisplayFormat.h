#pragma once

/*
	Data the Wavetable Osc sends to the Wavetable Display.

	Two blobs, so the large one is only sent when the table changes:

	"Display Table" - overview of all frames (for the 3D view), sent when the table changes.
		TableBlobHeader + int8_t samples[frames * points]   (-127..127 = -1..1)

	"Display Wave" - the frame currently playing (position-interpolated), sent ~30x/s while it changes.
		WaveBlobHeader + int16_t samples[points]            (-32767..32767 = -1..1)
*/

#include <stdint.h>

namespace fvwt
{
	constexpr uint32_t kTableMagic = 0x54575646; // 'FVWT'
	constexpr uint32_t kWaveMagic  = 0x57575646; // 'FVWW'
	constexpr uint16_t kFormatVersion = 1;

	constexpr int kDisplayTableMaxFrames = 64;  // keeps the table blob <= ~8 KB
	constexpr int kDisplayTablePoints    = 128;
	constexpr int kDisplayWavePoints     = 256;

#pragma pack(push, 1)
	struct TableBlobHeader
	{
		uint32_t magic;
		uint16_t version;
		uint16_t frames;       // frames contained in this blob
		uint16_t points;       // points per frame
		uint16_t totalFrames;  // frames in the real wavetable
		uint32_t serial;       // changes whenever the table changes
	};

	struct WaveBlobHeader
	{
		uint32_t magic;
		uint16_t version;
		uint16_t points;
		float    position;     // 0..1
		uint32_t serial;       // matches TableBlobHeader::serial
		uint16_t totalFrames;
		uint16_t reserved;
	};
#pragma pack(pop)
}
