#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stdint.h>

// =======================
// Callback type
// =======================
typedef void (*i2c_callback_t)(void);

// =======================
// Init
// =======================
void i2c_init(void);

// =======================
// Blocking API
// =======================
int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t data);
int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len);

// =======================
// Non-blocking API
// =======================
int i2c_write_reg_async(uint8_t dev, uint8_t reg, uint8_t data, i2c_callback_t cb);
int i2c_read_reg_async(uint8_t dev, uint8_t reg, uint8_t *buf, int len, i2c_callback_t cb);

// =======================
void i2c_ev_irq_handler(void);
int i2c_is_busy(void);

#endif