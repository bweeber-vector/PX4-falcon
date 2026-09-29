/****************************************************************************
 *
 *   Copyright (c) 2018 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file atxxxx.cpp
 * @author Daniele Pettenuzzo
 * @author Beat Küng <beat-kueng@gmx.net>
 *
 * Driver for the ATXXXX chip (e.g. MAX7456) on the omnibus f4 fcu connected via SPI.
 */

#include "atxxxx.h"
#include "symbols.h"
#include "vector_hud.h"
#include <matrix/matrix/math.hpp>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>

using namespace time_literals;

static constexpr uint32_t OSD_UPDATE_RATE{50_ms};	// 20 Hz

OSDatxxxx::OSDatxxxx(const I2CSPIDriverConfig &config) :
	SPI(config),
	ModuleParams(nullptr),
	I2CSPIDriver(config),
	_vector(config.custom1 != 0)
{
}

int
OSDatxxxx::init()
{
	if (_param_osd_atxxxx_cfg.get() < 1 || _param_osd_atxxxx_cfg.get() > 2) {
		PX4_ERR("Set OSD_ATXXXX_CFG to 1 (NTSC) or 2 (PAL)");
		return PX4_ERROR;
	}

	/* do SPI init (and probe) first */
	int ret = SPI::init();

	if (ret != PX4_OK) {
		return ret;
	}

	ret = reset();

	if (ret != PX4_OK) {
		return ret;
	}

	ret = init_osd();

	if (ret != PX4_OK) {
		return ret;
	}

	if (_vector) {
		load_vector_callsign();
		// The first scheduled update redraws every cell, including unused rows.
		return start();
	}

	// clear the screen
	int num_rows = (_param_osd_atxxxx_cfg.get() == 1 ? OSD_NUM_ROWS_NTSC : OSD_NUM_ROWS_PAL);

	for (int i = 0; i < OSD_CHARS_PER_ROW; i++) {
		for (int j = 0; j < num_rows; j++) {
			add_character_to_screen(' ', i, j);
		}
	}

	if (ret == PX4_OK) {
		start();
	}

	return ret;
}

int
OSDatxxxx::start()
{
	ScheduleOnInterval(_vector ? 200_ms : OSD_UPDATE_RATE, 10000);

	return PX4_OK;
}

int
OSDatxxxx::probe()
{
	if (_vector) {
		// Explicit opt-in: this hardware has no register readback/probe.
		return PX4_OK;
	}

	uint8_t data = 0;
	int ret = PX4_OK;

	ret |= writeRegister(0x00, 0x01); //disable video output
	ret |= readRegister(0x00, &data, 1);

	if (data != 1 || ret != PX4_OK) {
		PX4_ERR("probe failed (%i %i)", ret, data);
		return PX4_ERROR;
	}

	return ret;
}

int
OSDatxxxx::init_osd()
{
	if (_vector) {
		int ret = writeRegister(vector_osd::FONT_REGISTER, vector_osd::ARDUPILOT_FONT);
		px4_usleep(1_ms);
		ret |= writeRegister(0x04, 0); // Explicit character addressing.
		ret |= enable_screen();
		return ret;
	}

	int ret = PX4_OK;
	uint8_t data = OSD_ZERO_BYTE;

	if (_param_osd_atxxxx_cfg.get() == 2) {
		data |= OSD_PAL_TX_MODE;
	}

	ret |= writeRegister(0x00, data);
	ret |= writeRegister(0x04, OSD_ZERO_BYTE);

	enable_screen();

	return ret;
}

int
OSDatxxxx::readRegister(unsigned reg, uint8_t *data, unsigned count)
{
	uint8_t cmd[5] {}; // read up to 4 bytes

	cmd[0] = DIR_READ(reg);

	int ret = transfer(&cmd[0], &cmd[0], count + 1);

	if (ret != PX4_OK) {
		DEVICE_LOG("spi::transfer returned %d", ret);
		return ret;
	}

	memcpy(&data[0], &cmd[1], count);

	return ret;
}

int
OSDatxxxx::writeRegister(unsigned reg, uint8_t data)
{
	uint8_t cmd[2] {}; // write 1 byte

	cmd[0] = DIR_WRITE(reg);
	cmd[1] = data;

	int ret = transfer(&cmd[0], nullptr, 2);

	if (OK != ret) {
		DEVICE_LOG("spi::transfer returned %d", ret);
		return ret;
	}

	return ret;
}

int
OSDatxxxx::add_character_to_screen(char c, uint8_t pos_x, uint8_t pos_y)
{
	uint16_t position = (OSD_CHARS_PER_ROW * pos_y) + pos_x;
	uint8_t position_lsb = 0;
	int ret = PX4_ERROR;

	if (position > 0xFF) {
		position_lsb = static_cast<uint8_t>(position & 0xFF);
		ret = writeRegister(0x05, 0x01); //DMAH

	} else {
		position_lsb = static_cast<uint8_t>(position);
		ret = writeRegister(0x05, 0x00); //DMAH
	}

	if (ret != 0) {
		return ret;
	}

	ret = writeRegister(0x06, position_lsb); //DMAL

	if (ret != 0) {
		return ret;
	}

	ret = writeRegister(0x07, c);

	return ret;
}

void
OSDatxxxx::add_string_to_screen_centered(const char *str, uint8_t pos_y, int max_length)
{
	int len = strlen(str);

	if (len > max_length) {
		len = max_length;
	}

	int pos = (OSD_CHARS_PER_ROW - max_length) / 2;
	int before = (max_length - len) / 2;

	for (int i = 0; i < before; ++i) {
		add_character_to_screen(' ', pos++, pos_y);
	}

	for (int i = 0; i < len; ++i) {
		add_character_to_screen(str[i], pos++, pos_y);
	}

	while (pos < (OSD_CHARS_PER_ROW + max_length) / 2) {
		add_character_to_screen(' ', pos++, pos_y);
	}
}

void
OSDatxxxx::clear_line(uint8_t pos_x, uint8_t pos_y, int length)
{
	for (int i = 0; i < length; ++i) {
		add_character_to_screen(' ', pos_x + i, pos_y);
	}
}

int
OSDatxxxx::add_battery_info(uint8_t pos_x, uint8_t pos_y)
{
	char buf[10];
	int ret = PX4_OK;

	char batt_symbol = OSD_SYMBOL_BATT_EMPTY;

	if (_battery_remaining >= 0.875f) {
		batt_symbol = OSD_SYMBOL_BATT_FULL;

	} else if (_battery_remaining >= 0.625f) {
		batt_symbol = OSD_SYMBOL_BATT_5;

	} else if (_battery_remaining >= 0.375f) {
		batt_symbol = OSD_SYMBOL_BATT_4;

	} else if (_battery_remaining >= 0.25f) {
		batt_symbol = OSD_SYMBOL_BATT_3;

	} else if (_battery_remaining >= 0.125f) {
		batt_symbol = OSD_SYMBOL_BATT_2;

	} else if (_battery_remaining >= 0.f) {
		batt_symbol = OSD_SYMBOL_BATT_1;
	}

	snprintf(buf, sizeof(buf), "%c%5.2f", batt_symbol, (double)_battery_voltage_v);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	ret |= add_character_to_screen('V', pos_x + 5, pos_y);

	pos_y++;
	pos_x++;

	snprintf(buf, sizeof(buf), "%5d", (int)_battery_discharge_mah);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	ret |= add_character_to_screen(OSD_SYMBOL_MAH, pos_x + 5, pos_y);

	return ret;
}

int
OSDatxxxx::add_altitude(uint8_t pos_x, uint8_t pos_y)
{
	char buf[16];
	int ret = PX4_OK;

	snprintf(buf, sizeof(buf), "%c%5.2f%c", OSD_SYMBOL_ARROW_NORTH, (double)_local_position_z, OSD_SYMBOL_M);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::add_flighttime(float flight_time, uint8_t pos_x, uint8_t pos_y)
{
	char buf[10];
	int ret = PX4_OK;

	snprintf(buf, sizeof(buf), "%c%5.1f", OSD_SYMBOL_FLIGHT_TIME, (double)flight_time);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::enable_screen()
{
	if (_vector) {
		return writeRegister(vector_osd::VM0, vector_osd::video_mode(_param_osd_atxxxx_cfg.get() == 2));
	}

	uint8_t data = 0;
	int ret = PX4_OK;

	ret |= readRegister(0x00, &data, 1);
	ret |= writeRegister(0x00, data | 0x48);

	return ret;
}

int
OSDatxxxx::disable_screen()
{
	if (_vector) {
		return writeRegister(vector_osd::VM0, _param_osd_atxxxx_cfg.get() == 2 ? vector_osd::PAL : 0);
	}

	uint8_t data = 0;
	int ret = PX4_OK;

	ret |= readRegister(0x00, &data, 1);
	ret |= writeRegister(0x00, data & 0xF7);

	return ret;
}

int
OSDatxxxx::update_topics()
{
	/* update battery subscription */
	if (_battery_sub.updated()) {
		battery_status_s battery{};
		_battery_sub.copy(&battery);

		if (battery.connected) {
			_battery_voltage_v = battery.voltage_v;
			_battery_discharge_mah = battery.discharged_mah;
			_battery_remaining = battery.remaining;
			_battery_valid = true;

		} else {
			_battery_valid = false;
		}
	}

	/* update vehicle local position subscription */
	if (_local_position_sub.updated()) {
		vehicle_local_position_s local_position{};
		_local_position_sub.copy(&local_position);

		_local_position_valid = local_position.z_valid;

		if (_local_position_valid) {
			_local_position_z = -local_position.z;
		}
	}

	/* update vehicle status subscription */
	if (_vehicle_status_sub.updated()) {
		vehicle_status_s vehicle_status{};
		_vehicle_status_sub.copy(&vehicle_status);

		if (vehicle_status.arming_state == vehicle_status_s::ARMING_STATE_ARMED &&
		    _arming_state != vehicle_status_s::ARMING_STATE_ARMED) {
			// arming
			_arming_timestamp = hrt_absolute_time();

		} else if (vehicle_status.arming_state != vehicle_status_s::ARMING_STATE_ARMED &&
			   _arming_state == vehicle_status_s::ARMING_STATE_ARMED) {
			// disarming
		}

		_arming_state = vehicle_status.arming_state;
		_nav_state = vehicle_status.nav_state;
	}

	return PX4_OK;
}

const char *
OSDatxxxx::get_flight_mode(uint8_t nav_state)
{
	const char *flight_mode = "UNKNOWN";

	switch (nav_state) {
	case vehicle_status_s::NAVIGATION_STATE_MANUAL:
		flight_mode = "MANUAL";
		break;

	case vehicle_status_s::NAVIGATION_STATE_ALTCTL:
		flight_mode = "ALTITUDE";
		break;

	case vehicle_status_s::NAVIGATION_STATE_ALTITUDE_CRUISE:
		flight_mode = "CRUISE";
		break;

	case vehicle_status_s::NAVIGATION_STATE_POSCTL:
		flight_mode = "POSITION";
		break;

	case vehicle_status_s::NAVIGATION_STATE_AUTO_RTL:
		flight_mode = "RETURN";
		break;

	case vehicle_status_s::NAVIGATION_STATE_AUTO_MISSION:
		flight_mode = "MISSION";
		break;

	case vehicle_status_s::NAVIGATION_STATE_AUTO_LOITER:
	case vehicle_status_s::NAVIGATION_STATE_DESCEND:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_TAKEOFF:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_LAND:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_FOLLOW_TARGET:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_PRECLAND:
		flight_mode = "AUTO";
		break;

	case vehicle_status_s::NAVIGATION_STATE_ACRO:
		flight_mode = "ACRO";
		break;

	case vehicle_status_s::NAVIGATION_STATE_TERMINATION:
		flight_mode = "TERMINATE";
		break;

	case vehicle_status_s::NAVIGATION_STATE_OFFBOARD:
		flight_mode = "OFFBOARD";
		break;

	case vehicle_status_s::NAVIGATION_STATE_STAB:
		flight_mode = "STABILIZED";
		break;
	}

	return flight_mode;
}

int
OSDatxxxx::update_screen()
{
	int ret = PX4_OK;

	if (_battery_valid) {
		ret |= add_battery_info(1, 1);

	} else {
		clear_line(1, 1, 10);
		clear_line(1, 2, 10);
	}

	if (_local_position_valid) {
		ret |= add_altitude(1, 3);

	} else {
		clear_line(1, 3, 10);
	}

	const char *flight_mode = "";

	if (_arming_state == vehicle_status_s::ARMING_STATE_ARMED) {
		float flight_time_sec = static_cast<float>((hrt_absolute_time() - _arming_timestamp) / (1e6f));
		ret |= add_flighttime(flight_time_sec, 1, 14);

	} else {
		flight_mode = get_flight_mode(_nav_state);
	}

	add_string_to_screen_centered(flight_mode, 12, 10);

	return ret;
}

int
OSDatxxxx::reset()
{
	int ret = writeRegister(0x00, 0x02);
	px4_usleep(_vector ? 1_ms : 100_us);

	return ret;
}

void
OSDatxxxx::RunImpl()
{
	if (should_exit()) {
		disable_screen();
		exit_and_cleanup();
		return;
	}

	update_topics();

	if (_vector) {
		if (update_vector_screen() == PX4_OK) {
			++_vector_frames;

		} else {
			++_vector_errors;
		}

	} else {
		update_screen();
	}
}

void OSDatxxxx::print_status()
{
	I2CSPIDriverBase::print_status();
	PX4_INFO("backend: %s, video: %s", _vector ? "Vector (write-only, display unverified)" : "ATXXXX",
		 _param_osd_atxxxx_cfg.get() == 2 ? "PAL" : "NTSC");

	if (_vector) {
		PX4_INFO("layout: %s", _param_osd_vec_layout.get() == 0 ? "Diagnostics" : "Hammer OSD1");
		PX4_INFO("frames sent: %lu, transfer failures: %lu", (unsigned long)_vector_frames, (unsigned long)_vector_errors);
	}
}

void OSDatxxxx::vector_line(unsigned row, const char *format, ...)
{
	if (row >= vector_osd::PAL_ROWS) {
		return;
	}

	char line[vector_osd::COLUMNS + 1] {};
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	memcpy(_vector_screen[row], line, strlen(line));
}

void OSDatxxxx::render_vector_diagnostics()
{
	const hrt_abstime now = hrt_absolute_time();
	const auto fresh = [now](uint64_t timestamp) {
		return timestamp != 0 && timestamp <= now && now - timestamp < 1_s;
	};
	memset(_vector_screen, ' ', sizeof(_vector_screen));
	vector_line(0, "PX4 VECTOR  %lus", (unsigned long)(now / 1000000));
	vehicle_status_s status{};
	_vehicle_status_sub.copy(&status);
	vector_line(1, "%s  %s", status.arming_state == vehicle_status_s::ARMING_STATE_ARMED ? "ARMED" : "DISARMED",
		    fresh(status.timestamp) ? (status.pre_flight_checks_pass ? "CHECKS PASS" : "NOT READY") : "STATUS STALE");
	vector_line(2, "%s", get_flight_mode(status.nav_state));

	input_rc_s rc{};
	_rc_sub.copy(&rc);
	const bool rc_ok = fresh(rc.timestamp_last_signal) && !rc.rc_lost && !rc.rc_failsafe && rc.channel_count >= 4;
	const char *protocol = rc.input_source == input_rc_s::RC_INPUT_SOURCE_PX4FMU_CRSF ? "CRSF" :
			       rc.input_source == input_rc_s::RC_INPUT_SOURCE_PX4FMU_SBUS ? "SBUS" : "OTHER";
	vector_line(3, "RC %s %s CH:%u", rc_ok ? "LIVE" : "LOST", protocol, (unsigned)rc.channel_count);

	if (rc_ok) {
		vector_line(4, "CH1:%4u CH2:%4u", (unsigned)rc.values[0], (unsigned)rc.values[1]);
		vector_line(5, "CH3:%4u CH4:%4u", (unsigned)rc.values[2], (unsigned)rc.values[3]);

	} else {
		vector_line(4, "NO VALID RECEIVER SIGNAL");
	}

	manual_control_setpoint_s manual{};
	_manual_sub.copy(&manual);

	if (rc_ok && manual.valid && manual.data_source == manual_control_setpoint_s::SOURCE_RC
	    && fresh(manual.timestamp_sample) && PX4_ISFINITE(manual.roll) && PX4_ISFINITE(manual.pitch)
	    && PX4_ISFINITE(manual.yaw) && PX4_ISFINITE(manual.throttle)) {
		vector_line(6, "ROLL:%+.2f PITCH:%+.2f", (double)manual.roll, (double)manual.pitch);
		vector_line(7, "YAW:%+.2f THR:%+.2f", (double)manual.yaw, (double)manual.throttle);

	} else {
		vector_line(6, "RC MAPPING NOT VALID");
	}

	sensor_accel_s accel{};
	sensor_gyro_s gyro{};
	_accel_sub.copy(&accel);
	_gyro_sub.copy(&gyro);
	const bool imu_ok = fresh(accel.timestamp) && fresh(gyro.timestamp) && accel.error_count == 0
			    && gyro.error_count == 0 && PX4_ISFINITE(accel.z) && PX4_ISFINITE(gyro.z);
	vector_line(8, "IMU %s AZ:%+.2f", imu_ok ? "DATA" : "WAIT/ERR", (double)accel.z);

	sensor_baro_s baro{};
	_baro_sub.copy(&baro);

	if (fresh(baro.timestamp) && baro.error_count == 0 && PX4_ISFINITE(baro.pressure)) {
		vector_line(9, "BARO %.1f hPa", (double)(baro.pressure * 0.01f));

	} else {
		vector_line(9, "BARO NO DATA/ERR");
	}

	vehicle_magnetometer_s mag{};
	sensor_gps_s gps{};
	_mag_sub.copy(&mag);
	_gps_sub.copy(&gps);
	vector_line(10, "MAG:%s GPS:%s SV:%u", fresh(mag.timestamp) ? "DATA" : "NONE",
		    fresh(gps.timestamp) && gps.fix_type >= 3 ? "FIX" : "WAIT", (unsigned)gps.satellites_used);
	battery_status_s battery{};
	_battery_sub.copy(&battery);

	if (fresh(battery.timestamp) && battery.connected && PX4_ISFINITE(battery.voltage_v)) {
		vector_line(11, "BAT %.2fV %.1fA", (double)battery.voltage_v, (double)battery.current_a);

	} else {
		vector_line(11, "BAT DISCONNECTED/STALE");
	}

}

int OSDatxxxx::update_vector_screen()
{
	// Keep diagnostics selectable during receiver and sensor commissioning.
	_param_osd_vec_layout.update();
	_param_osd_vec_cells.update();

	render_vector_hud(); // Keep flight statistics running in either layout.

	if (_param_osd_vec_layout.get() == 0) {
		render_vector_diagnostics();
	}

	const bool pal = _param_osd_atxxxx_cfg.get() == 2;
	const unsigned rows = pal ? vector_osd::PAL_ROWS : vector_osd::NTSC_ROWS;
	int ret = writeRegister(vector_osd::FONT_REGISTER, vector_osd::ARDUPILOT_FONT);
	ret |= writeRegister(0x02, _param_osd_atxxxx_hoff.get());
	ret |= writeRegister(0x03, _param_osd_atxxxx_voff.get());
	ret |= enable_screen();
	uint8_t row_buffer[vector_osd::ROW_BYTES] {};

	for (unsigned row = 0; row < rows; ++row) {
		if (!vector_osd::encode_row(_vector_screen[row], row, rows, row_buffer, sizeof(row_buffer))) {
			return PX4_ERROR;
		}

		ret |= transfer(row_buffer, nullptr, sizeof(row_buffer));
	}

	return ret;
}

void OSDatxxxx::load_vector_callsign()
{
	// Same SD-root file as ArduPilot. Read once at startup, bounded to the
	// space before the mode widget; no invented aircraft identification.
	FILE *file = fopen("/fs/microsd/callsign.txt", "r");

	if (file) {
		const size_t length = fread(_hud_callsign, 1, sizeof(_hud_callsign) - 1, file);
		fclose(file);

		for (size_t i = 0; i < length; ++i) {
			const unsigned char ch = _hud_callsign[i];
			_hud_callsign[i] = ch >= 32 && ch < 127 ? toupper(ch) : ' ';
		}
	}
}

void OSDatxxxx::render_vector_hud()
{
	using namespace vector_osd;
	HudData d;
	const hrt_abstime now = hrt_absolute_time();
	const auto fresh = [now](uint64_t timestamp) {
		return timestamp != 0 && timestamp <= now && now - timestamp < 2_s;
	};
	d.blink_visible = (now / 500_ms) % 2 == 0;
	d.callsign = _hud_callsign;

	vehicle_status_s &status = _hud_telemetry.status;
	_vehicle_status_sub.copy(&status);
	d.status_valid = fresh(status.timestamp);
	d.armed = d.status_valid && status.arming_state == vehicle_status_s::ARMING_STATE_ARMED;

	if (d.status_valid) {
		// Four-character PX4 mode abbreviations fit beside the arm-state glyph.
		switch (status.nav_state) {
		case vehicle_status_s::NAVIGATION_STATE_STAB: d.mode = "STAB"; break;

		case vehicle_status_s::NAVIGATION_STATE_ACRO: d.mode = "ACRO"; break;

		case vehicle_status_s::NAVIGATION_STATE_ALTCTL: d.mode = "ALT"; break;

		case vehicle_status_s::NAVIGATION_STATE_POSCTL: d.mode = "POS"; break;

		case vehicle_status_s::NAVIGATION_STATE_AUTO_RTL: d.mode = "RTL"; break;

		case vehicle_status_s::NAVIGATION_STATE_AUTO_MISSION: d.mode = "AUTO"; break;

		case vehicle_status_s::NAVIGATION_STATE_AUTO_LOITER: d.mode = "HOLD"; break;

		case vehicle_status_s::NAVIGATION_STATE_AUTO_TAKEOFF: d.mode = "TKOF"; break;

		case vehicle_status_s::NAVIGATION_STATE_AUTO_LAND: d.mode = "LAND"; break;

		case vehicle_status_s::NAVIGATION_STATE_OFFBOARD: d.mode = "OFFB"; break;

		default: d.mode = get_flight_mode(status.nav_state); break;
		}
	}

	vehicle_attitude_s &attitude = _hud_telemetry.attitude;
	_attitude_sub.copy(&attitude);
	const matrix::Quatf q(attitude.q);

	if (fresh(attitude.timestamp) && q.isAllFinite() && fabsf(q.norm() - 1.f) < .01f) {
		const matrix::Eulerf angles(q);
		d.roll = angles.phi();
		d.pitch = angles.theta();
		d.yaw = angles.psi();
	}

	vehicle_local_position_s &local = _hud_telemetry.local;
	_local_position_sub.copy(&local);

	if (fresh(local.timestamp)) {
		if (local.v_xy_valid) {
			d.north_speed = local.vx;
			d.east_speed = local.vy;
		}

		if (local.v_z_valid) { d.climb = -local.vz; }
	}

	home_position_s &home = _hud_telemetry.home;
	_home_sub.copy(&home);
	vehicle_global_position_s &global = _hud_telemetry.global;
	_global_position_sub.copy(&global);

	// Home is latched, so its publication need not be recent.
	if (home.timestamp && home.valid_alt && PX4_ISFINITE(home.alt)) {
		if (fresh(global.timestamp) && global.alt_valid) {
			d.altitude = global.alt - home.alt;

		} else if (fresh(local.timestamp) && local.z_valid && local.z_global) {
			d.altitude = local.ref_alt - local.z - home.alt;
		}
	}

	if (home.timestamp && home.valid_hpos && fresh(global.timestamp) && global.lat_lon_valid
	    && PX4_ISFINITE(home.lat) && PX4_ISFINITE(home.lon)
	    && PX4_ISFINITE(global.lat) && PX4_ISFINITE(global.lon)) {
		// Great-circle distance and initial bearing, using double precision
		// before subtracting latitude/longitude near home.
		constexpr double RADIANS = 3.14159265358979323846 / 180.;
		constexpr double EARTH_RADIUS = 6371000.;
		const double lat = global.lat * RADIANS, target_lat = home.lat * RADIANS;
		const double delta_lon = (home.lon - global.lon) * RADIANS;
		const double sin_lat = sin((target_lat - lat) * .5), sin_lon = sin(delta_lon * .5);
		const double haversine = sin_lat * sin_lat + cos(lat) * cos(target_lat) * sin_lon * sin_lon;
		d.home_distance = 2. * EARTH_RADIUS * asin(sqrt(fmax(0., fmin(1., haversine))));

		if (PX4_ISFINITE(d.yaw)) {
			d.home_bearing = d.home_distance < 2.f ? 0. :
					 atan2(sin(delta_lon) * cos(target_lat),
					       cos(lat) * sin(target_lat) - sin(lat) * cos(target_lat) * cos(delta_lon)) - double(d.yaw);
		}
	}

	input_rc_s &rc = _hud_telemetry.rc;
	_rc_sub.copy(&rc);
	const bool rc_ok = fresh(rc.timestamp_last_signal) && !rc.rc_lost && !rc.rc_failsafe && rc.channel_count >= 4;

	if (rc_ok) {
		d.rssi = rc.rssi;
		d.link_quality = rc.link_quality;
	}

	battery_status_s &battery = _hud_telemetry.battery;
	_battery_sub.copy(&battery);

	if (fresh(battery.timestamp) && battery.connected) {
		const int cells = _param_osd_vec_cells.get() > 0 ? _param_osd_vec_cells.get() : battery.cell_count;

		if (cells > 0 && PX4_ISFINITE(battery.voltage_v)) { d.cell_voltage = battery.voltage_v / cells; }

		// A remaining estimate without a configured battery cell count is not trustworthy.
		if (battery.cell_count > 0) { d.remaining = battery.remaining; }

		if (PX4_ISFINITE(battery.current_a) && battery.current_a >= 0.f) {
			_hud_current = PX4_ISFINITE(_hud_current) ? _hud_current + .33f * (battery.current_a - _hud_current) : battery.current_a;
			d.current = _hud_current;

		} else { _hud_current = NAN; }

	} else { _hud_current = NAN; }

	char location_code[12] = "--------+--";
	sensor_gps_s &gps = _hud_telemetry.gps;
	_gps_sub.copy(&gps);

	if (fresh(gps.timestamp)) {
		d.satellites = gps.satellites_used;
		d.gps_fix = gps.fix_type >= 3;

		if (gps.fix_type >= 2) { plus_code(gps.latitude_deg, gps.longitude_deg, location_code); }
	}

	d.plus_code = location_code;

	const uint64_t elapsed = _hud_last_update && now - _hud_last_update < 1_s ? now - _hud_last_update : 0;
	_hud_last_update = now;
	vehicle_land_detected_s &land = _hud_telemetry.land;
	_land_sub.copy(&land);

	if (d.armed) {
		const float speed = hypotf(d.north_speed, d.east_speed);

		if (PX4_ISFINITE(speed) && speed >= .178f) { _hud_distance += speed * float(elapsed) * 1e-6f; }

		if (fresh(land.timestamp) && !land.landed) { _hud_flight_us += elapsed; }
	}

	d.distance = _hud_distance;
	d.flight_seconds = _hud_flight_us / 1000000;

	vtx_s &vtx = _hud_telemetry.vtx;
	_vtx_sub.copy(&vtx);
	char power[5] = "----";

	// VTX publishes on change, including protocol NONE on communication loss.
	// Show its last reported table label; never infer milliwatts from its index.
	if (vtx.timestamp && vtx.protocol != vtx_s::PROTOCOL_NONE) {
		if (vtx.mode == vtx_s::MODE_PIT) {
			strcpy(power, "0");

		} else if (vtx.power_level >= 0 && vtx.power_label[0]) {
			memcpy(power, vtx.power_label, sizeof(power) - 1);
		}
	}

	d.vtx_power = power;
	log_message_s &message = _hud_telemetry.message;

	for (unsigned i = 0; i < log_message_s::ORB_QUEUE_LENGTH && _log_sub.update(&message); ++i) {
		if (message.severity <= 4) { _hud_message = message; } // warning or more severe
	}

	char visible_message[27] {};

	if (!d.status_valid) { d.message = "VEHICLE STATUS LOST"; }

	else if (status.failsafe) { d.message = "FAILSAFE"; }

	else if (!rc_ok) { d.message = "RC SIGNAL LOST"; }

	else if (_hud_message.timestamp && now >= _hud_message.timestamp && now - _hud_message.timestamp < 10_s) {
		const size_t length = strnlen(_hud_message.text, sizeof(_hud_message.text));
		const size_t extra = length > sizeof(visible_message) - 1 ? length - (sizeof(visible_message) - 1) : 0;
		const size_t phase = (now - _hud_message.timestamp) / 250_ms;
		const size_t cycle = 2 * extra + 8; // pause at both ends of the scroll
		const size_t position = phase % cycle;
		const size_t offset = position < extra + 4 ? (position > 4 ? position - 4 : 0) :
				      (position < extra + 8 ? extra : cycle - position);

		for (size_t i = 0; i < sizeof(visible_message) - 1 && i + offset < length; ++i) {
			const unsigned char ch = _hud_message.text[i + offset];
			visible_message[i] = ch >= 32 && ch < 127 ? toupper(ch) : ' ';
		}

		d.message = visible_message;

	} else if (!status.pre_flight_checks_pass && !d.armed) { d.message = "PREARM CHECKS: NOT READY"; }

	render_hud(_vector_screen, d, _param_osd_atxxxx_cfg.get() == 2);
}

void
OSDatxxxx::print_usage()
{
	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
OSD driver for the ATXXXX chip that is mounted on the OmnibusF4SD board for example.

It can be enabled with the OSD_ATXXXX_CFG parameter.
-V selects the write-only Vector backend and its ASCII bench status screen.
Vector requires SPI mode 3; it cannot detect or confirm a connected display.
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("atxxxx", "driver");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_PARAMS_I2C_SPI_DRIVER(false, true);
	PRINT_MODULE_USAGE_PARAM_FLAG('V', "Vector write-only OSD protocol", true);
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
}

int
atxxxx_main(int argc, char *argv[])
{
	using ThisDriver = OSDatxxxx;
	BusCLIArguments cli{false, true};
	cli.spi_mode = SPIDEV_MODE0;
	cli.default_spi_frequency = OSD_SPI_BUS_SPEED;

	int ch;

	while ((ch = cli.getOpt(argc, argv, "V")) != EOF) {
		if (ch == 'V') {
			cli.custom1 = 1;
		}
	}

	if (cli.custom1) {
		cli.spi_mode = SPIDEV_MODE3;
	}

	const char *verb = cli.optArg();

	if (!verb) {
		ThisDriver::print_usage();
		return -1;
	}

	BusInstanceIterator iterator(MODULE_NAME, cli, DRV_OSD_DEVTYPE_ATXXXX);

	if (!strcmp(verb, "start")) {
		return ThisDriver::module_start(cli, iterator);
	}

	if (!strcmp(verb, "stop")) {
		return ThisDriver::module_stop(iterator);
	}

	if (!strcmp(verb, "status")) {
		return ThisDriver::module_status(iterator);
	}

	ThisDriver::print_usage();
	return -1;
}
