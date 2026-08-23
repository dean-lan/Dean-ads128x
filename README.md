# DEANACQ-DEV

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-C-blue.svg)]()
[![Platform](https://img.shields.io/badge/platform-RT--Thread-orange.svg)](https://www.rt-thread.org/)
[![DeanDAQ](https://img.shields.io/badge/deandaq-adapter-green.svg)](https://github.com/dean-lan/DeanDAQ)

**English** | [中文](README_zh.md)

## 1. Introduction

**DeanAcq-dev** (upgraded from `Dean-ads128x`) is an RT-Thread software package
that provides a **shared acquisition-device class** plus its first chip
implementation. It hosts:

- `acq_device.h` / `acq_device.c` — a public device class (like the `sensor`
  framework): a `struct rt_acq_device` embedded in every chip driver, a
  `struct rt_acq_ops` per chip, and shared registration/lookup/command-dispatch
  code so every chip (ADS128x today, ADXL/IMUs and other ADCs on SPI or I2C
  later) reuses the same `rt_device` layer.
- `ads128x` — the first implementation: a bare SPI driver plus the
  acquisition-class binding (`ads128x_acqdev_register()`), the RT-Thread ADC
  device wrapper and the multi-chip acquisition front-end.

Chips register as standard devices named `acq0`, `acq1`, ... and are driven
uniformly through `rt_device_find/open/control/read` with the `RT_ACQ_CTRL_*`
commands (rate, channel, gain, filter, start, stop, sync, reset, ownership,
info). Aggregators such as [DeanAcq](https://github.com/dean-lan/DeanAcq) only
see this uniform interface — they never special-case a concrete chip.

Supported chip models (one code base, chip selected at compile time):

| Model | Resolution | Description |
| ----- | ---------- | ----------- |
| ADS1281 | 31-bit | Single channel, no PGA |
| ADS1282 | 31-bit | 2-channel with PGA (baseline) |
| ADS1282-HT / ADS1282-SP | 31-bit | High-temp / space-grade ADS1282, no separate entry needed |
| ADS1283 | 31-bit | Low-power variant |
| ADS1284 | 31-bit | Dual power mode |

The family shares the same SPI command protocol, data frame format and register map (verified against the ADS1282 SBAS499 Rev I, ADS1282-HT, ADS1283 and ADS1284 datasheets). The ADS1281 register map is assumed to use the same map; confirm against SBAS449.

### Feature summary

- **Bare driver + optional RT-Thread ADC device** (`adc_ads128x`), see [Why both?](#why-bare-api--adc-device)
- **Multi-chip support**: up to `ADS128X_MAX_DEVICES` devices on one or more SPI buses, aligned via SYNC
- Full register/command set: filter, PGA gain, MUX, HPF, phase, sync, offset/gain calibration
- Two operating modes: high-resolution (32-bit) / low-power (8-bit status + 24-bit data), auto-extracted
- DRDY interrupt support for lossless acquisition up to 4 kSPS
- Optional DeanDAQ acquisition module: per-device ISR rings + batched frame publish
- Single code base for the whole family, chip model chosen in menuconfig

### License

- Apache-2.0, see `LICENSE`.

## 2. Quick Start

```c
#include "ads128x.h"

/* 1. Initialize: SPI bus "spi1", CS=PA4(4), DRDY=PA0(0), RESET=PA1(1) */
ads128x_init("spi1", 4, 0, 1);

/* 2. Optional: register as the RT-Thread ADC device "adc_ads128x" */
ads128x_adc_register();

/* 3. Use the bare API */
ads128x_device_t dev = ads128x_find();
ads128x_set_data_rate(dev, 1000);       /* 250/500/1000/2000/4000 SPS */
ads128x_set_gain(dev, 1);               /* 1/2/4/8/16/32/64 */

ads128x_start_continuous(dev);          /* RDATAC mode */
int32_t sample = ads128x_read_data(dev);
ads128x_stop_continuous(dev);
```

See [section 4](#4-usage) for hardware wiring, interrupt-based high-rate acquisition and DeanDAQ integration.

## 3. How to Get

1. Configure in menuconfig:

```
RT-Thread online packages
    peripheral libraries and drivers  --->
        [*] ads128x: TI ADS128x ultra-high-resolution delta-sigma ADC driver
            [*] Register as an RT-Thread ADC device
            [*] Register as a standard acquisition device (acqN, unified class)
            chip model (ADS1282 (31-bit))  --->
            Number of ADS128x devices (multi-chip support) (1)  --->
            [ ] Enable the DeanDAQ acquisition module (multi-chip batch publish)
            SPI bus name (spi1)  --->
            SPI clock frequency (Hz) (10000000)  --->
            Chip select pin number (-1)  --->
            Data ready (DRDY) pin number (-1)  --->
            Reset pin number (hardware reset) (-1)  --->
            DRDY wait timeout (ms) (100)  --->
            [ ] Enable sample command
```

2. Save and run `pkgs --update`.

3. Dependencies: the SPI device framework (`RT_USING_SPI`), the ADC device
   framework (`RT_USING_ADC`, when the ADC device wrapper is enabled) and, for
   the acquisition module, the [DeanDAQ](https://github.com/dean-lan/DeanDAQ)
   package (`PKG_USING_DDAQ`).

   This repository also hosts the shared acquisition-device class
   (`acq_device.h/.c`) consumed by the [DeanAcq](https://github.com/dean-lan/DeanAcq)
   aggregator: enable `PKG_USING_ADS128X` (this package) whenever you use
   DeanAcq, so its `acq_device.h` header is available.

## 4. Usage

### 4.1 Hardware Connection

| ADS128x Pin | MCU Pin |
| ----------- | ------- |
| SCLK        | SPI SCLK |
| SDIN (DIN)  | SPI MOSI |
| SDOUT (DOUT)| SPI MISO |
| CS          | Any GPIO |
| DRDY        | Any GPIO (low indicates data ready) |
| RESET       | Any GPIO (optional) |
| PWDN        | Any GPIO (optional, active high) |

SPI parameters: Mode 1 (CPOL=0, CPHA=1), MSB first, 8-bit, up to 20 MHz (10 MHz recommended).

### 4.2 Initialization

```c
#include "ads128x.h"

/* SPI1 bus, CS=PA4(4), DRDY=PA0(0), RESET=PA1(1) */
ads128x_init("spi1", 4, 0, 1);

/* Optional: register as an RT-Thread ADC device "adc_ads128x" */
ads128x_adc_register();

/* Get the (opaque) driver handle for the bare API */
ads128x_device_t dev = ads128x_find();
```

### 4.3 Standard Acquisition Device (acqN)

With `ADS128X_USING_ACQDEV` enabled, every instance is registered through the
shared acquisition-device class and driven uniformly via standard rt_device
interfaces (the same code works for any chip implementing the class):

```c
ads128x_acqdev_register(0);
ads128x_acqdev_register(1);

rt_device_t d = rt_device_find("acq0");
rt_device_open(d, RT_DEVICE_OFLAG_RDONLY);

rt_uint32_t hz = 1000;
rt_device_control(d, RT_ACQ_CTRL_SET_RATE, &hz);   /* uniform commands */
rt_device_control(d, RT_ACQ_CTRL_START, RT_NULL);

struct rt_acq_info info;
rt_device_control(d, RT_ACQ_CTRL_GET_INFO, &info);  /* model / channels / flags */

struct rt_acq_frame frame;
rt_device_read(d, 0, &frame, sizeof(frame));        /* one conversion */

/* Ownership: hand the device to an aggregator (e.g. DeanAcq). While owned,
 * every control command from other callers returns -RT_EBUSY; reads stay allowed. */
void *owner = ...;
rt_device_control(d, RT_ACQ_CTRL_ATTACH, owner);
...
rt_device_control(d, RT_ACQ_CTRL_DETACH, RT_NULL);
```

Capability flags (`info.flags`, `RT_ACQ_FLAG_*`) tell the aggregator which
operations a chip truly supports (SYNC/RESET/POWER/GAIN/FILTER/STREAM). Chips
that lack a hardware feature either emulate it in software inside their ops or
leave the op NULL (the framework returns `-RT_ENOSYS`).

### 4.4 Reading Data (polling)

```c
ads128x_set_data_rate(dev, 1000);       /* 250/500/1000/2000/4000 SPS */
ads128x_set_gain(dev, 4);               /* 1/2/4/8/16/32/64 */
ads128x_set_filter(dev, ADS128X_FILTER_SINC_LPF_HPF);
ads128x_set_mux(dev, 0);                /* 0: AINP1/AINN1, 1: AINP2/AINN2 */

ads128x_start_continuous(dev);
int32_t val = ads128x_read_data(dev);
ads128x_stop_continuous(dev);
```

Polling works well up to 500 SPS. For higher data rates use the interrupt
approach below.

### 4.5 High-Rate Acquisition (DRDY interrupt)

The ADS128x has **no internal FIFO**: a single 4-byte data register holds the
latest conversion, and a new conversion **overwrites** the previous one if it is
not read in time. The bottleneck is not SPI bandwidth (reading 4 bytes at 20 MHz
takes ~1.6 us, versus a 250 us sample period at 4 kSPS) but how promptly the MCU
responds to DRDY. Use the falling-edge DRDY interrupt and read from an
application thread:

```c
#include <rtdevice.h>

static void drdy_isr_entry(void *args)   /* DRDY falling-edge ISR */
{
    ads128x_drdy_isr(ads128x_find());    /* release the data-ready semaphore */
}

/* in init code: */
rt_pin_attach_irq(0, PIN_IRQ_MODE_FALLING, drdy_isr_entry, RT_NULL);
rt_pin_irq_enable(0, PIN_IRQ_ENABLE);

/* acquisition thread: */
void acq_thread_entry(void *param)
{
    ads128x_device_t dev = ads128x_find();

    while (1)
    {
        ads128x_wait_data(dev, RT_WAITING_FOREVER);  /* wakes on DRDY */
        int32_t val = ads128x_read_data(dev);
        /* ... use val ... */
    }
}
```

Performance tips:

- Raise the SPI clock to 10-20 MHz (`ADS128X_SPI_MAX_HZ`).
- Enable SPI DMA at the BSP layer to offload the CPU during the 4-byte read.
- `ads128x_start_continuous()` (RDATAC) keeps CS low and skips the per-sample
  RDATA command, saving SPI overhead.

### 4.6 DeanDAQ Integration

The driver pairs with the [DeanDAQ](https://github.com/dean-lan/DeanDAQ)
publish/subscribe bus: feed samples from the DRDY-driven acquisition thread into
a topic and let any number of subscribers consume them with zero-copy borrow:

```c
#include "ads128x.h"
#include <ddaq.h>
#include <ddaq_topics.h>   /* generated: struct ads128x_sample_s, DDAQ_ID(...) */

void acq_thread_entry(void *param)
{
    struct ads128x_sample_s msg;
    ads128x_device_t dev = ads128x_find();

    while (1)
    {
        ads128x_wait_data(dev, RT_WAITING_FOREVER);
        msg.timestamp = rt_tick_get();
        msg.value = ads128x_read_data(dev);
        ddaq_publish(DDAQ_ID(ads128x_sample), &msg, sizeof(msg));
    }
}
```

### 4.7 ADC Device Interface

When `ADS128X_USING_ADC_DEVICE` is enabled and `ads128x_adc_register()` is called,
the driver is available as an RT-Thread ADC device (device name `adc_ads128x`)
through the standard interface:

```c
rt_adc_device_t adc = (rt_adc_device_t)rt_device_find("adc_ads128x");
rt_adc_read(adc, 0);
```

#### Why bare API + ADC device?

- The RT-Thread ADC framework only models single-shot channel reads
  (`enabled` / `convert` / `get_resolution` / `get_vref`). It cannot express
  continuous acquisition, DRDY interrupts, gain/filter/HPF/sync/calibration.
- The bare API therefore exposes the full capability, and the device wrapper is
  a convenience for standard `rt_adc_read()` access.
- `ADS128X_USING_ADC_DEVICE` is enabled by default; disable it to save RAM/ROM.

### 4.8 File Layout

```
DeanAcq-dev/
├── include/
│   ├── acq_device.h          # Public acquisition-device class (shared by every chip)
│   └── ads128x/
│       └── ads128x.h         # ADS128x public API (bare driver + wrappers)
├── src/
│   ├── acq_device.c          # Shared class: register/find/read/control dispatch + ownership
│   └── ads128x/
│       ├── ads128x_internal.h  # Internal: device struct, register/command defines, chip table
│       ├── ads128x_core.c      # Bare driver core (SPI access, config, control, data read)
│       ├── ads128x_acqdev.c    # Acquisition-class binding (implements rt_acq_ops)
│       ├── ads128x_adc.c       # Optional RT-Thread ADC device wrapper
│       └── ads128x_acq.c       # Optional multi-chip front-end (ISR rings + batch + group ctrl)
├── Kconfig
├── SConscript
├── examples/ads128x_sample.c
└── README.md
```

### 4.9 Multi-Chip & DeanDAQ Acquisition

For multi-chip setups (e.g. several ADS1282s sharing one SPI bus, conversions
aligned via SYNC), initialize every instance and start the acquisition module:

```c
/* Instances 0..N-1, each with its own CS/DRDY/RESET pins (shared SPI bus OK) */
ads128x_init_ex(0, "spi1", CS0, DRDY0, RESET0);
ads128x_init_ex(1, "spi1", CS1, DRDY1, RESET1);

/* DeanDAQ acquisition module: one frame per conversion, 8 frames per publish */
ads128x_acq_start(DDAQ_ID(ads128x_acq), 8);
```

Compiled with `ADS128X_USING_ACQ` (requires `PKG_USING_DDAQ`), the module puts
every initialized device into RDATAC mode, issues SYNC for aligned conversions,
and then:

- **ISR side** (`ads128x_acq_isr()` from each DRDY interrupt): reads the fresh
  sample into a per-device software ring (depth `ADS128X_ACQ_RING_DEPTH`) and
  wakes the worker. The ISR stays short: one 4-byte SPI read plus a push.
- **Worker thread**: assembles one frame per conversion (a timestamp plus one
  sample per device), packs `batch` frames and publishes them with a single
  `ddaq_publish()` call (publish rate = data rate / batch).

The published payload is an array of `struct ads128x_acq_frame`:

```c
struct ads128x_acq_frame
{
    rt_uint64_t timestamp;
    rt_int32_t  ch[ADS128X_MAX_DEVICES];
};
```

Wire the DRDY ISRs:

```c
static void drdy0_isr(void *arg) { ads128x_acq_isr(ads128x_get(0), 0); }
static void drdy1_isr(void *arg) { ads128x_acq_isr(ads128x_get(1), 1); }
```

**Shared control lines** (multi-chip hardware variants):

- **Shared SYNC pin**: configure one GPIO and pulse it once to align every chip
  sharing the line (`ads128x_set_sync_pin()` / `ads128x_sync_hw()`); no need to
  send per-chip SYNC commands.
- **Shared DRDY (wired-OR)**: use a single aggregated ISR that reads every
  device in one go and wakes the worker once:

```c
static void drdy_shared_isr(void *arg) { ads128x_acq_isr_all(); }
```

Stop the module with `ads128x_acq_stop()`.

### 4.10 msh Sample

Enable `ADS128X_SAMPLE` in menuconfig and run:

```
msh > ads128x_sample
```

The sample initializes the driver with the configured pins, prints the ID
register and reads a few conversions. *Example output*:

```
[ads128x] ID register = 0x1A
[ads128x] sample[0] = 0000C2A1
[ads128x] sample[1] = FFFF3E50
```

## 5. API Reference

- `ads128x_init()`: initialize the driver and attach the SPI device
- `ads128x_find()`: find the driver handle (returns instance 0)
- `ads128x_init_ex()` / `ads128x_get()`: multi-chip instance initialization / accessor
- `ads128x_acq_start()` / `ads128x_acq_stop()` / `ads128x_acq_isr()`: DeanDAQ acquisition module (multi-chip, batched publish)
- `ads128x_acq_isr_all()`: aggregated DRDY ISR for a shared/wired-OR DRDY line (reads every device, one wakeup)
- `ads128x_set_data_rate()` / `ads128x_set_gain()` / `ads128x_set_mux()`: data rate / PGA gain / input channel configuration
- `ads128x_set_filter()` / `ads128x_set_phase()` / `ads128x_set_hpf()`: digital filter type, phase response, HPF corner frequency
- `ads128x_set_mode()`: switch between high-resolution / low-power mode
- `ads128x_start_continuous()` / `ads128x_stop_continuous()`: read-data-continuous (RDATAC) mode control
- `ads128x_read_data()`: read one conversion result
- `ads128x_wait_data()` / `ads128x_drdy_isr()`: wait for data ready / ISR entry
- `ads128x_reset()` / `ads128x_standby()` / `ads128x_wakeup()`: operation control
- `ads128x_sync()` / `ads128x_set_sync_mode()`: conversion synchronization (multi-chip alignment)
- `ads128x_set_sync_pin()` / `ads128x_sync_hw()`: hardware SYNC pin (shared SYNC line, pulse aligns all chips; falls back to the SYNC command)
- `ads128x_offset_cal()` / `ads128x_gain_cal()`: offset / gain calibration commands
- `ads128x_read_reg()` / `ads128x_write_reg()`: register access for custom calibration (OFC0-2/FSC0-2) and diagnostics
- `ads128x_set_pwdn_pin()` / `ads128x_power_down()` / `ads128x_power_up()`: optional hardware power-down pin control (falls back to STANDBY/WAKEUP commands when no pin is set)
- `ads128x_check_id()`: read the device ID register (0x00) as a sanity check and print it
- `ads128x_adc_register()`: register the "adc_ads128x" RT-Thread ADC device (compiled when `ADS128X_USING_ADC_DEVICE`)
- `ads128x_acqdev_register()`: register an instance as the standard acquisition device "acqN" (compiled when `ADS128X_USING_ACQDEV`)
- Shared class (from `acq_device.h`): `rt_acq_device_register()` / `rt_acq_find()` / `rt_acq_control()` and the `RT_ACQ_CTRL_*` commands

## 6. Notes

- The data frame is fixed at 4 bytes: high-resolution mode (MODE=1, default) outputs 32-bit two's complement; low-power mode outputs an 8-bit status byte + 24-bit data. The driver extracts data automatically based on the MODE bit of `config0` and performs sign extension.
- `ads128x_check_id()` reads the ID register (0x00): TI does not publish per-model ID values, so only an existence check is possible (the low nibble is always 0). It cannot distinguish chip models; the model is still selected at compile time by Kconfig.
- When no DRDY pin is configured, reads do not wait and return the current data directly.
- The register map and data frame format of the ADS1281 have not been directly verified; confirm against SBAS449 before use.
- Memory usage is minimal: the driver holds one static instance (no dynamic allocation on the data path).

## 7. Contact & Thanks

- Maintainer: [dean-lan](https://github.com/dean-lan)
