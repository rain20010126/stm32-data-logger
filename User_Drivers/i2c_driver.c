#include "i2c_driver.h"
#include "stm32f4xx.h"
#include <stdio.h>

static void i2c_start(void);
static int  i2c_send_address(uint8_t addr);
static int  i2c_write_byte(uint8_t data);
static uint8_t i2c_read_byte(int ack);
static void i2c_stop(void);
int i2c_write_reg_async(uint8_t dev, uint8_t reg, uint8_t val, i2c_callback_t cb);
int i2c_read_reg_async(uint8_t dev, uint8_t reg, uint8_t *buf, int len, i2c_callback_t cb);



typedef enum {
    I2C_IDLE,
    I2C_START,
    I2C_ADDR_W,
    I2C_REG,
    I2C_WRITE_DATA,   
    I2C_RESTART,
    I2C_ADDR_R,
    I2C_ADDR_R_WAIT,
    I2C_READ,
    I2C_STOP,
    I2C_DONE,
    I2C_ERROR
} i2c_state_t;

typedef struct {
    uint8_t dev;
    uint8_t reg;

    uint8_t *buf;
    uint8_t tx_buf[4];  

    int len;
    int idx;

    int is_read;

    i2c_state_t state;
    int busy;
} i2c_ctx_t;

static i2c_ctx_t i2c;
static i2c_callback_t i2c_cb = 0;

void i2c_init(void)
{
    // 1. enable GPIO
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // 2. configure PB8 PB9
    GPIOB->MODER &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB->MODER |=  ((2 << (8*2)) | (2 << (9*2)));

    GPIOB->AFR[1] &= ~((0xF << ((8-8)*4)) | (0xF << ((9-8)*4)));
    GPIOB->AFR[1] |=  ((4 << ((8-8)*4)) | (4 << ((9-8)*4)));

    GPIOB->OTYPER |= (1 << 8) | (1 << 9);

    GPIOB->PUPDR &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB->PUPDR |=  ((1 << (8*2)) | (1 << (9*2)));

    // 3. enable I2C
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // 4. reset
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // 5. clock config
    I2C1->CR2 = 16;
    I2C1->CCR = 80;
    I2C1->TRISE = 17;

    // 6. enable peripheral
    I2C1->CR1 |= I2C_CR1_PE;

    // 7. wait not busy
    while (I2C1->SR2 & I2C_SR2_BUSY);

    // 8. enable interrupt
    I2C1->CR2 |= (I2C_CR2_ITEVTEN |
                I2C_CR2_ITBUFEN |
                I2C_CR2_ITERREN);

    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
}

int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len)
{   
    i2c.is_read = 1; 

    if (i2c_read_reg_async(dev, reg, buf, len, 0) != 0)
        return -1;

    while (i2c_is_busy());

    return 0;
}

int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{   

    i2c.is_read = 0; 

    if (i2c_write_reg_async(dev, reg, val, 0) != 0)
        return -1;
  
    while (i2c_is_busy());

    return 0;
}

static void i2c_start(void)
{   
    // printf("before START: SR1=0x%04lX SR2=0x%04lX\n", I2C1->SR1, I2C1->SR2);
    uint32_t timeout = 100000;

    I2C1->CR1 |= I2C_CR1_START;

    // printf("after START: SR1=0x%04lX\n", I2C1->SR1);

    while (!(I2C1->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0) return;
    }

    volatile uint32_t temp = I2C1->SR1;
    (void)temp;
}


static int i2c_send_address(uint8_t addr)
{
    uint32_t timeout = 100000;

    // Send address 
    // DR: Data register 7-bit address + 1-bit R/W
    I2C1->DR = addr;

    // Wait for ADDR (address matched) or AF (Acknowledge Failure)
    while (!(I2C1->SR1 & I2C_SR1_ADDR))
    {
        // ACK failure
        if (I2C1->SR1 & I2C_SR1_AF)
        {
            I2C1->SR1 &= ~I2C_SR1_AF; //reset SR1
            return -1;
        }

        if (--timeout == 0)
            return -2;
    }

    // Clear ADDR (read SR1 then SR2)
    volatile uint32_t temp;
    temp = I2C1->SR1;
    temp = I2C1->SR2;
    (void)temp;

    return 0;
}

// --------------------------------

static int i2c_write_byte(uint8_t data)
{
    uint32_t timeout = 100000;

    // Write data
    I2C1->DR = data;

    // Wait TXE (Transmit Data Register Empty)
    while (!(I2C1->SR1 & I2C_SR1_TXE))
    {
        if (--timeout == 0)
            return -1;
    }

    // Wait BTF (Byte Transfer Finished)
    while (!(I2C1->SR1 & I2C_SR1_BTF))
    {
        if (--timeout == 0)
            return -2;
    }

    return 0;
}

// --------------------------------

static uint8_t i2c_read_byte(int ack)
{
    uint8_t data;

    if (ack)
        I2C1->CR1 |= I2C_CR1_ACK;
    else
        I2C1->CR1 &= ~I2C_CR1_ACK;

    // Wait RXNE (Receive buffer not empty)
    while (!(I2C1->SR1 & I2C_SR1_RXNE));

    // Read data and clean buffer
    data = I2C1->DR;

    return data;
}

static void i2c_stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
}


int i2c_is_busy(void)
{
    return i2c.busy;
}

int i2c_write_reg_async(uint8_t dev, uint8_t reg, uint8_t val, i2c_callback_t cb)
{   
    if (i2c.busy) return -1;

    i2c.dev = dev;
    i2c.reg = reg;
    i2c.tx_buf[0] = val;   
    i2c.buf = i2c.tx_buf;
    i2c.len = 1;
    i2c.idx = 0;
    i2c_cb = cb;
    i2c.state = I2C_START;
    i2c.busy = 1;

    // trigger START
    I2C1->CR1 |= I2C_CR1_START;

    return 0;
}

int i2c_read_reg_async(uint8_t dev, uint8_t reg, uint8_t *buf, int len, i2c_callback_t cb)
{
    if (i2c.busy) return -1;

    i2c.dev = dev;   // already (addr<<1)
    i2c.reg = reg;
    i2c.buf = buf;
    i2c.len = len;
    i2c.idx = 0;
    i2c_cb = cb;
    i2c.state = I2C_START;
    i2c.busy = 1;

    I2C1->CR1 |= I2C_CR1_START;

    return 0;
}

void i2c_ev_irq_handler(void)
{   
    
    uint32_t sr1 = I2C1->SR1;

    // printf("[IRQ] state=%d SR1=0x%04lX\n\r", i2c.state, sr1);

    switch (i2c.state)
    {
    // =========================
    // START → send write addr
    // =========================
    case I2C_START:
        if (sr1 & I2C_SR1_SB)
        {
            I2C1->DR = i2c.dev;   // write
            i2c.state = I2C_ADDR_W;
        }
        break;

    // =========================
    // ADDR (write)
    // =========================
    case I2C_ADDR_W:
        if (sr1 & I2C_SR1_ADDR)
        {
            volatile uint32_t tmp;
            tmp = I2C1->SR1;
            tmp = I2C1->SR2;
            (void)tmp; 

            i2c.state = I2C_REG;
        }
        break;

    // =========================
    // send register
    // =========================
    case I2C_REG:
        if (sr1 & I2C_SR1_TXE)
        {
            I2C1->DR = i2c.reg;

            if (i2c.is_read)
            {
                i2c.state = I2C_RESTART;   // read flow
            }
            else
            {
                i2c.state = I2C_WRITE_DATA; // write flow
            }
        }
        break;

    case I2C_WRITE_DATA:
        if (sr1 & I2C_SR1_BTF)
        {
            I2C1->DR = i2c.buf[i2c.idx++];

            if (i2c.idx >= i2c.len)
            {
                i2c.state = I2C_STOP;
            }
        }
        break;   

    // =========================
    // repeated start
    // =========================
    case I2C_RESTART:
        if (sr1 & I2C_SR1_BTF)
        {
            I2C1->CR1 |= I2C_CR1_START;
            i2c.state = I2C_ADDR_R;
        }
        break;

    // =========================
    // ADDR (read)
    // =========================
    case I2C_ADDR_R:
        if (sr1 & I2C_SR1_SB)
        {
            I2C1->DR = i2c.dev | 1;   // send read address
            i2c.state = I2C_ADDR_R_WAIT;
        }
        break;

    case I2C_ADDR_R_WAIT:
        if (sr1 & I2C_SR1_ADDR)
        {
            volatile uint32_t tmp;
            tmp = I2C1->SR1;
            tmp = I2C1->SR2; 
            (void)tmp;

            if (i2c.len == 1)
            {
                I2C1->CR1 &= ~I2C_CR1_ACK;   // NACK
                I2C1->CR1 |= I2C_CR1_STOP;  
            }
            else
            {
                I2C1->CR1 |= I2C_CR1_ACK;    // enable ACK for multi-byte
            }

            i2c.state = I2C_READ;
        }
        break;

    // =========================
    // READ DATA
    // =========================
    case I2C_READ:
        if (sr1 & I2C_SR1_RXNE)
        {
            i2c.buf[i2c.idx++] = I2C1->DR;

            if (i2c.idx == i2c.len - 1)
            {
                I2C1->CR1 &= ~I2C_CR1_ACK;
                I2C1->CR1 |= I2C_CR1_STOP;
            }

            if (i2c.idx >= i2c.len)
            {
                i2c.state = I2C_DONE;
                i2c.busy = 0;

                I2C1->CR1 |= I2C_CR1_ACK; // restore

                if (i2c_cb)
                    i2c_cb();
            }
        }
        break;

    // =========================
    // STOP
    // =========================
    case I2C_STOP:
        if (sr1 & I2C_SR1_BTF)   
        {
            I2C1->CR1 |= I2C_CR1_STOP;

            i2c.state = I2C_DONE;
            i2c.busy = 0;

            I2C1->CR1 |= I2C_CR1_ACK;

            if (i2c_cb)
                i2c_cb();
        }

        break;

    default:
        break;
    }
}