// SPDX-License-Identifier: BSD-3-Clause
// Standalone host test: g++ -std=c++17 -I. test/vector_osd_protocol_test.cpp -o /tmp/vector-osd-test
#include "src/drivers/osd/atxxxx/vector_osd.h"
#include <array>
#include <cassert>
#include <algorithm>

int main()
{
	using namespace vector_osd;
	std::array<char, COLUMNS> text{};
	std::array < uint8_t, ROW_BYTES + 2 > bytes{};

	for (size_t i = 0; i < COLUMNS; ++i) { text[i] = static_cast<char>(i + 32); }

	assert(video_mode(true) == 0x48);
	assert(video_mode(false) == 0x08);

	// Decode the emitted register stream independently, including the 255/256
	// address boundary and the last cell in both video standards.
	for (const size_t rows : {PAL_ROWS, NTSC_ROWS}) {
		for (size_t row = 0; row < rows; ++row) {
			bytes.fill(0xa5);
			assert(encode_row(text.data(), row, rows, bytes.data() + 1, ROW_BYTES));
			assert(bytes.front() == 0xa5 && bytes.back() == 0xa5);

			for (size_t col = 0; col < COLUMNS; ++col) {
				const uint8_t *cmd = bytes.data() + 1 + col * 6;
				assert(cmd[0] == 5 && cmd[2] == 6 && cmd[4] == 7);
				assert((unsigned(cmd[1]) * 256 + cmd[3]) == row * COLUMNS + col);
				assert(cmd[5] == static_cast<uint8_t>(text[col]));
			}
		}
	}

	bytes.fill(0xa5);
	assert(!encode_row(text.data(), PAL_ROWS, PAL_ROWS, bytes.data(), ROW_BYTES));
	assert(!encode_row(text.data(), 0, 0, bytes.data(), ROW_BYTES));
	assert(!encode_row(text.data(), 0, PAL_ROWS, bytes.data(), ROW_BYTES - 1));
	assert(!encode_row(nullptr, 0, PAL_ROWS, bytes.data(), ROW_BYTES));
	assert(!encode_row(text.data(), 0, PAL_ROWS, nullptr, ROW_BYTES));
	assert(std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0xa5; }));
	return 0;
}
