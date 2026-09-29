/****************************************************************************
 *
 *   Copyright (c) 2021 PX4 Development Team. All rights reserved.
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
 * @file board_config.h
 *
 * Board internal definitions
 */

#pragma once

/****************************************************************************************************
 * Included Files
 ****************************************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <nuttx/compiler.h>
#include <stdint.h>

#include <stm32_gpio.h>

/****************************************************************************************************
 * Definitions
 ****************************************************************************************************/

// #define FLASH_BASED_PARAMS


/* Vector LEDs are active low. */
#define GPIO_nLED_BLUE  /* PC13 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_50MHz|GPIO_OUTPUT_SET|GPIO_PORTC|GPIO_PIN13)
#define GPIO_nLED_RED   /* PC14 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_50MHz|GPIO_OUTPUT_SET|GPIO_PORTC|GPIO_PIN14)
#define GPIO_nLED_GREEN /* PC15 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_50MHz|GPIO_OUTPUT_SET|GPIO_PORTC|GPIO_PIN15)

#define GPIO_IMU_PWR_EN /* PE13 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_SET|GPIO_PORTE|GPIO_PIN13)
#define GPIO_PL_GPIO_1  /* PE3  */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTE|GPIO_PIN3)
#define GPIO_PL_GPIO_2  /* PE4  */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTE|GPIO_PIN4)
#define GPIO_CAM_CTRL   /* PB2  */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTB|GPIO_PIN2)

#define BOARD_HAS_CONTROL_STATUS_LEDS 1
#define BOARD_ARMED_STATE_LED 3
#define BOARD_OVERLOAD_LED 0

/*
 * ADC channels
 *
 * These are the channel numbers of the ADCs of the microcontroller that
 * can be used by the Px4 Firmware in the adc driver
 */

/* ADC defines to be used in sensors.cpp to read from a particular channel */
#define ADC1_CH(n)                  (n)

/* Define GPIO pins used as ADC N.B. Channel numbers must match below  */
#define PX4_ADC_GPIO  \
	/* PC0  */  GPIO_ADC123_INP10, \
	/* PC1  */  GPIO_ADC123_INP11, \
	/* PA4  */  GPIO_ADC12_INP18, \
	/* PA7  */  GPIO_ADC12_INP7, \
	/* PC4  */  GPIO_ADC12_INP4, \
	/* PC5  */  GPIO_ADC12_INP8

/* Define Channel numbers must match above GPIO pin IN(n)*/
#define ADC_BATTERY_VOLTAGE_CHANNEL     /* PC0  */  ADC1_CH(10)
#define ADC_BATTERY_CURRENT_CHANNEL     /* PC1  */  ADC1_CH(11)
#define ADC_SCALED_5V_CHANNEL          /* PA4  */  ADC1_CH(18)
#define ADC_SCALED_3V3_CHANNEL         /* PA7  */  ADC1_CH(7)
#define ADC_SCALED_12V_CHANNEL         /* PC4  */  ADC1_CH(4)
#define ADC_SCALED_SENSOR_VDD_CHANNEL  /* PC5  */  ADC1_CH(8)

#define ADC_CHANNELS \
	((1 << ADC_BATTERY_VOLTAGE_CHANNEL) | \
	 (1 << ADC_BATTERY_CURRENT_CHANNEL) | \
	 (1 << ADC_SCALED_5V_CHANNEL) | \
	 (1 << ADC_SCALED_3V3_CHANNEL) | \
	 (1 << ADC_SCALED_12V_CHANNEL) | \
	 (1 << ADC_SCALED_SENSOR_VDD_CHANNEL))


/* ADC system-power interface: the rail dividers are 2:1 in the ArduPilot hwdef.
 * Vector has no documented VBUS/power-good GPIO. USB activity is observed from
 * received USB frames; the avionics rail is checked through the measured ADC.
 */
#define ADC_SCALED_V5_SENSE ADC_SCALED_5V_CHANNEL
#define ADC_V5_V_FULL_SCALE (6.6f)
#define ADC_SCALED_V3V3_SENSORS_SENSE {ADC_SCALED_3V3_CHANNEL, ADC_SCALED_SENSOR_VDD_CHANNEL}
#define ADC_SCALED_V3V3_SENSORS_COUNT 2
#define ADC_3V3_SCALE (2.0f)
#define BOARD_ADC_USB_CONNECTED board_usb_connected()

/* Voltage divider and current scale from ArduPilot vector-H743. */
#define BOARD_BATTERY1_V_DIV   (11.0f)
#define BOARD_BATTERY1_A_PER_V (15.0f)

/* PWM
 */
#define DIRECT_PWM_OUTPUT_CHANNELS   10


/* Tone alarm output */

#define GPIO_TONE_ALARM_IDLE    /* PA15 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTA|GPIO_PIN15)
#define GPIO_TONE_ALARM_GPIO    /* PA15 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTA|GPIO_PIN15)

// #define TONE_ALARM_TIMER        2  /* Timer 2 */
// #define TONE_ALARM_CHANNEL      1  /* PA15 GPIO_TIM2_CH1OUT_2 */
// #define GPIO_TONE_ALARM_IDLE    /* PA15 */ (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTA|GPIO_PIN15)
// #define GPIO_TONE_ALARM         GPIO_TIM2_CH1OUT_2


/* The PX4 H7 bootloader requires a VBUS GPIO definition even when
 * BOARD_VBUS is not used. ArduPilot does not define a Vector VBUS sense
 * signal, so PE2 is configured only as an input and never interpreted.
 */
#define GPIO_OTGFS_VBUS /* PE2 */ (GPIO_INPUT|GPIO_FLOAT|GPIO_PORTE|GPIO_PIN2)

/* High-resolution timer */
#define HRT_TIMER               2
#define HRT_TIMER_CHANNEL       1


/* RC Serial port */
#define RC_SERIAL_PORT          "/dev/ttyS4"
#define BOARD_SUPPORTS_RC_SERIAL_PORT_OUTPUT

// #define GPIO_RSSI_IN            /* PC5  */ (GPIO_INPUT|GPIO_PULLUP|GPIO_PORTC|GPIO_PIN5)


/* SD Card */
#define SDIO_SLOTNO             0  /* Only one slot */
#define SDIO_MINOR              0

/* This board provides a DMA pool and APIs */
#define BOARD_DMA_ALLOC_POOL_SIZE 5120

/* This board provides the board_on_reset interface */
#define BOARD_HAS_ON_RESET 1


#define PX4_GPIO_INIT_LIST { \
		PX4_ADC_GPIO, \
		GPIO_CAN1_TX, \
		GPIO_CAN1_RX, \
		GPIO_nLED_BLUE, \
		GPIO_nLED_RED, \
		GPIO_nLED_GREEN, \
		GPIO_IMU_PWR_EN, \
		GPIO_PL_GPIO_1, \
		GPIO_PL_GPIO_2, \
		GPIO_CAM_CTRL, \
		GPIO_TONE_ALARM_IDLE, \
	}

#define BOARD_ENABLE_CONSOLE_BUFFER

#define BOARD_NUM_IO_TIMERS 3


__BEGIN_DECLS

/****************************************************************************************************
 * Public Types
 ****************************************************************************************************/

/****************************************************************************************************
 * Public data
 ****************************************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************************************
 * Public Functions
 ****************************************************************************************************/

/****************************************************************************
 * Name: stm32_sdio_initialize
 *
 * Description:
 *   Initialize SDIO-based MMC/SD card support
 *
 ****************************************************************************/

int stm32_sdio_initialize(void);

/****************************************************************************************************
 * Name: stm32_spiinitialize
 *
 * Description:
 *   Called to configure SPI chip select GPIO pins for the board.
 *
 ****************************************************************************************************/

extern void stm32_spiinitialize(void);

extern void stm32_usbinitialize(void);
extern bool board_usb_connected(void);

extern void board_peripheral_reset(int ms);

#include <px4_platform_common/board_common.h>

#endif /* __ASSEMBLY__ */

__END_DECLS
