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

#ifndef CHAMBER_AMIGA_H
#define CHAMBER_AMIGA_H

#include "chamber/renderer.h"

namespace Chamber {

// The Amiga "Kult" port shares the same logical resources, screen geometry and
// planar 4-bitplane / 16-colour graphics format as the EGA build, so the Amiga
// renderer reuses the whole EGA chunky-8bpp pipeline and only differs in the
// colour palette: the Amiga uses a table of 31 custom 12-bit RGB palettes stored
// inside the KULT executable, selected per zone/room, instead of the fixed EGA
// palette.
class AmigaRenderer : public EGARenderer {
public:
	void switchToGraphicsMode() override;
	// On Amiga, `csel` is the real palette index into the 31-entry table
	// (see selectPalette()/selectSpecificPalette() in room.cpp), not a CGA
	// colour-select byte.
	void colorSelect(byte csel) override;
	// SOURI.BIN uses the same two-plane cursor layout as SOURI.EGA but with
	// big-endian plane words, so the Amiga needs its own decode.
	void selectCursor(uint16 num) override;
};

// Number of 16-colour palettes packed after the static resources in KULT.
#define AMIGA_NUM_PALETTES 31

// Pointer to the 31×16 12-bit RGB palette table inside the loaded KULT image
// (set up by loadAmigaStaticData()). This is the title fade ramp.
extern const byte *amiga_palette_table;

// Pointer to the Master's-Orbit full 16-colour palette (the one scene that does
// not use the base+delta composition); set up by loadAmigaStaticData().
extern const byte *amiga_room_palette_table;

// Apply one of the title palettes (clamped to a valid index) to the screen.
void amigaApplyPalette(byte index);

// Compose and apply a room palette for the given zone palette_index (0..19),
// reproducing KULT's base-palette + per-index stone-ramp-delta composition.
void amigaApplyRoomPalette(byte index);

// Load the static engine resources by slicing them out of the Amiga KULT
// executable (they are stored uncompressed). Mirrors loadStaticData() for the
// DOS PXI path. Returns 1 on success.
int16 loadAmigaStaticData();

} // End of namespace Chamber

#endif // CHAMBER_AMIGA_H
