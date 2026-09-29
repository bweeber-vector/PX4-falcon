/****************************************************************************
 *
 *   Copyright (C) 2021 PX4 Development Team. All rights reserved.
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
 * @file usb.c
 *
 * Board-specific USB functions.
 */

#include "board_config.h"
#include <nuttx/usb/usbdev.h>
#include <nuttx/usb/usbdev_trace.h>
#include <stm32_otg.h>
#include <debug.h>
#ifndef CONFIG_BOOTLOADER
#include <drivers/drv_hrt.h>
#include <nuttx/irq.h>
#include "arm_internal.h"
#include "hardware/stm32_otg.h"
#endif

/************************************************************************************
 * Name: stm32_usbinitialize
 *
 * Description:
 *   Called to setup USB-related GPIO pins for the board.
 *
 ************************************************************************************/

__EXPORT void stm32_usbinitialize(void)
{
	/* The OTG FS has an internal soft pull-up */

	/* Configure the OTG FS VBUS sensing GPIO, Power On, and Overcurrent GPIOs */

#ifdef CONFIG_STM32F7_OTGFS
	stm32_configgpio(GPIO_OTGFS_VBUS);
#endif
}

/************************************************************************************
 * Name:  stm32_usbsuspend
 *
 * Description:
 *   Board logic must provide the stm32_usbsuspend logic if the USBDEV driver is
 *   used.  This function is called whenever the USB enters or leaves suspend mode.
 *   This is an opportunity for the board logic to shutdown clocks, power, etc.
 *   while the USB is suspended.
 *
 ************************************************************************************/
__EXPORT void stm32_usbsuspend(FAR struct usbdev_s *dev, bool resume)
{
	uinfo("resume: %d\n", resume);
}

#ifndef CONFIG_BOOTLOADER
/* No VBUS pin is documented. Observe SOF progress, not a floating GPIO or
 * CDC DTR (which depends on the host application). This detects an active USB
 * host, not a charge-only cable. Leave CDC enumeration enabled independently.
 */
__EXPORT bool board_usb_connected(void)
{
	static uint32_t previous_frame;
	static hrt_abstime last_activity;
	const irqstate_t flags = enter_critical_section();
	const uint32_t frame = getreg32(STM32_OTGFS_BASE + STM32_OTG_DSTS_OFFSET) & OTG_DSTS_SOFFN_MASK;
	const hrt_abstime now = hrt_absolute_time();

	if (frame != previous_frame) {
		previous_frame = frame;
		last_activity = now;
	}

	const bool connected = last_activity != 0 && now - last_activity < 1000000;
	leave_critical_section(flags);
	return connected;
}
#endif

/* Vector's ArduPilot hwdef has USB D+/D- but no VBUS sense GPIO. Keep
 * USB CDC available without assuming the copied Matek PE2 wiring.
 */
__EXPORT int board_read_VBUS_state(void)
{
	return 0;
}
