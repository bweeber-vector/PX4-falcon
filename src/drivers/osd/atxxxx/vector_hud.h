/****************************************************************************
 * Copyright (c) 2026 PX4 Development Team. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/
#pragma once

#include "vector_osd.h"
#include <cmath>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Independently implemented renderer for the saved Hammer/Vector OSD1 layout.
// Positions and font character codes are the Vector ArduPilot-font interface.
// No ArduPilot implementation code is included here.
namespace vector_osd
{
namespace glyph
{
constexpr uint8_t METRE = 0x0c, KILOMETRE = 0xa0, ALTITUDE = 0xa3;
constexpr uint8_t BATTERY = 0x90, VOLT = 0x06, AMP = 0x9a;
constexpr uint8_t RSSI = 0x01, LQ = 0x7b, SAT_LEFT = 0x1e, SAT_RIGHT = 0x1f;
constexpr uint8_t DEGREE = 0xa5, GROUND_SPEED = 0x70, KMH = 0x9e, MS = 0x9f;
constexpr uint8_t HOME = 0x11, ARROW = 0x60, ARROW_COUNT = 16;
constexpr uint8_t HORIZON_H = 0x80, HORIZON_H_COUNT = 9;
constexpr uint8_t NORTH = 0x18, SOUTH = 0x19, EAST = 0x1a, WEST = 0x1b, TICK = 0x1d, HALF_TICK = 0x1c;
constexpr uint8_t UP_FAST = 0xa9, UP = 0xa8, DOWN = 0xaa, DOWN_FAST = 0xab;
constexpr uint8_t ARMED = 0xbb, DISARMED = 0xba, DISTANCE = 0xac, FLIGHT = 0x9c;
constexpr uint8_t EFFICIENCY = 0xad, MAH = 0x07, MW = 0xaf;
}

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.f;

struct HudData {
	float roll{NAN}, pitch{NAN}, yaw{NAN}; // radians, body FRD relative to NED
	float north_speed{NAN}, east_speed{NAN}, climb{NAN}, altitude{NAN};
	float home_distance{NAN}, home_bearing{NAN}; // bearing relative to heading
	float cell_voltage{NAN}, remaining{NAN}, current{NAN};
	float distance{0.f};
	uint32_t flight_seconds{0};
	int rssi{-1}, link_quality{-1}, satellites{-1};
	bool gps_fix{false}, armed{false}, status_valid{false};
	bool blink_visible{true};
	const char *mode{"----"};
	const char *message{""};
	const char *callsign{""};
	const char *plus_code{"--------+--"};
	const char *vtx_power{"----"}; // last reported power-table label, not a set command
};

inline float wrap_radians(float angle)
{
	float wrapped = fmodf(angle, 2.f * PI);
	return wrapped < 0.f ? wrapped + 2.f * PI : wrapped;
}

inline uint8_t direction(float bearing)
{
	const int sector = int(lroundf(wrap_radians(PI - bearing) * glyph::ARROW_COUNT / (2.f * PI)));
	return glyph::ARROW + sector % glyph::ARROW_COUNT;
}

// Ten-digit Open Location Code: five base-20 latitude/longitude pairs,
// 20 degrees down to 0.000125 degrees; separator after the eighth digit.
inline bool plus_code(double latitude, double longitude, char (&out)[12])
{
	memcpy(out, "--------+--", sizeof(out));

	if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90. || latitude > 90.) {
		return false;
	}

	constexpr double CELL = 0.000125;
	constexpr char ALPHABET[] = "23456789CFGHJMPQRVWX";
	latitude = fmin(latitude, 90. - CELL);
	longitude = fmod(longitude + 180., 360.);

	if (longitude < 0.) { longitude += 360.; }

	// Integer arithmetic avoids cumulative rounding errors between digit pairs.
	uint32_t lat = uint32_t(floor((latitude + 90.) / CELL + 1e-7));
	uint32_t lon = uint32_t(floor(longitude / CELL + 1e-7));
	uint32_t divisor = 160000; // 20^4

	for (unsigned pair = 0; pair < 5; ++pair) {
		const unsigned pos = pair * 2 + (pair == 4 ? 1 : 0);
		out[pos] = ALPHABET[(lat / divisor) % 20];
		out[pos + 1] = ALPHABET[(lon / divisor) % 20];
		divisor /= 20;
	}

	return true;
}

class HudCanvas
{
public:
	HudCanvas(char (&screen)[PAL_ROWS][COLUMNS], bool pal) : _screen(screen), _pal(pal) {}

	void put(int x, int y, uint8_t character)
	{

		if (x >= 0 && x < int(COLUMNS) && y >= 0 && y < int(_pal ? PAL_ROWS : NTSC_ROWS)) {
			_screen[y][x] = char(character);
		}
	}

	void text(int x, int y, const char *format, ...) __attribute__((format(printf, 4, 5)))
	{
		char buffer[COLUMNS + 1] {};
		va_list args;
		va_start(args, format);
		const int length = vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);

		// Use the returned length so all character codes, including zero, survive.
		for (int i = 0; i < length && i < int(COLUMNS); ++i) { put(x + i, y, uint8_t(buffer[i])); }
	}

	void distance(int x, int y, float metres)
	{
		if (!std::isfinite(metres) || metres < 0.f) {
			text(x, y, "----%c", glyph::METRE);

		} else if (metres < 10.f) {
			text(x, y, "%4.1f%c", double(metres), glyph::METRE);

		} else if (metres <= 9999.f) {
			text(x, y, "%4.0f%c", double(metres), glyph::METRE);

		} else {
			const float km = metres * .001f;
			text(x, y, "%.*f%c", km < 99.f ? 2 : (km < 999.f ? 1 : 0), double(km), glyph::KILOMETRE);
		}
	}

	void horizon(float roll, float pitch, bool visible)
	{
		if (!std::isfinite(roll) || !std::isfinite(pitch)) {
			text(11, 7, "ATT WAIT");
			return;
		}

		// Clip the world horizon to a nine-cell square centred on (15, 7).
		// Positive body pitch moves it down; positive roll raises its right side.
		const float aspect = _pal ? 12.f / 15.f : 12.f / 18.46f;
		const float pitch_cells = fmaxf(-20.f * DEG_TO_RAD, fminf(pitch, 20.f * DEG_TO_RAD)) / (5.f * DEG_TO_RAD);
		const float sine = sinf(roll), cosine = cosf(roll);

		if (fabsf(roll) < PI / 2.f || visible) {
			for (int step = -4; step <= 4; ++step) {
				const bool horizontal = fabsf(cosine) > fabsf(sine);
				const float intersection = horizontal ? aspect * step * sine / cosine - pitch_cells :
							   (step + pitch_cells) * cosine / (aspect * sine);
				const float cell = intersection + .5f;
				const int offset = int(floorf(cell));

				if (offset < -4 || offset > 4) { continue; }

				const int count = glyph::HORIZON_H_COUNT;
				const int subcell = int((cell - offset) * count);
				const uint8_t symbol = horizontal ? glyph::HORIZON_H + count - 1 - subcell : '|'; // Vector has no vertical subcell glyphs
				const int row = 7 - (horizontal ? offset : step);

				if (_pal || row <= 9) { put(15 + (horizontal ? step : offset), row, symbol); }
			}
		}

		text(14, 7, "%c%c%c", 0x72, 0x73, 0x74); // fixed aircraft reference
	}

private:
	char (&_screen)[PAL_ROWS][COLUMNS];
	bool _pal;
};

inline void render_hud(char (&screen)[PAL_ROWS][COLUMNS], const HudData &d, bool pal)
{
	memset(screen, ' ', sizeof(screen));
	HudCanvas c(screen, pal);
	c.horizon(d.roll, d.pitch, d.blink_visible);
	c.text(0, 0, "%c%3lu:%02lu", glyph::FLIGHT, (unsigned long)(d.flight_seconds / 60 % 1000),
	       (unsigned long)(d.flight_seconds % 60));

	if (std::isfinite(d.yaw)) {
		const uint8_t cardinal[] = {glyph::NORTH, glyph::EAST, glyph::SOUTH, glyph::WEST};
		const int centre = int(lroundf(wrap_radians(d.yaw) / (PI / 8.f)));

		for (int x = 11; x <= 19; ++x) {
			const int sector = (centre + x - 15 + 16) % 16;
			c.put(x, 0, sector % 4 == 0 ? cardinal[sector / 4] : (sector % 2 == 0 ? glyph::HALF_TICK : glyph::TICK));
		}

		c.text(13, 1, "%3d%c", int(wrap_radians(d.yaw) / DEG_TO_RAD) % 360, glyph::DEGREE);

	} else {
		c.text(11, 0, "---------");
		c.text(13, 1, "---%c", glyph::DEGREE);
	}

	if (d.link_quality >= 0 && d.link_quality <= 100) { c.text(26, 0, "%c%2d", glyph::LQ, d.link_quality); }

	else { c.text(26, 0, "%c--", glyph::LQ); }

	if (d.rssi >= 0 && d.rssi <= 100) {
		if (d.rssi >= 30 || d.blink_visible) { c.text(26, 1, "%c%2d", glyph::RSSI, d.rssi); }

	} else { c.text(26, 1, "%c--", glyph::RSSI); }

	const float speed = hypotf(d.north_speed, d.east_speed);
	const float kmh = speed * 3.6f;

	if (std::isfinite(speed)) {
		const uint8_t arrow = std::isfinite(d.yaw) ? direction(speed > 1.f ? atan2f(d.east_speed, d.north_speed) - d.yaw : 0.f) : '-';

		if (kmh < 9.95f) { c.text(0, 1, "%c%c %1.1f%c", glyph::GROUND_SPEED, arrow, double(kmh), glyph::KMH); }

		else { c.text(0, 1, "%c%c%3.0f%c", glyph::GROUND_SPEED, arrow, double(kmh), glyph::KMH); }

	} else { c.text(0, 1, "%c----%c", glyph::GROUND_SPEED, glyph::KMH); }

	c.put(12, 2, glyph::HOME);
	c.put(13, 2, std::isfinite(d.home_bearing) ? direction(d.home_bearing) : '-');
	c.distance(14, 2, d.home_distance);

	if (std::isfinite(d.altitude)) { c.text(1, 9, "%4.0f%c", double(truncf(d.altitude)), glyph::ALTITUDE); }

	else { c.text(1, 9, "----%c", glyph::ALTITUDE); }

	if (std::isfinite(d.climb)) {
		const uint8_t arrow = d.climb > 3.f ? glyph::UP_FAST : d.climb >= 0.f ? glyph::UP : d.climb >= -3.f ? glyph::DOWN : glyph::DOWN_FAST;
		c.text(2, 8, "%c%.*f%c", arrow, fabsf(d.climb) < 9.95f ? 1 : 0, double(fabsf(d.climb)), glyph::MS);

	} else { c.text(2, 8, "---%c", glyph::MS); }

	if (std::isfinite(d.current) && d.current >= 0.f) {
		c.text(25, 8, "%.*f%c", d.current < 10.f ? 2 : 1, double(d.current), glyph::AMP);

	} else { c.text(25, 8, "----%c", glyph::AMP); }

	if (std::isfinite(d.cell_voltage) && d.cell_voltage > 0.f) {
		const int icon = std::isfinite(d.remaining) && d.remaining >= 0.f && d.remaining <= 1.f ?
				 glyph::BATTERY + int((1.f - d.remaining) * 6.f) : ' '; // Do not imply a known state of charge.

		if (d.cell_voltage >= 3.6f || d.blink_visible) { c.text(24, 7, "%c%.2f%c", icon, double(d.cell_voltage), glyph::VOLT); }

	} else { c.text(24, 7, " ----%c", glyph::VOLT); }

	if (std::isfinite(kmh) && kmh > 2.f && std::isfinite(d.current) && d.current >= 0.f) {
		c.text(1, 7, "%c%3.0f%c", glyph::EFFICIENCY, double(truncf(1000.f * d.current / kmh)), glyph::MAH);

	} else { c.text(1, 7, "%c---%c", glyph::EFFICIENCY, glyph::MAH); }

	const int distance_row = pal ? 11 : 9;
	const int mode_row = pal ? 13 : 11;
	const int bottom_row = pal ? 14 : 12;
	c.put(22, distance_row, glyph::DISTANCE);
	c.distance(23, distance_row, d.distance);
	c.text(12, mode_row, "%.12s", d.callsign);
	c.text(25, mode_row, "%-4.4s%c", d.mode, d.status_valid ? (d.armed ? glyph::ARMED : glyph::DISARMED) : '-');

	if (d.satellites < 0) { c.text(0, bottom_row, "%c%c--", glyph::SAT_LEFT, glyph::SAT_RIGHT); }

	else if ((d.gps_fix && d.satellites >= 9) || d.blink_visible) { c.text(0, bottom_row, "%c%c%2d", glyph::SAT_LEFT, glyph::SAT_RIGHT, d.satellites); }

	c.text(11, bottom_row, "%.11s", d.plus_code);
	c.text(25, bottom_row, "%4.4s%c", d.vtx_power, glyph::MW);
	c.text(2, 10, "%.26s", d.message); // warnings drawn last, over the moving horizon
}
}
