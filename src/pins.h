/**
 * pins.h — STM-Auto pin map (SWEE.BRZ oil monitor)
 * Target: STM32F103C8T6 "Blue Pill", PlatformIO + Arduino (variant_PILL_F103Cx).
 * Single source of truth for pin assignments; DECISION_MATRIX.md follows this file.
 *
 * Shared timers (library-owned — keep other PWM off them):
 *   TIM2 = Servo library   |   TIM3 = tone() (buzzer)   |   free: TIM1, TIM4.
 * Power: external 12V->5V converter feeds the servos, pressure sensor and STM32
 *   (5V pin); onboard 3.3V feeds the OLED + CAN transceiver; all grounds common.
 * Levels: analog/ADC pins keep <=3.3V (3.6V abs max). 5V-tolerant: PA9/PA10, PB6/PB7.
 */

#ifndef PINS_H
#define PINS_H

// SENSORS — analog in, keep <= 3.3V
#define PIN_TEMP_SENSE    PA1   // ADC, 220R series divider
#define PIN_PRESS_SENSE   PA0   // ADC, 10k/20k divider (clamped <=3.3V), 5V-supplied sensor

// ACTUATORS — two flap servos driven in tandem (write the same angle to both)
#define PIN_SERVO_FLAP_1  PA6   // Servo lib (TIM2); 5V supply + 470uF cap, signal only to MCU
#define PIN_SERVO_FLAP_2  PA7   // idem

// DISPLAY — OLED 0.96" SSD1306 on I2C2
#define PIN_OLED_SCL      PB10  // I2C2 SCL
#define PIN_OLED_SDA      PB11  // I2C2 SDA

// AUDIO
#define PIN_MP3_TX        PA9   // USART1 TX -> module RX (3.3V module, own decoupled supply)
#define PIN_MP3_RX        PA10  // USART1 RX <- module TX
#define PIN_BUZZER        PB6   // piezo via tone() (TIM3)

// USER INPUT — capacitive touch module (VCC/GND/OUT); OUT idle LOW, HIGH on touch
#define PIN_ACK_BUTTON    PB5   // acknowledge; plain INPUT (module drives it), rising edge

// STATUS
#define PIN_WS2812_DATA   PB15  // RGB LED via SPI2 MOSI + DMA; level-shift to 5V (or supply ~4.3V)
#define PIN_STATUS_LED    PC13  // onboard LED (active LOW), backup indicator

// CAN BUS — SN65HVD230 @ 3.3V. PA11/PA12 are the USB pins: no USB, remove PA12 USB pull-up.
#define PIN_CAN_RX        PA11
#define PIN_CAN_TX        PA12

// Free for future use: PA2/PA3 (USART2), PA4/PA5/PA8, PB0/PB1, PB7-PB9, PB12-PB14.

#endif // PINS_H
