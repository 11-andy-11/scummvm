/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/file.h"
#include "common/system.h"
#include "graphics/paletteman.h"

#include "chamber/chamber.h"
#include "chamber/common.h"
#include "chamber/amiga.h"
#include "chamber/resdata.h"

namespace Chamber {

const byte *amiga_palette_table = nullptr;
const byte *amiga_room_palette_table = nullptr;

// ---------------------------------------------------------------------------
// Amiga static-resource layout inside the EU "Kult" KULT executable.
//
// The 12 static resources are stored uncompressed as raw byte slices of the
// 68000 Hunk executable (verified bit-for-bit against the reference extract).
// These offsets are for the EU build and are shared by every EU variant and
// its cracks (only the corrupt dumps and the US "Chamber" build differ; US is
// handled separately later). The 31×16 12-bit RGB palette table immediately
// follows the last resource (LUTIN).
// ---------------------------------------------------------------------------
struct AmigaResEnt {
	const char *name;     // for diagnostics
	byte **buffer;        // engine global to point at the slice
	uint32 offset;        // file offset inside KULT
	uint32 size;          // resource length
};

#define AMIGA_PAL_OFFSET 122836   // == end of LUTIN (120036 + 2800)

// The 122836 table is the title's fade ramp, NOT the room palettes.
//
// Room palettes are COMPOSED at run time, exactly as the KULT 68000 code does
// (reverse-engineered from the disassembly). The key routine is at hunk0 0x5ab6:
//     move.b 0xa88a,d0  ; palette_index
//     mulu.w #10,d0     ; lea 0x9c60,a0 ; adda d0,a0   ; -> &delta[index]
//     lea 0x9fbc,a1     ; addq #2,a1                   ; -> &livepal[slot 1]
//     move.w #4,d0      ; copy 5 words (a0)+ -> (a1)+  ; then LoadRGB4
// i.e. a room overwrites ONLY live-palette slots 1..5 with a 5-word "stone ramp"
// DELTA selected by palette_index (table at hunk0 0x9c60, file offset 40068, 20
// entries of 5 big-endian 0x0RGB words). Slots 0 and 6..15 come from a PERSISTENT
// base palette and are shared by every room - which is why all rooms share the
// same accents (black bg, crevice, torch, peach dialog box, portrait bg, white)
// but each room type has its own wall hue. The only full-16-colour palette write
// is the Master's-Orbit special scene (movea.l #0x9d28,a0 at hunk0 0x8398, file
// offset 40268 = amiga_room_palette_table).
//
// amigaApplyRoomPalette() reproduces this base+delta composition. The per-room
// stone-ramp delta (slots 1..5) is read straight from the exe; the shared accent
// base (slots 0, 6..15) uses the reference-verified values in amiga_room_base_pal
// below rather than the exe's raw static base. Same reason as the stone ramp's
// hue choice: this OCS floppy dump's static accent colours (e.g. an orange
// slot-13 portrait background) do not match the reference footage (dark-blue
// portrait background), and the goal is to match the original game's on-screen
// look. The exe values for the differing accent slots are noted inline.
#define AMIGA_ROOM_PAL_OFFSET 40268      // hunk0 0x9d28: Master's-Orbit full palette
#define AMIGA_ROOM_DELTA_OFFSET 40068    // hunk0 0x9c60: per-index stone-ramp delta
#define AMIGA_NUM_ROOM_PALETTES 20       // delta-table entries (palette_index 0..19)

static const byte *amiga_room_delta_table = nullptr; // 20 x 5 BE 0x0RGB words

// WORKAROUND: FIXME: shared accent base palette (slots 0, 6..15) is hand-authored
// here instead of read from the exe. The real values are in KULT (static init of
// the live palette buffer at hunk0 0x9fbc, file offset 40928), but this OCS dump's
// accent colours do not match the reference footage (e.g. its slot-13 portrait
// background is orange 0xea8, the footage is dark blue 0x224), so these are tuned
// to the reference instead. Derived empirically (2026-06-27) by dumping the
// 320x200 palette-index buffer for the Master's-Orbit room with the master's
// dialogue + portrait on screen and reading each index's colour back against the
// longplay reference (the dialogue state is what exposes the dialog-box (11) /
// portrait-bg (13) / text-box (15) indices). The exe's own value is noted per slot.
// Slots 1..5 are placeholders: always overwritten by the per-room exe stone-ramp
// delta, so their values here are unused. 4-bit 0x0RGB.
static const uint16 amiga_room_base_pal[16] = {
	0x000, // 0  black (background)
	0x000, // 1  stone  } slots 1..5 overwritten by the per-room exe delta
	0x000, // 2  stone  }
	0x000, // 3  stone  }
	0x000, // 4  stone  }
	0x000, // 5  stone  }
	0x101, // 6  crevice (near black, very common)      [exe base: 0x000]
	0x100, // 7  dark detail                            [exe base: 0xe14]
	0x886, // 8  stone/sprite highlight
	0x64A, // 9  blue-purple accent
	0x700, // 10 dark red                               [exe base: 0x800]
	0xEA8, // 11 peach (dialog boxes)                   [exe base: 0xa61]
	0xD66, // 12 red (medallion accent)                 [exe base: 0xe68]
	0x446, // 13 slate blue (portrait background)       [exe base: 0xea8; sampled (68,68,102) from footage]
	0xA8B, // 14 light mauve                            [exe base: 0xa8c]
	0xFFF  // 15 white (text box / highlights)
};

static AmigaResEnt amiga_res[] = {
	{ "SOUCO.BIN", &souco_data,  37724,   424 },
	{ "ZONES.BIN", &zones_data,  43250,  9014 },
	{ "TEMPL.BIN", &templ_data,  52318, 27336 },
	{ "MURSM.BIN", &mursm_data,  79654,    76 },
	{ "ANIMA.BIN", &anima_data,  79730,  2046 },
	{ "ANICO.BIN", &anico_data,  81776,   667 },
	{ "ARPLA.BIN", &arpla_data, 105612,  8024 },
	{ "CARAC.BIN", &carpc_data, 113636,   384 },
	{ "GAUSS.BIN", &gauss_data, 114148,  2880 },
	{ "ALEAT.BIN", &aleat_data, 117028,   256 },
	{ "ICONE.BIN", &icone_data, 117284,  2752 },
	{ "LUTIN.BIN", &lutin_data, 120036,  2800 },
};
static const int kAmigaNumRes = sizeof(amiga_res) / sizeof(amiga_res[0]);

int16 loadAmigaStaticData() {
	// Idempotent: this may be called both before the title splash (to make the
	// palette available) and again from loadStaticData().
	if (amiga_palette_table != nullptr)
		return 1;

	Common::File kult;
	if (!kult.open("KULT")) {
		warning("loadAmigaStaticData(): cannot open KULT");
		return 0;
	}

	uint32 sz = kult.size();
	byte *raw = new byte[sz];
	if (kult.read(raw, sz) != sz) {
		warning("loadAmigaStaticData(): short read on KULT");
		delete[] raw;
		return 0;
	}

	// The engine destructor frees _pxiData; reuse it to own the KULT image.
	delete[] g_vm->_pxiData;
	g_vm->_pxiData = raw;

	for (int i = 0; i < kAmigaNumRes; i++) {
		if (amiga_res[i].offset + amiga_res[i].size > sz) {
			warning("loadAmigaStaticData(): %s past EOF", amiga_res[i].name);
			return 0;
		}
		*amiga_res[i].buffer = raw + amiga_res[i].offset;
	}

	amiga_palette_table = raw + AMIGA_PAL_OFFSET;
	amiga_room_palette_table = raw + AMIGA_ROOM_PAL_OFFSET;
	amiga_room_delta_table = raw + AMIGA_ROOM_DELTA_OFFSET;

	// The font (CARAC.BIN) is consumed by the shared EGA printChar via
	// char_xlat_table[*font], a 16-entry table indexed by a 4-bit 1bpp glyph
	// row (0..15). The Amiga CARAC stores the same 4-pixel glyph but shifted
	// one bit left (it occupies bit positions 3..6 of the byte instead of the
	// low nibble), so feeding it raw indexes past the 16-entry table and
	// produces garbage. Re-pack each row into the low nibble so the existing
	// EGA text pipeline renders it correctly. (carpc_data is Amiga-only here.)
	for (uint32 i = 0; i < 384; i++) // CARAC.BIN size (64 chars x 6 rows)
		carpc_data[i] = (carpc_data[i] >> 1) & 0x0F;

	// SOURI (mouse cursor) is a separate file on the Amiga disk, not embedded
	// in KULT. Load it into its own buffer.
	static byte souri_buf[RES_SOURI_MAX];
	if (!loadFile("SOURI.BIN", souri_buf))
		warning("loadAmigaStaticData(): SOURI.BIN not found");
	souri_data = souri_buf;

	return 1;
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
void amigaApplyPalette(byte index) {
	if (!amiga_palette_table)
		return;

	// The engine also issues direct colorSelect(0x30) calls (a CGA colour-select
	// byte) which are meaningless on Amiga and fall outside the 0..30 palette
	// range; treat any out-of-range index as a no-op so the current zone palette
	// is preserved instead of being reset.
	if (index >= AMIGA_NUM_PALETTES)
		return;

	const byte *src = amiga_palette_table + index * (16 * 2);
	byte palData[16 * 3];
	for (int c = 0; c < 16; c++) {
		uint16 w = (src[c * 2] << 8) | src[c * 2 + 1];   // big-endian 0x0RGB
		// Expand each 4-bit channel to 8 bits (0..15 -> 0..255).
		palData[c * 3 + 0] = ((w >> 8) & 0xF) * 17;
		palData[c * 3 + 1] = ((w >> 4) & 0xF) * 17;
		palData[c * 3 + 2] = ( w       & 0xF) * 17;
	}
	g_system->getPaletteManager()->setPalette(palData, 0, 16);
}

void amigaApplyRoomPalette(byte index) {
	if (!amiga_room_delta_table)
		return;
	if (index >= AMIGA_NUM_ROOM_PALETTES)
		index = 0;

	// Compose the 16-colour room palette as KULT does (base + per-room delta):
	// the shared accent base (slots 0, 6..15) is the reference-verified
	// amiga_room_base_pal; this room's 5-word stone-ramp delta (exe table 0x9c60,
	// indexed by palette_index, big-endian 0x0RGB) is overlaid onto slots 1..5.
	uint16 pal[16];
	for (int c = 0; c < 16; c++)
		pal[c] = amiga_room_base_pal[c];
	const byte *delta = amiga_room_delta_table + index * (5 * 2);
	for (int c = 0; c < 5; c++)
		pal[1 + c] = (delta[c * 2] << 8) | delta[c * 2 + 1];

	byte palData[16 * 3];
	for (int c = 0; c < 16; c++) {
		uint16 w = pal[c];                               // 4-bit 0x0RGB
		palData[c * 3 + 0] = ((w >> 8) & 0xF) * 17;
		palData[c * 3 + 1] = ((w >> 4) & 0xF) * 17;
		palData[c * 3 + 2] = ( w       & 0xF) * 17;
	}
	g_system->getPaletteManager()->setPalette(palData, 0, 16);
}

// ---------------------------------------------------------------------------
// AmigaRenderer
// ---------------------------------------------------------------------------
void AmigaRenderer::switchToGraphicsMode() {
	// Palette table may not be loaded yet at first call; amigaApplyPalette is a
	// no-op until loadAmigaStaticData() has run.
	amigaApplyPalette(0);
}

void AmigaRenderer::colorSelect(byte csel) {
	// On Amiga, selectPalette()/selectSpecificPalette() pass the real palette
	// index here (see room.cpp), so apply it directly.
	amigaApplyPalette(csel);
}

} // End of namespace Chamber
