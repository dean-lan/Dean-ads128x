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

#if defined(ADS128X_USING_ADC_DEVICE)

/* ===================== RT-Thread ADC device wrapper =====================
 * Registers the single ADS128x instance as an RT-Thread ADC device named
 * "adc_ads128x" (ADS128X_ADC_DEV_NAME), so it can be used through the standard
 * rt_adc_open/read/control interface. Compile-gated by ADS128X_USING_ADC_DEVICE. */

static rt_err_t ads128x_adc_enabled(struct rt_adc_device *device, rt_int8_t channel, rt_bool_t enabled)
{
    struct ads128x_device *dev = rt_container_of(device, struct ads128x_device, adc);

    RT_UNUSED(dev);
    RT_UNUSED(channel);
    RT_UNUSED(enabled);
    return RT_EOK;
}

static rt_err_t ads128x_adc_convert(struct rt_adc_device *device, rt_int8_t channel, rt_uint32_t *value)
{
    struct ads128x_device *dev = rt_container_of(device, struct ads128x_device, adc);

    RT_UNUSED(channel);
    *value = (rt_uint32_t)ads128x_read_data(dev);
    return RT_EOK;
}

static rt_uint8_t ads128x_adc_get_resolution(struct rt_adc_device *device)
{
    struct ads128x_device *dev = rt_container_of(device, struct ads128x_device, adc);

    return dev->chip->resolution;
}

static rt_int16_t ads128x_adc_get_vref(struct rt_adc_device *device)
{
    RT_UNUSED(device);
    return 2500;    /* mV, depends on the hardware reference voltage; adjust as needed */
}

static const struct rt_adc_ops ads128x_adc_ops =
{
    .enabled = ads128x_adc_enabled,
    .convert = ads128x_adc_convert,
    .get_resolution = ads128x_adc_get_resolution,
    .get_vref = ads128x_adc_get_vref,
};

rt_err_t ads128x_adc_register(void)
{
    struct ads128x_device *dev = &ads128x_dev[0];
    rt_err_t ret;

    if (dev->spi_dev.bus == RT_NULL)
    {
        LOG_E("ads128x: not initialized, call ads128x_init first");
        return -RT_ERROR;
    }

    ret = rt_hw_adc_register(&dev->adc, ADS128X_ADC_DEV_NAME, &ads128x_adc_ops, dev);
    if (ret != RT_EOK)
    {
        LOG_E("ads128x: register adc device failed, err=%d", ret);
        return ret;
    }

    LOG_I("ads128x: registered as %s", ADS128X_ADC_DEV_NAME);
    return RT_EOK;
}

#endif /* defined(ADS128X_USING_ADC_DEVICE) */
