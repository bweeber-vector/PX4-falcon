// SPDX-License-Identifier: BSD-3-Clause
// Standalone: c++ -std=c++17 -Wall -Wextra -Werror -I. test/vector_hud_test.cpp -o /tmp/vector-hud-test
#include "src/drivers/osd/atxxxx/vector_hud.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <string>

using namespace vector_osd;

static std::string row(const char (&screen)[PAL_ROWS][COLUMNS], unsigned y)
{
	return std::string(screen[y], COLUMNS);
}

int main()
{
	char code[12];
	assert(plus_code(47.36559, 8.524997, code));
	assert(strcmp(code, "8FVC9G8F+6X") == 0);
	assert(plus_code(0, 0, code) && strcmp(code, "6FG22222+22") == 0);
	assert(plus_code(-90, -180, code) && strcmp(code, "22222222+22") == 0);
	assert(plus_code(90, 180, code) && strcmp(code, "C2X2X2X2+X2") == 0);
	assert(plus_code(0, 360, code) && strcmp(code, "6FG22222+22") == 0);
	assert(!plus_code(NAN, 0, code) && strcmp(code, "--------+--") == 0);
	assert(!plus_code(91, 0, code));

	// Vector's font arrows run in the opposite direction to the generic font.
	assert(direction(0) == 0x68);
	assert(direction(PI / 2) == 0x64);
	assert(direction(PI) == 0x60);
	assert(direction(-PI / 2) == 0x6c);
	assert(direction(2 * PI) == 0x68);

	struct Guarded {
		std::array<unsigned char, 32> before;
		char screen[PAL_ROWS][COLUMNS];
		std::array<unsigned char, 32> after;
	} g;
	g.before.fill(0xa5);
	g.after.fill(0x5a);
	HudData d;
	render_hud(g.screen, d, true);
	assert(row(g.screen, 7).substr(11, 8) == "ATT WAIT");
	assert(row(g.screen, 0).substr(27, 2) == "--");
	assert(row(g.screen, 14).substr(11, 11) == "--------+--");
	assert(row(g.screen, 14).substr(25, 4) == "----");
	assert(g.screen[2][13] == '-'); // Missing home bearing is explicitly unavailable.
	assert(g.screen[7][24] == ' '); // No misleading unknown-capacity glyph.
	d.cell_voltage = 3.85f;
	render_hud(g.screen, d, true);
	assert(row(g.screen, 7).substr(24, 5) == " 3.85");
	assert(uint8_t(g.screen[7][29]) == glyph::VOLT);


	d.roll = 0; d.pitch = 0; d.yaw = 0;
	d.north_speed = 10; d.east_speed = 0; d.climb = -1.5f; d.altitude = 123;
	d.home_distance = 100; d.home_bearing = 0;
	d.current = 12.3f; d.cell_voltage = 3.85f; d.remaining = .6f;
	d.rssi = 87; d.link_quality = 100; d.satellites = 20; d.gps_fix = true;
	d.mode = "STAB"; d.status_valid = true;
	d.flight_seconds = 125; d.distance = 800;
	d.callsign = "HAMMER"; d.plus_code = "8FVC9G8F+6X"; d.vtx_power = "25";
	d.message = "PREARM CHECKS: NOT READY";
	render_hud(g.screen, d, true);
	assert(row(g.screen, 0).substr(1, 6) == "  2:05");
	assert(uint8_t(g.screen[0][15]) == 0x18); // north centred on compass
	assert(uint8_t(g.screen[1][1]) == 0x68); // forward groundspeed arrow
	assert(row(g.screen, 1).substr(2, 3) == " 36"); // m/s to km/h
	assert(row(g.screen, 9).substr(1, 4) == " 123");
	assert(uint8_t(g.screen[8][2]) == 0xaa); // NED converted to descent
	assert(uint8_t(g.screen[7][11]) == 0x84); // level horizon centre subcell
	assert(uint8_t(g.screen[7][14]) == 0x72);
	assert(uint8_t(g.screen[7][15]) == 0x73);
	assert(uint8_t(g.screen[7][16]) == 0x74);
	assert(row(g.screen, 7).substr(25, 4) == "3.85");
	assert(row(g.screen, 8).substr(25, 4) == "12.3");
	assert(row(g.screen, 13).substr(25, 4) == "STAB");
	assert(uint8_t(g.screen[13][29]) == 0xba);
	assert(row(g.screen, 14).substr(25, 4) == "  25");
	assert(uint8_t(g.screen[14][29]) == 0xaf);

	// No embedded-NUL truncation in the character writer.
	HudCanvas canvas(g.screen, true);
	canvas.text(0, 15, "A%cB", 0);
	assert(g.screen[15][0] == 'A' && g.screen[15][1] == 0 && g.screen[15][2] == 'B');

	d.armed = true;
	render_hud(g.screen, d, false);
	assert(row(g.screen, 11).substr(25, 4) == "STAB");
	assert(uint8_t(g.screen[11][29]) == 0xbb);
	assert(row(g.screen, 12).substr(11, 11) == "8FVC9G8F+6X");
	assert(uint8_t(g.screen[9][22]) == glyph::DISTANCE);
	assert(row(g.screen, 10).substr(2, 24) == "PREARM CHECKS: NOT READY");

	for (unsigned i = NTSC_ROWS; i < PAL_ROWS; ++i) { assert(row(g.screen, i) == std::string(COLUMNS, ' ')); }

	// Pitch up moves the horizon down. Roll right raises the right-hand side.
	d.pitch = 10 * DEG_TO_RAD;
	render_hud(g.screen, d, true);
	assert(uint8_t(g.screen[9][11]) == 0x84);
	d.pitch = 0; d.roll = 30 * DEG_TO_RAD;
	render_hud(g.screen, d, true);
	assert(uint8_t(g.screen[5][19]) >= 0x80 && uint8_t(g.screen[5][19]) <= 0x88);

	// Exhaust steep/inverted attitudes and both formats with buffer guards.
	for (bool pal : {true, false}) {
		for (int degrees = -180; degrees <= 180; ++degrees) {
			d.roll = degrees * DEG_TO_RAD;
			d.pitch = degrees * DEG_TO_RAD;
			render_hud(g.screen, d, pal);
		}
	}
	assert(std::all_of(g.before.begin(), g.before.end(), [](auto v) { return v == 0xa5; }));
	assert(std::all_of(g.after.begin(), g.after.end(), [](auto v) { return v == 0x5a; }));

	// Warning blink must blank the original glyphs rather than leave stale data.
	d.roll = 0; d.pitch = 0; d.rssi = 20; d.cell_voltage = 3.4f;
	d.satellites = 5; d.gps_fix = false; d.blink_visible = false;
	render_hud(g.screen, d, true);
	assert(row(g.screen, 1).substr(26, 4) == "    ");
	assert(row(g.screen, 7).substr(24, 6) == "      ");
	assert(row(g.screen, 14).substr(0, 4) == "    ");
	return 0;
}
