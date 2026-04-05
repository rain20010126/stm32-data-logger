#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stdint.h>

void i2c_init(void);

int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t data);
int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len);

void i2c_ev_irq_handler(void);
int i2c_is_busy(void);

#endif