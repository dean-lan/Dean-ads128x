/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     dean-lan     first version
 */

/**
 * @file ads128x_internal.h
 * @brief Internal definitions of the ADS128x driver. NOT installed / NOT public.
 *
 * Contains the driver device structure, register/command definitions, the chip info
 * table and the single driver instance. Only used by src/ads128x_core.c and
 * src/ads128x_adc.c. Applications must only include "ads128x.h".
 */

#ifndef __ADS128X_INTERNAL_H__
#define __ADS128X_INTERNAL_H__

#include "ads128x.h"
#include <rtdevice.h>
#include <drivers/dev_spi.h>
#include <drivers/adc.h>

/* SPI slave device name (internal, used for bus attachment) */
#define ADS128X_SPI_DEV_NAME    "ads128x"

/* ===================== Commands (identical across the family) ===================== */
#define ADS128X_CMD_WAKEUP      0x00    /* Wake up from standby */
#define ADS128X_CMD_STANDBY     0x02    /* Enter standby */
#define ADS128X_CMD_SYNC        0x04    /* Synchronize conversion */
#define ADS128X_CMD_RESET       0x06    /* Reset registers to default values */
#define ADS128X_CMD_RDATAC      0x10    /* Enter read-data-continuous mode */
#define ADS128X_CMD_SDATAC      0x11    /* Exit read-data-continuous mode */
#define ADS128X_CMD_RDATA       0x12    /* Read data by command */
#define ADS128X_CMD_RREG        0x20    /* Read register, OR with register address */
#define ADS128X_CMD_WREG        0x40    /* Write register, OR with register address */
#define ADS128X_CMD_OFSCAL      0x60    /* Offset calibration */
#define ADS128X_CMD_GANCAL      0x61    /* Gain calibration */

/* ===================== Register map (unified across the family, verified) ===================== */
#define ADS128X_REG_ID          0x00    /* Device ID (read-only, bits7:4=ID, bits3:0=0) */
#define ADS128X_REG_CONFIG0     0x01    /* Configuration 0, reset value 0x52 */
#define ADS128X_REG_CONFIG1     0x02    /* Configuration 1, reset value 0x08 */
#define ADS128X_REG_HPF0        0x03    /* High-pass filter, low byte, reset 0x32 */
#define ADS128X_REG_HPF1        0x04    /* High-pass filter, high byte, reset 0x03 */
#define ADS128X_REG_OFC0        0x05
#define ADS128X_REG_OFC1        0x06
#define ADS128X_REG_OFC2        0x07
#define ADS128X_REG_FSC0        0x08
#define ADS128X_REG_FSC1        0x09
#define ADS128X_REG_FSC2        0x0A    /* Reset 0x40 */

/* ===================== CONFIG0/CONFIG1 bit fields ===================== */
/* CONFIG0 */
#define ADS128X_CFG0_SYNC       (1 << 7)    /* 0: pulse sync, 1: continuous sync */
#define ADS128X_CFG0_MODE       (1 << 6)    /* 0: low-power mode, 1: high-resolution mode (default) */
#define ADS128X_CFG0_DR(rate)   ((rate) << 3)   /* Data rate: 0=250,1=500,2=1000,3=2000,4=4000 SPS */
#define ADS128X_CFG0_PHS        (1 << 2)    /* 0: linear phase, 1: minimum phase */
#define ADS128X_CFG0_FILTR(f)   ((f) & 0x03)    /* 0: bypass, 1: sinc, 2: sinc+LPF (default), 3: sinc+LPF+HPF */
/* CONFIG1 */
#define ADS128X_CFG1_MUX(mux)   ((mux) << 4)    /* 0: AINP1/AINN1, 1: AINP2/AINN2, 2: internal short, ... */
#define ADS128X_CFG1_CHOP       (1 << 3)    /* PGA chopping (enabled by default) */
#define ADS128X_CFG1_PGA(gain)  ((gain) & 0x07) /* 0:1,1:2,2:4,3:8,4:16,5:32,6:64 */

/* ===================== Chip model ===================== */
enum ads128x_chip_id
{
    ADS128X_ADS1281 = 0,
    ADS128X_ADS1282,
    ADS128X_ADS1283,
    ADS128X_ADS1284,
    ADS128X_CHIP_NUM,
};

/* Chip capability flags */
#define ADS128X_CHIP_F_NO_PGA       (1 << 0)    /* No programmable gain amplifier */
#define ADS128X_CHIP_F_SINGLE_CH    (1 << 1)    /* Single input channel (no MUX) */

struct ads128x_chip_info
{
    const char *name;       /* Chip model name */
    rt_uint8_t resolution;  /* Output resolution (bits) */
    rt_uint8_t flags;       /* Capability flags */
};

/* ===================== Driver device structure ===================== */
struct ads128x_device
{
    struct rt_adc_device adc;               /* RT-Thread ADC device (used by the wrapper) */
    struct rt_spi_device spi_dev;           /* SPI slave device */
    const struct ads128x_chip_info *chip;   /* Chip parameters */
    rt_uint8_t config0;                     /* Cached CONFIG0 value */
    rt_uint8_t config1;                     /* Cached CONFIG1 value */
    rt_base_t drdy_pin;                     /* Data-ready pin */
    rt_base_t reset_pin;                    /* Reset pin */
    rt_base_t pwdn_pin;                     /* Power-down pin (optional, -1 if unused) */
    struct rt_semaphore drdy_sem;           /* Data-ready semaphore */
    rt_bool_t rdatac_mode;                  /* In read-data-continuous mode */
};

/* Single driver instance, defined in ads128x_core.c */
extern struct ads128x_device ads128x_dev;

#endif /* __ADS128X_INTERNAL_H__ */
