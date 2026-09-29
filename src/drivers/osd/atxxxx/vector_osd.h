/****************************************************************************
 * Copyright (c) 2026 PX4 Development Team. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/
#pragma once

#include <stddef.h>
#include <stdint.h>

// Vector OSD accepts register/value pairs, including multiple pairs per CS.
// It cannot acknowledge writes or supply readable MAX7456 registers.
namespace vector_osd
{
static constexpr size_t COLUMNS = 30;
static constexpr size_t PAL_ROWS = 16;
static constexpr size_t NTSC_ROWS = 13;
static constexpr size_t ROW_BYTES = COLUMNS * 6;
static constexpr uint8_t VM0 = 0x00;
static constexpr uint8_t RESET = 0x02;
static constexpr uint8_t ENABLE = 0x08;
static constexpr uint8_t PAL = 0x40;
static constexpr uint8_t FONT_REGISTER = 0x69;
static constexpr uint8_t ARDUPILOT_FONT = 0xad;

constexpr uint8_t video_mode(bool pal) { return ENABLE | (pal ? PAL : 0); }

inline bool encode_row(const char *characters, size_t row, size_t rows, uint8_t *output, size_t capacity)
{
	if (!characters || !output || row >= rows || (rows != PAL_ROWS && rows != NTSC_ROWS)
	    || capacity < ROW_BYTES) {
		return false;
	}

	for (size_t column = 0; column < COLUMNS; ++column) {
		const uint16_t position = row * COLUMNS + column;
		output[column * 6] = 0x05; // DMAH
		output[column * 6 + 1] = position >> 8;
		output[column * 6 + 2] = 0x06; // DMAL
		output[column * 6 + 3] = position & 0xff;
		output[column * 6 + 4] = 0x07; // DMDI, explicit addressing (no 0xff escape)
		output[column * 6 + 5] = static_cast<uint8_t>(characters[column]);
	}

	return true;
}
}
