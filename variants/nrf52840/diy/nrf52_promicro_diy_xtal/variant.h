#ifndef _VARIANT_PROMICRO_DIY_
#define _VARIANT_PROMICRO_DIY_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// #define USE_LFXO // Board uses 32khz crystal for LF
#define USE_LFRC // Board uses RC for LF

#define PROMICRO_DIY_XTAL
#define HAS_NRF52_DUAL_BATTERY_SENSE
#define HAS_NRF52_VBUS_DETECT
/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
NRF52 PRO MICRO PIN ASSIGNMENT

| Pin   | Function   |   | Pin     | Function     |
|-------|------------|---|---------|--------------|
| Gnd   |            |   | vbat    |              |
| P0.06 | MOSI       |   | vbat    |              |
| P0.08 | MISO       |   | Gnd     |              |
| Gnd   |            |   | reset   |              |
| Gnd   |            |   | ext_vcc | *see 0.13    |
| P0.17 | SCK        |   | P0.31   | BATTERY_PIN  |
| P0.20 | GPS_RX     |   | P0.29   | Free pin     |
| P0.22 | GPS_TX     |   | P0.02   | Free pin     |
| P0.24 | CS         |   | P1.15   | Free pin     |
| P1.00 | BUTTON_PIN |   | P1.13   | Free pin     |
| P0.11 | DIO0/IRQ   |   | P1.11   | Free pin     |
| P1.04 | SDA        |   | P0.10   | Free pin     |
| P1.06 | Free pin   |   | P0.09   | RESET        |
|       |            |   |         |              |
|       | Mid board  |   |         | Internal     |
| P1.01 | Free pin   |   | 0.15    | LED          |
| P1.02 | Free pin   |   | 0.13    | 3V3_EN       |
| P1.07 | Free pin   |   |         |              |
*/

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// Pin 13 enables 3.3V periphery. If the Lora module is on this pin, then it should stay enabled at all times.
#define PIN_3V3_EN (0 + 13) // P0.13

// Analog pins
#define BATTERY_PIN (0 + 31) // P0.31 Battery ADC
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define BATTERY_SENSE_SAMPLE_TIME 40
// Definition of milliVolt per LSB => 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#define VBAT_MV_PER_LSB (0.73242188F)
// VBAT -> 1M -> P0.31 -> 1M -> GND, so VADC / VBAT = 0.5
#define VBAT_DIVIDER (0.5F)
// Inverse divider factor used to reconstruct VBAT from VADC
#define VBAT_DIVIDER_COMP (1.0F / VBAT_DIVIDER)
// Fixed calculation of milliVolt from compensation value
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP // REAL_VBAT_MV_PER_LSB
#define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

// WIRE IC AND IIC PINS
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4) // P1.04
#define PIN_WIRE_SCL (32 + 6) // P1.06; P0.11 is used by RFM95W DIO0 / G0

// LED
#define PIN_LED1 (0 + 15) // P0.15
// Actually red
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 1 // State when LED is lit

// Button
#define BUTTON_PIN (32 + 0) // P1.00

// GPS
#define GPS_TX_PIN (0 + 20) // P0.20 - MCU TX to GPS RX
#define GPS_RX_PIN (0 + 22) // P0.22 - MCU RX from GPS TX

#define PIN_GPS_EN (0 + 29) // P0.29; P0.24 is used by RFM95W CS
#define GPS_UBLOX
// define GPS_DEBUG

// UART interfaces
#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL1_RX GPS_RX_PIN

#define PIN_SERIAL2_RX (32 + 1) // P1.01; P0.06 is used by RFM95W MOSI
#define PIN_SERIAL2_TX (32 + 2) // P1.02; P0.08 is used by RFM95W MISO

// Serial interfaces
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 8)  // P0.08 / silk 008 -> RFM95W MISO
#define PIN_SPI_MOSI (0 + 6)  // P0.06 / silk 006 -> RFM95W MOSI
#define PIN_SPI_SCK (0 + 17)  // P0.17 / silk 017 -> RFM95W SCK

// LORA MODULES
// Adafruit RFM95W uses the Semtech SX127x/RFM95 driver path, not SX126x/LLCC68.
#define USE_RF95

// LORA CONFIG - Adafruit RFM95W / SX127x
#define LORA_CS (0 + 24)        // P0.24 / silk 024 -> RFM95W CS
#define LORA_DIO0 (0 + 11)      // P0.11 / silk 011 -> RFM95W G0 / DIO0 / IRQ
#define LORA_RESET (0 + 9)      // P0.09 / silk 009 -> RFM95W RST
#define LORA_DIO1 RADIOLIB_NC   // Not required by Meshtastic RF95Interface
#define LORA_DIO2 RADIOLIB_NC   // Not required by Meshtastic RF95Interface

// Adafruit RFM95W uses the PA_BOOST output path and supports up to 20 dBm.
// Do not define USE_RF95_RFO for this module, or RadioLib will use the lower-power RFO path.
#define RF95_MAX_POWER 20
#define RF95_CURRENT_LIMIT 120
#define RF95_ALLOW_20DBM_TX_POWER
#define LORA_TW_POWER_LIMIT_OVERRIDE 20

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
