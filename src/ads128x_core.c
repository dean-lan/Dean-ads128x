/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     dean-lan     first version
 */

#include <rtdbg.h>
#include "ads128x_internal.h"

/* Single-instance driver device (declared extern in ads128x_internal.h) */
struct ads128x_device ads128x_dev;

/* ===================== Chip info table =====================
 * The chip model is selected at compile time by Kconfig.
 * The register map is unified across the family (ID=0x00, CONFIG0=0x01, ...), verified
 * against the ADS1282 SBAS499 Rev I / ADS1282-HT / ADS1283 / ADS1284 datasheets.
 * Note: the ADS1281 register map is assumed to use the same map, confirm against
 * SBAS449. ADS1282-HT / ADS1282-SP share the ADS1282 map and need no separate entry.
 */
static const struct ads128x_chip_info ads128x_chips[ADS128X_CHIP_NUM] =
{
#if defined(ADS128X_USING_ADS1281)
    [ADS128X_ADS1281] = { .name = "ads1281", .resolution = 31,
                          .flags = ADS128X_CHIP_F_NO_PGA | ADS128X_CHIP_F_SINGLE_CH },
#endif
#if defined(ADS128X_USING_ADS1282)
    [ADS128X_ADS1282] = { .name = "ads1282", .resolution = 31, .flags = 0 },
#endif
#if defined(ADS128X_USING_ADS1283)
    [ADS128X_ADS1283] = { .name = "ads1283", .resolution = 31, .flags = 0 },
#endif
#if defined(ADS128X_USING_ADS1284)
    [ADS128X_ADS1284] = { .name = "ads1284", .resolution = 31, .flags = 0 },
#endif
};

/* Low-level SPI transfer: returns RT_EOK when the whole message chain is done (NULL),
 * otherwise an error */
static rt_err_t ads128x_transfer(struct ads128x_device *dev,
                                 const rt_uint8_t *tx, rt_uint8_t *rx, rt_size_t len)
{
    struct rt_spi_message msg;
    struct rt_spi_message *ret;

    msg.send_buf    = tx;
    msg.recv_buf    = rx;
    msg.length      = len;
    msg.cs_take     = 1;
    msg.cs_release  = 1;
    msg.next        = RT_NULL;

    ret = rt_spi_transfer_message(&dev->spi_dev, &msg);
    if (ret == RT_NULL)
    {
        return RT_EOK;
    }

    return -RT_EIO;
}

static rt_err_t ads128x_cmd(struct ads128x_device *dev, rt_uint8_t cmd)
{
    return ads128x_transfer(dev, &cmd, RT_NULL, 1);
}

/* Write a single register: 2-byte WREG command + 1 data byte, CS held low throughout.
 * Public API: also usable for custom calibration (OFC0-2/FSC0-2) and diagnostics. */
rt_err_t ads128x_write_reg(struct ads128x_device *dev, rt_uint8_t reg, rt_uint8_t val)
{
    rt_uint8_t buf[3];
    rt_err_t ret;

    if (dev->rdatac_mode)
    {
        /* RREG/WREG are not allowed in read-data-continuous mode, exit it first */
        ret = ads128x_cmd(dev, ADS128X_CMD_SDATAC);
        if (ret != RT_EOK)
        {
            return ret;
        }
        dev->rdatac_mode = RT_FALSE;
    }

    buf[0] = ADS128X_CMD_WREG | reg;
    buf[1] = 0x00;      /* nnnnn = number of registers - 1, 0 for a single register */
    buf[2] = val;

    return ads128x_transfer(dev, buf, RT_NULL, sizeof(buf));
}

/* Read a single register: after the 2-byte RREG command, the data shifts out in the
 * 3rd byte position. Public API: for custom calibration and diagnostics. */
rt_err_t ads128x_read_reg(struct ads128x_device *dev, rt_uint8_t reg, rt_uint8_t *val)
{
    rt_uint8_t tx[3] = { ADS128X_CMD_RREG | reg, 0x00, 0x00 };
    rt_uint8_t rx[3] = { 0, 0, 0 };
    rt_err_t ret;

    if (dev->rdatac_mode)
    {
        /* RREG requires exiting read-data-continuous mode first */
        ret = ads128x_cmd(dev, ADS128X_CMD_SDATAC);
        if (ret != RT_EOK)
        {
            return ret;
        }
        dev->rdatac_mode = RT_FALSE;
    }

    ret = ads128x_transfer(dev, tx, rx, sizeof(tx));
    if (ret == RT_EOK)
    {
        *val = rx[2];
    }

    return ret;
}

/* ===================== Wait for data ready =====================
 * DRDY is active low; when no DRDY pin is configured, return immediately (unconditional
 * read). Polling granularity is 1ms; for high data rates use the interrupt +
 * ads128x_drdy_isr() approach.
 */
rt_err_t ads128x_wait_data(struct ads128x_device *dev, rt_int32_t timeout)
{
    rt_tick_t start;

    if (dev->drdy_pin < 0)
    {
        return RT_EOK;
    }

    start = rt_tick_get();
    while (rt_tick_get() - start < rt_tick_from_millisecond(timeout))
    {
        if (rt_pin_read(dev->drdy_pin) == PIN_LOW)
        {
            return RT_EOK;
        }
        rt_thread_mdelay(1);
    }

    return -RT_ETIMEOUT;
}

/* DRDY falling-edge ISR entry: release the data-ready semaphore */
void ads128x_drdy_isr(struct ads128x_device *dev)
{
    rt_sem_release(&dev->drdy_sem);
}

/* ===================== Data read ===================== */
rt_int32_t ads128x_read_data(struct ads128x_device *dev)
{
    rt_uint8_t tx[1], rx[4] = { 0, 0, 0, 0 };
    struct rt_spi_message msg[2];
    rt_uint32_t raw = 0;
    rt_int32_t value = 0;

    if (ads128x_wait_data(dev, ADS128X_DRDY_TIMEOUT) != RT_EOK)
    {
        LOG_E("ads128x: DRDY wait timeout");
        return 0;
    }

    if (dev->rdatac_mode)
    {
        /* Read-data-continuous mode: clock out 32 bits right after CS is pulled low */
        ads128x_transfer(dev, RT_NULL, rx, sizeof(rx));
    }
    else
    {
        /* Read by command: RDATA command byte + 32-bit data, CS held low throughout */
        tx[0] = ADS128X_CMD_RDATA;
        msg[0].send_buf    = tx;
        msg[0].recv_buf    = RT_NULL;
        msg[0].length      = 1;
        msg[0].cs_take     = 1;
        msg[0].cs_release  = 0;
        msg[0].next        = &msg[1];

        msg[1].send_buf    = RT_NULL;
        msg[1].recv_buf    = rx;
        msg[1].length      = sizeof(rx);
        msg[1].cs_take     = 0;
        msg[1].cs_release  = 1;
        msg[1].next        = RT_NULL;

        rt_spi_transfer_message(&dev->spi_dev, msg);
    }

    raw = ((rt_uint32_t)rx[0] << 24) | ((rt_uint32_t)rx[1] << 16) |
          ((rt_uint32_t)rx[2] << 8) | (rt_uint32_t)rx[3];

    /* Data extraction:
     * High-resolution mode (MODE=1, default) outputs 32-bit two's complement;
     * low-power mode (MODE=0) outputs an 8-bit status byte + 24-bit data */
    if ((dev->config0 & ADS128X_CFG0_MODE) && dev->chip->resolution > 24)
    {
        value = (rt_int32_t)raw;
    }
    else
    {
        value = (rt_int32_t)(raw & 0x00FFFFFFUL);
        if (value & 0x00800000)
        {
            value |= (rt_int32_t)0xFF000000;    /* Sign-extend the 24-bit two's complement */
        }
    }

    return value;
}

rt_err_t ads128x_start_continuous(struct ads128x_device *dev)
{
    rt_err_t ret;

    if (dev->rdatac_mode)
    {
        return RT_EOK;
    }

    ret = ads128x_cmd(dev, ADS128X_CMD_RDATAC);
    if (ret == RT_EOK)
    {
        dev->rdatac_mode = RT_TRUE;
    }

    return ret;
}

rt_err_t ads128x_stop_continuous(struct ads128x_device *dev)
{
    rt_err_t ret;

    if (!dev->rdatac_mode)
    {
        return RT_EOK;
    }

    ret = ads128x_cmd(dev, ADS128X_CMD_SDATAC);
    if (ret == RT_EOK)
    {
        dev->rdatac_mode = RT_FALSE;
    }

    return ret;
}

/* ===================== Configuration ===================== */
rt_err_t ads128x_set_data_rate(struct ads128x_device *dev, rt_uint32_t sps)
{
    rt_uint8_t dr;

    switch (sps)
    {
    case 250:   dr = 0; break;
    case 500:   dr = 1; break;
    case 1000:  dr = 2; break;
    case 2000:  dr = 3; break;
    case 4000:  dr = 4; break;
    default:    LOG_E("ads128x: unsupported data rate %d", sps); return -RT_EINVAL;
    }

    dev->config0 = (rt_uint8_t)(dev->config0 & ~ADS128X_CFG0_DR(0x07));
    dev->config0 = (rt_uint8_t)(dev->config0 | ADS128X_CFG0_DR(dr));

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG0, dev->config0);
}

rt_err_t ads128x_set_gain(struct ads128x_device *dev, rt_uint8_t gain)
{
    rt_uint8_t pga;

    switch (gain)
    {
    case 1:     pga = 0; break;
    case 2:     pga = 1; break;
    case 4:     pga = 2; break;
    case 8:     pga = 3; break;
    case 16:    pga = 4; break;
    case 32:    pga = 5; break;
    case 64:    pga = 6; break;
    default:    LOG_E("ads128x: unsupported gain %d", gain); return -RT_EINVAL;
    }

    dev->config1 = (rt_uint8_t)(dev->config1 & ~ADS128X_CFG1_PGA(0x07));
    dev->config1 = (rt_uint8_t)(dev->config1 | ADS128X_CFG1_PGA(pga));

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG1, dev->config1);
}

rt_err_t ads128x_set_mux(struct ads128x_device *dev, rt_uint8_t mux)
{
    if (mux > 4)
    {
        LOG_E("ads128x: unsupported mux %d", mux);
        return -RT_EINVAL;
    }

    dev->config1 = (rt_uint8_t)(dev->config1 & ~ADS128X_CFG1_MUX(0x07));
    dev->config1 = (rt_uint8_t)(dev->config1 | ADS128X_CFG1_MUX(mux));

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG1, dev->config1);
}

rt_err_t ads128x_set_filter(struct ads128x_device *dev, enum ads128x_filter filter)
{
    if (filter > ADS128X_FILTER_SINC_LPF_HPF)
    {
        return -RT_EINVAL;
    }

    dev->config0 = (rt_uint8_t)(dev->config0 & ~ADS128X_CFG0_FILTR(0x03));
    dev->config0 = (rt_uint8_t)(dev->config0 | ADS128X_CFG0_FILTR(filter));

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG0, dev->config0);
}

/* Filter phase: 0 = linear phase (default), 1 = minimum phase */
rt_err_t ads128x_set_phase(struct ads128x_device *dev, rt_bool_t minimum)
{
    if (minimum)
    {
        dev->config0 = (rt_uint8_t)(dev->config0 | ADS128X_CFG0_PHS);
    }
    else
    {
        dev->config0 = (rt_uint8_t)(dev->config0 & ~ADS128X_CFG0_PHS);
    }

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG0, dev->config0);
}

/* High-pass filter corner frequency: raw 16-bit HPF register value.
 * The value depends on the data rate, see the datasheet formula (e.g. 0.5Hz@250SPS=0x0337,
 * 1Hz@500SPS=0x0337, 1Hz@1000SPS=0x019A). Only effective when the filter includes the HPF
 * stage (ADS128X_FILTER_SINC_LPF_HPF). */
rt_err_t ads128x_set_hpf(struct ads128x_device *dev, rt_uint16_t hpf)
{
    rt_err_t ret = ads128x_write_reg(dev, ADS128X_REG_HPF0, (rt_uint8_t)(hpf & 0xFF));
    if (ret != RT_EOK)
    {
        return ret;
    }
    return ads128x_write_reg(dev, ADS128X_REG_HPF1, (rt_uint8_t)(hpf >> 8));
}

rt_err_t ads128x_set_mode(struct ads128x_device *dev, rt_bool_t high_res)
{
    if (high_res)
    {
        dev->config0 = (rt_uint8_t)(dev->config0 | ADS128X_CFG0_MODE);
    }
    else
    {
        dev->config0 = (rt_uint8_t)(dev->config0 & ~ADS128X_CFG0_MODE);
    }

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG0, dev->config0);
}

/* ===================== Operation control ===================== */
rt_err_t ads128x_reset(struct ads128x_device *dev)
{
    rt_err_t ret = RT_EOK;

    if (dev->reset_pin >= 0)
    {
        /* Hardware reset: pull RESET low for at least one fCLK period */
        rt_pin_write(dev->reset_pin, PIN_LOW);
        rt_hw_us_delay(10);
        rt_pin_write(dev->reset_pin, PIN_HIGH);
        rt_thread_mdelay(10);
    }
    else
    {
        ret = ads128x_cmd(dev, ADS128X_CMD_RESET);
        if (ret != RT_EOK)
        {
            return ret;
        }
        rt_thread_mdelay(10);
    }

    /* Cache reset defaults: CONFIG0=0x52 (high-res/1000SPS/sinc+LPF), CONFIG1=0x08 */
    dev->config0 = 0x52;
    dev->config1 = 0x08;
    dev->rdatac_mode = RT_FALSE;

    return RT_EOK;
}

rt_err_t ads128x_standby(struct ads128x_device *dev)
{
    return ads128x_cmd(dev, ADS128X_CMD_STANDBY);
}

rt_err_t ads128x_wakeup(struct ads128x_device *dev)
{
    rt_err_t ret = ads128x_cmd(dev, ADS128X_CMD_WAKEUP);

    if (ret == RT_EOK)
    {
        rt_thread_mdelay(10);   /* Re-synchronization needed after leaving standby */
    }

    return ret;
}

/* Issue the SYNC command: resets the digital filter and modulator to align conversions */
rt_err_t ads128x_sync(struct ads128x_device *dev)
{
    return ads128x_cmd(dev, ADS128X_CMD_SYNC);
}

/* Synchronization mode (CONFIG0 SYNC bit): 0 = pulse sync (default), 1 = continuous sync */
rt_err_t ads128x_set_sync_mode(struct ads128x_device *dev, rt_bool_t continuous)
{
    if (continuous)
    {
        dev->config0 = (rt_uint8_t)(dev->config0 | ADS128X_CFG0_SYNC);
    }
    else
    {
        dev->config0 = (rt_uint8_t)(dev->config0 & ~ADS128X_CFG0_SYNC);
    }

    return ads128x_write_reg(dev, ADS128X_REG_CONFIG0, dev->config0);
}

/* Optional hardware power-down pin (PWDN, active high). Default is -1 (unused), in which
 * case power_down/power_up fall back to the STANDBY/WAKEUP commands. */
rt_err_t ads128x_set_pwdn_pin(struct ads128x_device *dev, rt_base_t pwdn_pin)
{
    dev->pwdn_pin = pwdn_pin;
    if (pwdn_pin >= 0)
    {
        rt_pin_mode(pwdn_pin, PIN_MODE_OUTPUT);
    }
    return RT_EOK;
}

rt_err_t ads128x_power_down(struct ads128x_device *dev)
{
    if (dev->pwdn_pin >= 0)
    {
        rt_pin_write(dev->pwdn_pin, PIN_HIGH);
        return RT_EOK;
    }
    return ads128x_cmd(dev, ADS128X_CMD_STANDBY);
}

rt_err_t ads128x_power_up(struct ads128x_device *dev)
{
    rt_uint8_t config0, config1;
    rt_err_t ret;

    if (dev->pwdn_pin >= 0)
    {
        rt_pin_write(dev->pwdn_pin, PIN_LOW);
        rt_thread_mdelay(10);
    }

    /* Register settings are not retained across power-down: save, reset, restore */
    config0 = dev->config0;
    config1 = dev->config1;

    ret = ads128x_reset(dev);
    if (ret != RT_EOK)
    {
        return ret;
    }

    dev->config0 = config0;
    dev->config1 = config1;
    ret = ads128x_write_reg(dev, ADS128X_REG_CONFIG0, dev->config0);
    if (ret != RT_EOK)
    {
        return ret;
    }
    return ads128x_write_reg(dev, ADS128X_REG_CONFIG1, dev->config1);
}

rt_err_t ads128x_offset_cal(struct ads128x_device *dev)
{
    return ads128x_cmd(dev, ADS128X_CMD_OFSCAL);
}

rt_err_t ads128x_gain_cal(struct ads128x_device *dev)
{
    return ads128x_cmd(dev, ADS128X_CMD_GANCAL);
}

/* Device check: read the ID register (0x00, read-only) and print it.
 * The low nibble of the ID is always 0 and the high nibble is a factory-programmed
 * identifier; TI does not publish per-model ID values, so only a sanity check is done
 * (warning when the low nibble is non-zero). It cannot distinguish chip models. */
rt_err_t ads128x_check_id(struct ads128x_device *dev)
{
    rt_uint8_t id = 0;
    rt_err_t ret = ads128x_read_reg(dev, ADS128X_REG_ID, &id);

    if (ret != RT_EOK)
    {
        LOG_E("ads128x: read ID register failed, err=%d", ret);
        return ret;
    }

    if ((id & 0x0F) != 0)
    {
        LOG_W("ads128x: unexpected ID 0x%02X (low nibble should be 0)", id);
        return -RT_ERROR;
    }

    LOG_I("ads128x: device ID = 0x%02X (%s detected)", id, dev->chip->name);
    return RT_EOK;
}

/* ===================== Initialization ===================== */
ads128x_device_t ads128x_find(void)
{
    if (ads128x_dev.spi_dev.bus == RT_NULL)
    {
        return RT_NULL;
    }
    return &ads128x_dev;
}

rt_err_t ads128x_init(const char *spi_bus_name, rt_base_t cs_pin,
                      rt_base_t drdy_pin, rt_base_t reset_pin)
{
    struct ads128x_device *dev = &ads128x_dev;
    struct rt_spi_configuration cfg;
    rt_err_t ret;

    if (dev->spi_dev.bus != RT_NULL)
    {
        LOG_W("ads128x: already initialized");
        return -RT_EBUSY;
    }

    dev->drdy_pin  = drdy_pin;
    dev->reset_pin = reset_pin;
    dev->pwdn_pin  = -1;

    /* Select the chip model specified at compile time */
#if defined(ADS128X_USING_ADS1281)
    dev->chip = &ads128x_chips[ADS128X_ADS1281];
#elif defined(ADS128X_USING_ADS1282)
    dev->chip = &ads128x_chips[ADS128X_ADS1282];
#elif defined(ADS128X_USING_ADS1283)
    dev->chip = &ads128x_chips[ADS128X_ADS1283];
#elif defined(ADS128X_USING_ADS1284)
    dev->chip = &ads128x_chips[ADS128X_ADS1284];
#else
    LOG_E("ads128x: no chip model selected, check Kconfig");
    return -RT_EINVAL;
#endif

    /* Attach the ADS128x SPI slave device to the bus */
    ret = rt_spi_bus_attach_device_cspin(&dev->spi_dev, ADS128X_SPI_DEV_NAME,
                                         spi_bus_name, cs_pin, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_E("ads128x: attach to %s failed, err=%d", spi_bus_name, ret);
        return ret;
    }

    /* SPI parameters: Mode 1 (CPOL=0, CPHA=1), 8-bit, up to 20MHz, 10MHz recommended */
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.mode       = RT_SPI_MODE_1;
    cfg.data_width = 8;
    cfg.max_hz     = ADS128X_SPI_MAX_HZ;
    ret = rt_spi_configure(&dev->spi_dev, &cfg);
    if (ret != RT_EOK)
    {
        LOG_E("ads128x: spi configure failed, err=%d", ret);
        return ret;
    }

    /* Pin initialization */
    if (drdy_pin >= 0)
    {
        rt_pin_mode(drdy_pin, PIN_MODE_INPUT_PULLUP);
    }
    if (reset_pin >= 0)
    {
        rt_pin_mode(reset_pin, PIN_MODE_OUTPUT);
    }
    rt_sem_init(&dev->drdy_sem, "adsdrdy", 0, RT_IPC_FLAG_FIFO);

    /* Reset and write default config: high-resolution mode, 250SPS, sinc+LPF+HPF,
     * PGA=1, MUX=AINP1/AINN1 */
    ret = ads128x_reset(dev);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ads128x_check_id(dev);
    ret = ads128x_set_mode(dev, RT_TRUE);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ads128x_set_data_rate(dev, 250);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ads128x_set_filter(dev, ADS128X_FILTER_SINC_LPF_HPF);
    if (ret != RT_EOK)
    {
        return ret;
    }
    /* ADS1281 has no PGA and a single channel: skip gain/MUX configuration */
    if (!(dev->chip->flags & ADS128X_CHIP_F_NO_PGA))
    {
        ads128x_set_gain(dev, 1);
    }
    if (!(dev->chip->flags & ADS128X_CHIP_F_SINGLE_CH))
    {
        ads128x_set_mux(dev, 0);
    }

    LOG_I("ads128x: %s driver initialized on %s", dev->chip->name, spi_bus_name);

    return RT_EOK;
}
