# DEANACQ-DEV

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-C-blue.svg)]()
[![Platform](https://img.shields.io/badge/platform-RT--Thread-orange.svg)](https://www.rt-thread.org/)
[![DeanDAQ](https://img.shields.io/badge/deandaq-adapter-green.svg)](https://github.com/dean-lan/DeanDAQ)

[English](README.md) | **中文**

## 1. 简介

**DeanAcq-dev**（由 `Dean-ads128x` 升级而来）是一个 RT-Thread 软件包，提供**共享的采集设备类**及其首个芯片实现。它包含：

- `acq_device.h` / `acq_device.c` —— 公共设备类（类似 `sensor` 框架）：嵌入到每个芯片驱动中的 `struct rt_acq_device`、每个芯片一个 `struct rt_acq_ops`，以及共享的注册/查找/命令分发代码，使每个芯片（如今是 ADS128x，未来是 SPI 或 I2C 的 ADXL/IMU 及其他 ADC）复用同一套 `rt_device` 层。
- `ads128x` —— 首个实现：裸 SPI 驱动 + 采集类绑定（`ads128x_acqdev_register()`）、RT-Thread ADC 设备封装、多芯片采集前端。

芯片以标准设备 `acq0`、`acq1`、... 注册，通过 `rt_device_find/open/control/read` 配合 `RT_ACQ_CTRL_*` 命令（采样率、通道、增益、滤波、启动、停止、同步、复位、所有权、信息）统一驱动。像 [DeanAcq](https://github.com/dean-lan/DeanAcq) 这样的聚合器只看到这一统一接口——从不特殊化具体芯片。

支持的芯片型号（单一代码库，编译期选择）：

| 型号 | 分辨率 | 说明 |
| ---- | ---- | ---- |
| ADS1281 | 31-bit | 单通道，无 PGA |
| ADS1282 | 31-bit | 双通道带 PGA（基线） |
| ADS1282-HT / ADS1282-SP | 31-bit | 高温/航天级 ADS1282，无需单独条目 |
| ADS1283 | 31-bit | 低功耗变体 |
| ADS1284 | 31-bit | 双功耗模式 |

家族共享同一 SPI 命令协议、数据帧格式与寄存器映射（依据 ADS1282 SBAS499 Rev I、ADS1282-HT、ADS1283、ADS1284 数据手册核实）。ADS1281 寄存器映射假定相同，使用前请对照 SBAS449 确认。

### 特性摘要

- **裸驱动 + 可选 RT-Thread ADC 设备**（`adc_ads128x`），见[为何两者都要？](#为何裸-api--adc-设备)
- **多片支持**：一条或多条 SPI 总线上最多 `ADS128X_MAX_DEVICES` 片，经 SYNC 对齐
- 完整寄存器/命令集：滤波、PGA 增益、MUX、HPF、相位、同步、偏移/增益校准
- 两种工作模式：高分辨率（32-bit）/ 低功耗（8-bit 状态 + 24-bit 数据），自动提取
- DRDY 中断支持，最高 4 kSPS 无损采集
- 可选 DeanDAQ 采集模块：逐片 ISR 环 + 批量帧发布
- 全家族单一代码库，芯片型号在 menuconfig 中选择

### License

- Apache-2.0，见 `LICENSE`。

## 2. 快速开始

```c
#include "ads128x.h"

/* 1. 初始化：SPI 总线 "spi1"，CS=PA4(4)，DRDY=PA0(0)，RESET=PA1(1) */
ads128x_init("spi1", 4, 0, 1);

/* 2. 可选：注册为 RT-Thread ADC 设备 "adc_ads128x" */
ads128x_adc_register();

/* 3. 使用裸 API */
ads128x_device_t dev = ads128x_find();
ads128x_set_data_rate(dev, 1000);       /* 250/500/1000/2000/4000 SPS */
ads128x_set_gain(dev, 1);               /* 1/2/4/8/16/32/64 */

ads128x_start_continuous(dev);          /* RDATAC 模式 */
int32_t sample = ads128x_read_data(dev);
ads128x_stop_continuous(dev);
```

接线、基于中断的高速率采集与 DeanDAQ 集成见[第 4 节](#4-用法)。

## 3. 如何获取

1. 在 menuconfig 中配置：

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

2. 保存并运行 `pkgs --update`。

3. 依赖：SPI 设备框架（`RT_USING_SPI`）、ADC 设备框架（`RT_USING_ADC`，启用 ADC 设备封装时）以及（采集模块需要）[DeanDAQ](https://github.com/dean-lan/DeanDAQ) 包（`PKG_USING_DDAQ`）。

   本仓库还承载共享的采集设备类（`acq_device.h/.c`），供 [DeanAcq](https://github.com/dean-lan/DeanAcq) 聚合器消费：使用 DeanAcq 时请启用 `PKG_USING_ADS128X`（本包），以便其 `acq_device.h` 头文件可用。

## 4. 用法

### 4.1 硬件连接

| ADS128x 引脚 | MCU 引脚 |
| ----------- | ------- |
| SCLK        | SPI SCLK |
| SDIN (DIN)  | SPI MOSI |
| SDOUT (DOUT)| SPI MISO |
| CS          | 任意 GPIO |
| DRDY        | 任意 GPIO（低电平表示数据就绪） |
| RESET       | 任意 GPIO（可选） |
| PWDN        | 任意 GPIO（可选，高有效） |

SPI 参数：模式 1（CPOL=0, CPHA=1）、MSB 在前、8-bit、最高 20 MHz（建议 10 MHz）。

### 4.2 初始化

```c
#include "ads128x.h"

/* SPI1 总线，CS=PA4(4)，DRDY=PA0(0)，RESET=PA1(1) */
ads128x_init("spi1", 4, 0, 1);

/* 可选：注册为 RT-Thread ADC 设备 "adc_ads128x" */
ads128x_adc_register();

/* 获取裸 API 的（不透明）驱动句柄 */
ads128x_device_t dev = ads128x_find();
```

### 4.3 标准采集设备（acqN）

启用 `ADS128X_USING_ACQDEV` 后，每个实例都通过共享采集设备类注册，并可用标准 rt_device 接口统一驱动（任何实现该类的芯片使用相同代码）：

```c
ads128x_acqdev_register(0);
ads128x_acqdev_register(1);

rt_device_t d = rt_device_find("acq0");
rt_device_open(d, RT_DEVICE_OFLAG_RDONLY);

rt_uint32_t hz = 1000;
rt_device_control(d, RT_ACQ_CTRL_SET_RATE, &hz);   /* 统一命令 */
rt_device_control(d, RT_ACQ_CTRL_START, RT_NULL);

struct rt_acq_info info;
rt_device_control(d, RT_ACQ_CTRL_GET_INFO, &info);  /* model / channels / flags */

struct rt_acq_frame frame;
rt_device_read(d, 0, &frame, sizeof(frame));        /* 一次转换 */

/* 所有权：将设备移交给聚合器（如 DeanAcq）。托管期间其他调用者的
 * 控制命令一律返回 -RT_EBUSY；读仍允许。 */
void *owner = ...;
rt_device_control(d, RT_ACQ_CTRL_ATTACH, owner);
...
rt_device_control(d, RT_ACQ_CTRL_DETACH, RT_NULL);
```

能力标志（`info.flags`，`RT_ACQ_FLAG_*`）告知聚合器芯片真正支持哪些操作（SYNC/RESET/POWER/GAIN/FILTER/STREAM）。缺少硬件特性的芯片在 ops 内软件模拟，或将该 op 置 NULL（框架返回 `-RT_ENOSYS`）。

### 4.4 读取数据（轮询）

```c
ads128x_set_data_rate(dev, 1000);       /* 250/500/1000/2000/4000 SPS */
ads128x_set_gain(dev, 4);               /* 1/2/4/8/16/32/64 */
ads128x_set_filter(dev, ADS128X_FILTER_SINC_LPF_HPF);
ads128x_set_mux(dev, 0);                /* 0: AINP1/AINN1, 1: AINP2/AINN2 */

ads128x_start_continuous(dev);
int32_t val = ads128x_read_data(dev);
ads128x_stop_continuous(dev);
```

轮询在 500 SPS 以下表现良好；更高数据率请用下面的中断方式。

### 4.5 高速率采集（DRDY 中断）

ADS128x **没有内部 FIFO**：单个 4 字节数据寄存器保存最新转换，若未及时读取，新转换会**覆盖**旧值。瓶颈不在 SPI 带宽（20 MHz 下读 4 字节约 1.6 us，而 4 kSPS 的采样周期为 250 us），而在于 MCU 响应 DRDY 的速度。使用 DRDY 下降沿中断并从应用线程读取：

```c
#include <rtdevice.h>

static void drdy_isr_entry(void *args)   /* DRDY 下降沿 ISR */
{
    ads128x_drdy_isr(ads128x_find());    /* 释放数据就绪信号量 */
}

/* 在初始化代码中： */
rt_pin_attach_irq(0, PIN_IRQ_MODE_FALLING, drdy_isr_entry, RT_NULL);
rt_pin_irq_enable(0, PIN_IRQ_ENABLE);

/* 采集线程： */
void acq_thread_entry(void *param)
{
    ads128x_device_t dev = ads128x_find();

    while (1)
    {
        ads128x_wait_data(dev, RT_WAITING_FOREVER);  /* DRDY 唤醒 */
        int32_t val = ads128x_read_data(dev);
        /* ... 使用 val ... */
    }
}
```

性能建议：

- 将 SPI 时钟提高到 10-20 MHz（`ADS128X_SPI_MAX_HZ`）。
- 在 BSP 层启用 SPI DMA，减轻 4 字节读取期间的 CPU 占用。
- `ads128x_start_continuous()`（RDATAC）保持 CS 拉低并省去逐样本 RDATA 命令，节省 SPI 开销。

### 4.6 DeanDAQ 集成

驱动与 [DeanDAQ](https://github.com/dean-lan/DeanDAQ) 发布/订阅总线配合：将 DRDY 驱动的采集线程样本送入 topic，任意数量的订阅者零拷贝 borrow 消费：

```c
#include "ads128x.h"
#include <ddaq.h>
#include <ddaq_topics.h>   /* 生成：struct ads128x_sample_s, DDAQ_ID(...) */

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

### 4.7 ADC 设备接口

启用 `ADS128X_USING_ADC_DEVICE` 并调用 `ads128x_adc_register()` 后，驱动可作为 RT-Thread ADC 设备（设备名 `adc_ads128x`）通过标准接口使用：

```c
rt_adc_device_t adc = (rt_adc_device_t)rt_device_find("adc_ads128x");
rt_adc_read(adc, 0);
```

#### 为何裸 API + ADC 设备？

- RT-Thread ADC 框架只建模单次通道读取（`enabled` / `convert` / `get_resolution` / `get_vref`），无法表达连续采集、DRDY 中断、增益/滤波/HPF/同步/校准。
- 因此裸 API 暴露完整能力，设备封装只是标准 `rt_adc_read()` 访问的便利。
- `ADS128X_USING_ADC_DEVICE` 默认开启；禁用可节省 RAM/ROM。

### 4.8 文件布局

```
DeanAcq-dev/
├── include/
│   ├── acq_device.h          # 公共采集设备类（所有芯片共享）
│   └── ads128x/
│       └── ads128x.h         # ADS128x 公共 API（裸驱动 + 封装）
├── src/
│   ├── acq_device.c          # 共享类：注册/查找/读/控制分发 + 所有权
│   └── ads128x/
│       ├── ads128x_internal.h  # 内部：设备结构、寄存器/命令宏、芯片表
│       ├── ads128x_core.c      # 裸驱动核心（SPI 访问、配置、控制、读数据）
│       ├── ads128x_acqdev.c    # 采集类绑定（实现 rt_acq_ops）
│       ├── ads128x_adc.c       # 可选 RT-Thread ADC 设备封装
│       └── ads128x_acq.c       # 可选多芯片前端（ISR 环 + 批量 + 组控）
├── Kconfig
├── SConscript
├── examples/ads128x_sample.c
└── README.md
```

### 4.9 多芯片与 DeanDAQ 采集

对多芯片场景（如多片 ADS1282 共享一条 SPI 总线，经 SYNC 对齐转换），初始化每个实例并启动采集模块：

```c
/* 实例 0..N-1，各自独立的 CS/DRDY/RESET 引脚（共享 SPI 总线可以） */
ads128x_init_ex(0, "spi1", CS0, DRDY0, RESET0);
ads128x_init_ex(1, "spi1", CS1, DRDY1, RESET1);

/* DeanDAQ 采集模块：每次转换一帧，每次发布 8 帧 */
ads128x_acq_start(DDAQ_ID(ads128x_acq), 8);
```

编译 `ADS128X_USING_ACQ`（需要 `PKG_USING_DDAQ`）后，模块将每个已初始化设备置于 RDATAC 模式，发出 SYNC 对齐转换，然后：

- **ISR 侧**（`ads128x_acq_isr()`，来自每个 DRDY 中断）：将新样本读入逐片软件环（深度 `ADS128X_ACQ_RING_DEPTH`）并唤醒 worker。ISR 保持简短：一次 4 字节 SPI 读 + 一次入环。
- **Worker 线程**：每次转换装配一帧（时间戳 + 每片一个样本），攒满 `batch` 帧后以一次 `ddaq_publish()` 调用发布（发布频率 = 数据率 / batch）。

发布负载是 `struct ads128x_acq_frame` 数组：

```c
struct ads128x_acq_frame
{
    rt_uint64_t timestamp;
    rt_int32_t  ch[ADS128X_MAX_DEVICES];
};
```

接好 DRDY ISR：

```c
static void drdy0_isr(void *arg) { ads128x_acq_isr(ads128x_get(0), 0); }
static void drdy1_isr(void *arg) { ads128x_acq_isr(ads128x_get(1), 1); }
```

**共享控制线**（多芯片硬件变体）：

- **共享 SYNC 引脚**：配置一个 GPIO，脉冲一次即可对齐所有共享该线的芯片（`ads128x_set_sync_pin()` / `ads128x_sync_hw()`）；无需逐片发送 SYNC 命令。
- **共享 DRDY（或门）**：使用单个聚合 ISR 一次读取所有设备并只唤醒一次：

```c
static void drdy_shared_isr(void *arg) { ads128x_acq_isr_all(); }
```

用 `ads128x_acq_stop()` 停止模块。

### 4.10 msh 示例

在 menuconfig 中启用 `ADS128X_SAMPLE` 并运行：

```
msh > ads128x_sample
```

示例用配置的引脚初始化驱动，打印 ID 寄存器并读取几次转换。*示例输出*：

```
[ads128x] ID register = 0x1A
[ads128x] sample[0] = 0000C2A1
[ads128x] sample[1] = FFFF3E50
```

## 5. API 参考

- `ads128x_init()`：初始化驱动并挂载 SPI 设备
- `ads128x_find()`：查找驱动句柄（返回实例 0）
- `ads128x_init_ex()` / `ads128x_get()`：多芯片实例初始化 / 访问器
- `ads128x_acq_start()` / `ads128x_acq_stop()` / `ads128x_acq_isr()`：DeanDAQ 采集模块（多芯片、批量发布）
- `ads128x_acq_isr_all()`：共享/或门 DRDY 线的聚合 ISR（一次读取所有设备、一次唤醒）
- `ads128x_set_data_rate()` / `ads128x_set_gain()` / `ads128x_set_mux()`：数据率 / PGA 增益 / 输入通道配置
- `ads128x_set_filter()` / `ads128x_set_phase()` / `ads128x_set_hpf()`：数字滤波器类型、相位响应、HPF 角频率
- `ads128x_set_mode()`：高分辨率 / 低功耗模式切换
- `ads128x_start_continuous()` / `ads128x_stop_continuous()`：读数据连续（RDATAC）模式控制
- `ads128x_read_data()`：读取一次转换结果
- `ads128x_wait_data()` / `ads128x_drdy_isr()`：等待数据就绪 / ISR 入口
- `ads128x_reset()` / `ads128x_standby()` / `ads128x_wakeup()`：操作控制
- `ads128x_sync()` / `ads128x_set_sync_mode()`：转换同步（多芯片对齐）
- `ads128x_set_sync_pin()` / `ads128x_sync_hw()`：硬件 SYNC 引脚（共享 SYNC 线，脉冲对齐所有芯片；未配引脚时回退 SYNC 命令）
- `ads128x_offset_cal()` / `ads128x_gain_cal()`：偏移 / 增益校准命令
- `ads128x_read_reg()` / `ads128x_write_reg()`：寄存器访问，用于自定义校准（OFC0-2/FSC0-2）与诊断
- `ads128x_set_pwdn_pin()` / `ads128x_power_down()` / `ads128x_power_up()`：可选硬件掉电引脚控制（未配引脚时回退 STANDBY/WAKEUP 命令）
- `ads128x_check_id()`：读取设备 ID 寄存器（0x00）作合理性检查并打印
- `ads128x_adc_register()`：注册 "adc_ads128x" RT-Thread ADC 设备（编译 `ADS128X_USING_ADC_DEVICE` 时）
- `ads128x_acqdev_register()`：将实例注册为标准采集设备 "acqN"（编译 `ADS128X_USING_ACQDEV` 时）
- 共享类（来自 `acq_device.h`）：`rt_acq_device_register()` / `rt_acq_find()` / `rt_acq_control()` 与 `RT_ACQ_CTRL_*` 命令

## 6. 注意事项

- 数据帧固定为 4 字节：高分辨率模式（MODE=1，默认）输出 32 位补码；低功耗模式输出 8 位状态字节 + 24 位数据。驱动根据 `config0` 的 MODE 位自动提取并进行符号扩展。
- `ads128x_check_id()` 读取 ID 寄存器（0x00）：TI 未公布各型号 ID 值，只能做存在性检查（低半字节恒为 0）。无法区分芯片型号；型号仍由 Kconfig 在编译期选择。
- 未配置 DRDY 引脚时，读取不等待、直接返回当前数据。
- ADS1281 的寄存器映射与数据帧格式未经直接验证；使用前请对照 SBAS449 确认。
- 内存占用极小：驱动持有静态实例（数据路径无动态分配）。

## 7. 联系与致谢

- 维护者：[dean-lan](https://github.com/dean-lan)
