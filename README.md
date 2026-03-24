# STM32 Data Logger with BME680

This project implements a modular data logging system on STM32 using an I2C-based environmental sensor (BME680).

The goal of this project is to demonstrate embedded firmware design, including hardware integration, modular software architecture, and systematic debugging of communication issues.

---

## Features

- I2C communication with BME680 sensor
- Sensor initialization with chip ID verification
- Ring buffer for structured data storage
- Logger module for formatted UART output
- I2C bus scanning for hardware debugging

---

## System Architecture

The system is composed of the following modules:

- sensor  
  Handles BME680 communication via I2C, including initialization and data acquisition.

- ring_buffer  
  Stores sensor data and decouples data acquisition from output.

- logger  
  Formats sensor data and outputs it via UART.

- main  
  Integrates all modules and controls the execution flow.

---

## Hardware Setup

Board: STM32 NUCLEO-F446RE  
Sensor: BME680 (I2C mode)

### Connections

| BME680 | STM32 |
|--------|------|
| VCC    | 3.3V |
| GND    | GND  |
| SDA    | PB9  |
| SCL    | PB8  |
| CS     | 3.3V (I2C mode) |
| SDO    | GND (I2C address = 0x76) |

---

## I2C Pull-up Requirement

I2C uses an open-drain architecture, which requires pull-up resistors on SDA and SCL lines.

Without pull-up resistors:
- Communication may fail or become unstable
- I2C scan may not detect any device
- Sensor initialization may intermittently fail

### Recommended configuration

SDA ──[4.7kΩ]── 3.3V  
SCL ──[4.7kΩ]── 3.3V  

---

## Debugging Approach

To diagnose I2C communication issues, the following steps were used:

1. Scan the I2C bus to detect connected devices
2. Verify the correct I2C address (0x76 or 0x77)
3. Check for pull-up resistors on SDA/SCL
4. Validate wiring and power stability
5. Add debug logs to identify failure points

---

## I2C Scan Example

```c
printf("scan...\n");

for (uint8_t addr = 1; addr < 128; addr++)
{
    if (HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 1, 10) == HAL_OK)
    {
        printf("found device at 0x%02X\n", addr);
    }
}
