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
 * @file ads128x.h
 * @brief Public API of the TI ADS128x ultra-high-resolution delta-sigma ADC driver (SPI)
 *
 * This is the only header that applications need to include. The driver is split into:
 *   - a bare driver core (src/ads128x_core.c): low-level SPI access and configuration
 *   - an optional RT-Thread ADC device wrapper (src/ads128x_adc.c): registers an
 *     "adc_ads128x" device through ads128x_adc_register()
 *
 * The driver handle (ads128x_device_t) is opaque; all internal structures, register
 * definitions and helpers live in src/ads128x_internal.h and are not exported.
 *
 * Supported chip family (all confirmed on the TI website):
 *   ADS1281  (31-bit, single channel, no PGA)
 *   ADS1282  (31-bit, 2-channel with PGA)
 *   ADS1282-HT / ADS1282-SP (high-temp / space-grade ADS1282, same register map)
 *   ADS1283  (31-bit, low power) / ADS1284 (31-bit, dual power mode)
 *
 * Verified against the ADS1282 SBAS499 Rev I, ADS1282-HT, ADS1283 and ADS1284
 * datasheets. The ADS1281 register map is assumed to use the same map, confirm
 * against SBAS449.
 */

#ifndef __ADS128X_H__
#define __ADS128X_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADC device name registered by ads128x_adc_register() */
#define ADS128X_ADC_DEV_NAME    "adc_ads128x"

/* Maximum number of simultaneous devices (Kconfig ADS128X_MAX_DEVICES, default 1) */
#ifndef ADS128X_MAX_DEVICES
#define ADS128X_MAX_DEVICES     1
#endif

/* Opaque driver handle */
struct ads128x_device;
typedef struct ads128x_device *ads128x_device_t;

/* Filter selection (CONFIG0 FILTR field value) */
enum ads128x_filter
{
    ADS128X_FILTER_BYPASS = 0,  /* Modulator output, filter bypassed */
    ADS128X_FILTER_SINC,        /* Sinc only */
    ADS128X_FILTER_SINC_LPF,    /* Sinc + low-pass */
    ADS128X_FILTER_SINC_LPF_HPF,/* Sinc + low-pass + high-pass */
};

/* ===================== Bare driver API =====================
 * Initializes the SPI bus attachment, pins, reset and default configuration.
 * Does NOT register an RT-Thread device; call ads128x_adc_register() separately
 * if you want the "adc_ads128x" RT-Thread ADC device. */
rt_err_t   ads128x_init(const char *spi_bus_name, rt_base_t cs_pin,
                        rt_base_t drdy_pin, rt_base_t reset_pin);

/* Initialize one instance (idx < ADS128X_MAX_DEVICES) for multi-chip setups.
 * Each instance attaches to the SPI bus with its own CS; the bus itself is
 * arbitrated by the SPI device framework. */
rt_err_t   ads128x_init_ex(rt_uint8_t idx, const char *spi_bus_name, rt_base_t cs_pin,
                           rt_base_t drdy_pin, rt_base_t reset_pin);

/* Returns the driver handle of instance 0 if initialized, otherwise RT_NULL */
ads128x_device_t ads128x_find(void);

/* Returns the driver handle of instance idx if initialized, otherwise RT_NULL */
ads128x_device_t ads128x_get(rt_uint8_t idx);

/* Ownership: hand control of the device to `owner` (an aggregator such as
 * DeanAcq). While owned, configuration/control calls from other contexts are
 * rejected with -RT_EBUSY; data reads remain allowed. Only the owner can detach. */
rt_err_t   ads128x_attach(ads128x_device_t dev, void *owner);
rt_err_t   ads128x_detach(ads128x_device_t dev, void *owner);

/* Configuration */
rt_err_t   ads128x_set_data_rate(ads128x_device_t dev, rt_uint32_t sps);
rt_err_t   ads128x_set_gain(ads128x_device_t dev, rt_uint8_t gain);
rt_err_t   ads128x_set_mux(ads128x_device_t dev, rt_uint8_t mux);
rt_err_t   ads128x_set_filter(ads128x_device_t dev, enum ads128x_filter filter);
rt_err_t   ads128x_set_phase(ads128x_device_t dev, rt_bool_t minimum);
rt_err_t   ads128x_set_mode(ads128x_device_t dev, rt_bool_t high_res);
rt_err_t   ads128x_set_hpf(ads128x_device_t dev, rt_uint16_t hpf);   /* HPF corner (raw 16-bit value) */

/* Operation control */
rt_err_t   ads128x_reset(ads128x_device_t dev);
rt_err_t   ads128x_standby(ads128x_device_t dev);
rt_err_t   ads128x_wakeup(ads128x_device_t dev);
rt_err_t   ads128x_sync(ads128x_device_t dev);                        /* Issue the SYNC command */
rt_err_t   ads128x_set_sync_mode(ads128x_device_t dev, rt_bool_t continuous);
rt_err_t   ads128x_set_sync_pin(ads128x_device_t dev, rt_base_t sync_pin);
rt_err_t   ads128x_sync_hw(ads128x_device_t dev);                     /* Pulse the hardware SYNC pin */
rt_err_t   ads128x_offset_cal(ads128x_device_t dev);
rt_err_t   ads128x_gain_cal(ads128x_device_t dev);

/* Device check: read the ID register (0x00) and print it, used to confirm the chip is present */
rt_err_t   ads128x_check_id(ads128x_device_t dev);

/* Register access: for custom calibration (OFC0-2/FSC0-2), diagnostics, etc. */
rt_err_t   ads128x_read_reg(ads128x_device_t dev, rt_uint8_t reg, rt_uint8_t *val);
rt_err_t   ads128x_write_reg(ads128x_device_t dev, rt_uint8_t reg, rt_uint8_t val);

/* Optional hardware power-down pin (PWDN, active high) */
rt_err_t   ads128x_set_pwdn_pin(ads128x_device_t dev, rt_base_t pwdn_pin);
rt_err_t   ads128x_power_down(ads128x_device_t dev);
rt_err_t   ads128x_power_up(ads128x_device_t dev);

/* Data read: use read_data in continuous mode (start_continuous first) */
rt_err_t   ads128x_start_continuous(ads128x_device_t dev);
rt_err_t   ads128x_stop_continuous(ads128x_device_t dev);
rt_int32_t ads128x_read_data(ads128x_device_t dev);     /* Read one conversion result */
rt_err_t   ads128x_wait_data(ads128x_device_t dev, rt_int32_t timeout); /* Wait for DRDY */

/* ISR entry: call on the DRDY falling-edge interrupt to release the data-ready semaphore */
void       ads128x_drdy_isr(ads128x_device_t dev);

/* ===================== Device wrapper =====================
 * Registers the driver as an RT-Thread ADC device named "adc_ads128x"
 * (see ADS128X_ADC_DEV_NAME). Returns -RT_ERROR if the bare driver is not
 * initialized yet. Compile-gated by ADS128X_USING_ADC_DEVICE. */
rt_err_t   ads128x_adc_register(void);

/* Register instance idx as a standard acquisition device ("acq<idx>") through
 * the shared class (acq_device.h/.c): uniform RT_ACQ_CTRL_* commands, frame
 * reads and ownership. Compile-gated by ADS128X_USING_ACQDEV. */
rt_err_t   ads128x_acqdev_register(rt_uint8_t idx);

/* ===================== Multi-chip acquisition module =====================
 * Compiled with ADS128X_USING_ACQ (depends on the DeanDAQ package).
 * A single acquisition thread waits for data-ready, reads every initialized
 * device and packs one frame per conversion. The frame layout is:
 *
 *     rt_uint64 timestamp              -- conversion tick
 *     rt_int32  ch[ADS128X_MAX_DEVICES] -- one sample per device
 *
 * `batch` frames are accumulated into one ddaq_publish() call (batched
 * publish: publish rate = data rate / batch, fewer wakeups on the bus).
 * Call ads128x_acq_isr() from every device DRDY ISR; it only reads the sample
 * into a per-device software ring and releases the worker semaphore. */
#if defined(ADS128X_USING_ACQ)
struct ads128x_acq_frame
{
    rt_uint64_t timestamp;
    rt_int32_t  ch[ADS128X_MAX_DEVICES];
};

rt_err_t   ads128x_acq_start(rt_uint16_t topic_id, rt_uint16_t batch);
rt_err_t   ads128x_acq_stop(void);
rt_bool_t  ads128x_acq_is_running(void);
void       ads128x_acq_isr(ads128x_device_t dev, rt_uint8_t idx);
void       ads128x_acq_isr_all(void);   /* aggregated DRDY ISR: reads every initialized device */
rt_err_t   ads128x_acq_ctrl(int cmd, void *data);   /* group control (RT_ACQ_CTRL_*), shared/independent aware */
#endif /* defined(ADS128X_USING_ACQ) */

#ifdef __cplusplus
}
#endif

#endif /* __ADS128X_H__ */
