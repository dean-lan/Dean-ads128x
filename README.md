# ADS128X

## 1. Introduction

**ads128x** is an RT-Thread software package that provides an SPI driver for the TI ADS128x family of ultra-high-resolution delta-sigma analog-to-digital converters. It is a companion driver of the [DeanDAQ](https://github.com/dean-lan/DeanDAQ) data-acquisition framework: continuous samples can be published into DeanDAQ topics with zero-copy delivery.

Supported chip models (one code base, chip selected at compile time):

| Model | Resolution | Description |
| ----- | ---------- | ----------- |
| ADS1281 | 31-bit | Single channel, no PGA |
| ADS1282 | 31-bit | 2-channel with PGA (baseline) |
| ADS1282-HT / ADS1282-SP | 31-bit | High-temp / space-grade ADS1282, no separate entry needed |
| ADS1283 | 31-bit | Low-power variant |
| ADS1284 | 31-bit | Dual power mode |

The family shares the same SPI command protocol, data frame format and register map (verified against the ADS1282 SBAS499 Rev I, ADS1282-HT, ADS1283 and ADS1284 datasheets). The ADS1281 register map is assumed to use the same map; confirm against SBAS449.

### License

- Apache-2.0, see `LICENSE`.

## 2. How to Get

1. Configure in menuconfig:

```
RT-Thread online packages
    peripheral libraries and drivers  --->
        [*] ads128x: TI ADS128x ultra-high-resolution delta-sigma ADC driver
```

2. Select the chip model, SPI bus name and pin numbers, save and run `pkgs --update`.

3. Enable the package dependencies: the SPI device framework (`RT_USING_SPI`) and the ADC device framework (`RT_USING_ADC`).

## 3. Usage

### 3.1 Hardware Connection

| ADS128x Pin | MCU Pin |
| ----------- | ------- |
| SCLK        | SPI SCLK |
| SDIN (DIN)  | SPI MOSI |
| SDOUT (DOUT)| SPI MISO |
| CS          | Any GPIO |
| DRDY        | Any GPIO (low indicates data ready) |
| RESET       | Any GPIO (optional) |

SPI parameters: Mode 1 (CPOL=0, CPHA=1), MSB first, 8-bit, up to 20MHz (10MHz recommended).

### 3.2 Initialization

```c
#include "ads128x.h"

/* SPI1 bus, CS=PA4(4), DRDY=PA0(0), RESET=PA1(1) */
ads128x_init("spi1", 4, 0, 1);

/* Optional: register as an RT-Thread ADC device "adc_ads128x" */
ads128x_adc_register();

/* Get the (opaque) driver handle for the bare API */
ads128x_device_t dev = ads128x_find();
```

### 3.3 Reading Data

```c
/* Configuration */
ads128x_set_data_rate(dev, 1000);       /* 250/500/1000/2000/4000 SPS */
ads128x_set_gain(dev, 4);               /* 1/2/4/8/16/32/64 */
ads128x_set_filter(dev, ADS128X_FILTER_SINC_LPF_HPF);
ads128x_set_mux(dev, 0);                /* 0: AINP1/AINN1, 1: AINP2/AINN2 */

/* Continuous read */
ads128x_start_continuous(dev);
int32_t val = ads128x_read_data(dev);
ads128x_stop_continuous(dev);
```

For high data rates (>= 1000SPS), call `ads128x_drdy_isr()` in the DRDY falling-edge interrupt and take the data via the semaphore from an application thread, to avoid sample loss with polling.

### 3.4 ADC Device Interface

When `ADS128X_USING_ADC_DEVICE` is enabled and `ads128x_adc_register()` is called, the driver is available as an RT-Thread ADC device (device name `adc_ads128x`) through the standard interface:

```c
rt_adc_device_t adc = (rt_adc_device_t)rt_device_find("adc_ads128x");
rt_adc_read(adc, 0);
```

### 3.5 File Layout

```
Dean-ads128x/
├── include/
│   └── ads128x.h           # Public API: bare driver + device wrapper (the only header to include)
├── src/
│   ├── ads128x_internal.h  # Internal: device struct, register/command defines, chip table
│   ├── ads128x_core.c      # Bare driver core (SPI access, config, control, data read)
│   └── ads128x_adc.c       # Optional RT-Thread ADC device wrapper
├── Kconfig
├── SConscript
├── examples/ads128x_sample.c
└── README.md
```

### 3.6 msh Sample

Enable `ADS128X_SAMPLE` and run:

```
msh > ads128x_sample
```

## 4. API Reference

- `ads128x_init()`: initialize the driver and register the ADC device
- `ads128x_find()`: find the driver handle by device name
- `ads128x_set_data_rate()` / `ads128x_set_gain()` / `ads128x_set_mux()`: data rate / PGA gain / input channel configuration
- `ads128x_set_filter()` / `ads128x_set_phase()` / `ads128x_set_hpf()`: digital filter type, phase response, HPF corner frequency
- `ads128x_set_mode()`: switch between high-resolution / low-power mode
- `ads128x_start_continuous()` / `ads128x_stop_continuous()`: read-data-continuous mode control
- `ads128x_read_data()`: read one conversion result
- `ads128x_wait_data()` / `ads128x_drdy_isr()`: wait for data ready / ISR entry
- `ads128x_reset()` / `ads128x_standby()` / `ads128x_wakeup()`: operation control
- `ads128x_sync()` / `ads128x_set_sync_mode()`: conversion synchronization (multi-chip alignment)
- `ads128x_offset_cal()` / `ads128x_gain_cal()`: offset / gain calibration commands
- `ads128x_read_reg()` / `ads128x_write_reg()`: register access for custom calibration (OFC0-2/FSC0-2) and diagnostics
- `ads128x_set_pwdn_pin()` / `ads128x_power_down()` / `ads128x_power_up()`: optional hardware power-down pin control (falls back to STANDBY/WAKEUP commands when no pin is set)
- `ads128x_check_id()`: read the device ID register (0x00) as a sanity check and print it

## 5. Notes

- The data frame is fixed at 4 bytes: high-resolution mode (MODE=1, default) outputs 32-bit two's complement; low-power mode outputs an 8-bit status byte + 24-bit data. The driver extracts data automatically based on the MODE bit of `config0` and performs sign extension.
- `ads128x_check_id()` reads the ID register (0x00): TI does not publish per-model ID values, so only an existence check is possible (the low nibble is always 0). It cannot distinguish chip models; the model is still selected at compile time by Kconfig.
- When no DRDY pin is configured, reads do not wait and return the current data directly.
- Polling works at low data rates (<= 500SPS); use the interrupt approach for high data rates.
- The register map and data frame format of the ADS1281 have not been directly verified; confirm against SBAS449 before use.

## 6. Contact & Thanks

- Maintainer: [dean-lan](https://github.com/dean-lan)
