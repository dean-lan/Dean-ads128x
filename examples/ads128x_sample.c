/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     dean-lan     first version
 */

#include <rtthread.h>
#include "ads128x.h"

/* Turn the Kconfig string macro into a string literal */
#define ADS128X_STR_(x) #x
#define ADS128X_STR(x)  ADS128X_STR_(x)

/* SPI bus name and pin numbers configured by Kconfig */
#define ADS128X_SAMPLE_BUS      ADS128X_STR(ADS128X_SPI_BUS_NAME)

static int ads128x_sample(int argc, char *argv[])
{
    ads128x_device_t dev;
    rt_int32_t value;
    int i;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    /* Initialize the bare driver (returns -RT_EBUSY on repeated calls, can be ignored) */
    ads128x_init(ADS128X_SAMPLE_BUS, ADS128X_CS_PIN,
                 ADS128X_DRDY_PIN, ADS128X_RESET_PIN);
    /* Optionally register as an RT-Thread ADC device */
    ads128x_adc_register();

    dev = ads128x_find();
    if (dev == RT_NULL)
    {
        rt_kprintf("ads128x sample: driver not initialized!\n");
        return -RT_ERROR;
    }

    /* Configure 1000SPS, PGA gain 4, channel AINP1/AINN1 */
    ads128x_set_data_rate(dev, 1000);
    ads128x_set_gain(dev, 4);
    ads128x_set_mux(dev, 0);
    /* HPF corner 1Hz @ 1000SPS = 0x019A (only effective with the HPF filter stage) */
    ads128x_set_hpf(dev, 0x019A);

    /* Enter read-data-continuous mode and read 5 conversion results */
    ads128x_start_continuous(dev);
    for (i = 0; i < 5; i++)
    {
        value = ads128x_read_data(dev);
        rt_kprintf("ads128x sample[%d]: %d\n", i, value);
    }
    ads128x_stop_continuous(dev);

    return RT_EOK;
}
MSH_CMD_EXPORT(ads128x_sample, ads128x adc sample);
