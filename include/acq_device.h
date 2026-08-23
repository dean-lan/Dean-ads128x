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
 * @file acq_device.h
 * @brief Public acquisition-device class for RT-Thread (shared by every chip)
 *
 * A common device class for sampling chips (ADS128x today, ADXL/IMUs and other
 * ADCs on SPI or I2C later). Every chip embeds a struct rt_acq_device, provides
 * a struct rt_acq_ops and registers through rt_acq_device_register(); the
 * shared implementation in acq_device.c handles the standard rt_device layer
 * (register, read, control/command dispatch, ownership) so chip drivers only
 * implement their specific operations.
 *
 * Capability flags tell aggregators which operations a chip really supports
 * (e.g. hardware SYNC/RESET vs. software emulation inside the ops).
 */

#ifndef __ACQ_DEVICE_H__
#define __ACQ_DEVICE_H__

#include <rtthread.h>
#include <rtdevice.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== Command numbers =====================
 * Starts in the user-reserved range above the standard rt_device commands. */
#ifndef RT_ACQ_CTRL_BASE
#define RT_ACQ_CTRL_BASE      (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x80)
#endif

enum rt_acq_ctrl
{
    RT_ACQ_CTRL_SET_RATE   = RT_ACQ_CTRL_BASE,  /* arg: rt_uint32_t*  sample rate (Hz) */
    RT_ACQ_CTRL_SET_CHANNEL,                    /* arg: rt_int8_t*    input channel / mux */
    RT_ACQ_CTRL_SET_GAIN,                       /* arg: rt_uint8_t*   PGA gain */
    RT_ACQ_CTRL_SET_FILTER,                     /* arg: rt_int32_t*   filter mode (chip-specific) */
    RT_ACQ_CTRL_START,                          /* arg: RT_NULL       start continuous acquisition */
    RT_ACQ_CTRL_STOP,                           /* arg: RT_NULL       stop continuous acquisition */
    RT_ACQ_CTRL_SYNC,                           /* arg: RT_NULL       align conversions (SYNC) */
    RT_ACQ_CTRL_RESET,                          /* arg: RT_NULL       reset the device */
    RT_ACQ_CTRL_ATTACH,                         /* arg: void* owner   hand over control to owner */
    RT_ACQ_CTRL_DETACH,                         /* arg: RT_NULL       return control to caller */
    RT_ACQ_CTRL_GET_INFO,                       /* arg: struct rt_acq_info* */
};

/* ===================== Common structures ===================== */
/* Generic capability/information descriptor. */
struct rt_acq_info
{
    const char *model;          /* e.g. "ads1282" */
    rt_uint8_t  channels;       /* output channels per frame */
    rt_uint8_t  resolution;     /* resolution in bits (e.g. 31) */
    rt_uint32_t min_rate_hz;
    rt_uint32_t max_rate_hz;
    rt_uint32_t flags;          /* RT_ACQ_FLAG_* capability bits */
};

/* One conversion frame, returned by the read operation:
 *   offset 0: rt_uint64_t timestamp   (conversion time)
 *   offset 8: rt_int32_t  ch[0..channels-1] */
struct rt_acq_frame
{
    rt_uint64_t timestamp;
    rt_int32_t  ch[1];          /* flexible; actual length = channels (GET_INFO) */
};

/* ===================== Capability flags ===================== */
#define RT_ACQ_FLAG_STREAM      (1 << 0)    /* continuous acquisition (START/STOP) */
#define RT_ACQ_FLAG_SYNC        (1 << 1)    /* conversion synchronization (generic) */
#define RT_ACQ_FLAG_SYNC_HW     (1 << 2)    /* SYNC via a hardware pin/line */
#define RT_ACQ_FLAG_SYNC_CMD    (1 << 3)    /* SYNC via a software/command */
#define RT_ACQ_FLAG_RESET       (1 << 4)    /* hardware reset (or command) */
#define RT_ACQ_FLAG_POWER       (1 << 5)    /* power management */
#define RT_ACQ_FLAG_GAIN        (1 << 6)    /* programmable gain */
#define RT_ACQ_FLAG_FILTER      (1 << 7)    /* programmable filter */

/* ===================== Chip operations (each chip implements these) =====================
 * `dev` is the struct rt_acq_device; use container_of/user_data to reach the
 * chip private data. Any operation not supported must either emulate it in
 * software or leave the pointer NULL (the framework returns -RT_ENOSYS). */
struct rt_acq_device;

struct rt_acq_ops
{
    rt_err_t   (*set_rate)(struct rt_acq_device *dev, rt_uint32_t hz);
    rt_err_t   (*set_channel)(struct rt_acq_device *dev, rt_int8_t ch);
    rt_err_t   (*set_gain)(struct rt_acq_device *dev, rt_uint8_t gain);
    rt_err_t   (*set_filter)(struct rt_acq_device *dev, rt_int32_t mode);
    rt_err_t   (*start)(struct rt_acq_device *dev);
    rt_err_t   (*stop)(struct rt_acq_device *dev);
    rt_err_t   (*sync)(struct rt_acq_device *dev);
    rt_err_t   (*reset)(struct rt_acq_device *dev);
    rt_ssize_t (*read_frame)(struct rt_acq_device *dev, struct rt_acq_frame *frame,
                             rt_size_t size);
    rt_err_t   (*get_info)(struct rt_acq_device *dev, struct rt_acq_info *info);
    /* optional ownership hooks (mirror chip-level attach/detach) */
    rt_err_t   (*attach)(struct rt_acq_device *dev, void *owner);
    rt_err_t   (*detach)(struct rt_acq_device *dev);
};

/* ===================== Public device class =====================
 * Embedded at the head of every chip device (or referenced via user_data). */
struct rt_acq_device
{
    struct rt_device parent;            /* standard rt_device, findable via rt_device_find */
    const struct rt_acq_ops *ops;       /* chip-specific operations */
    void *user_data;                    /* chip private data */
    void *owner;                        /* aggregator owning the device (NULL = free) */
};

/* ===================== Shared framework API (acq_device.c) =====================
 * Device names are allocated centrally and uniquely across every chip
 * ("acq0", "acq1", ...); `user_data` is passed back to the ops. */
rt_err_t rt_acq_device_register(struct rt_acq_device *dev,
                                const struct rt_acq_ops *ops, void *user_data);
rt_err_t rt_acq_device_unregister(struct rt_acq_device *dev);

/* Find a registered acquisition device by its standard device name. */
struct rt_acq_device *rt_acq_find(const char *name);

/* Convenience: broadcast a command to a device (checks ownership for controls). */
rt_err_t rt_acq_control(struct rt_acq_device *dev, int cmd, void *arg);

/* ===================== Ownership sequence =====================
 * Aggregators (e.g. DeanAcq) drive a device in this order:
 *   1. configure while free: SET_RATE / SET_GAIN / ... 
 *   2. START (continuous acquisition), then SYNC (alignment)
 *   3. ATTACH(owner): hands the device over; every control command from then
 *      on is rejected with -RT_EBUSY (reads stay allowed)
 *   4. DETACH, then STOP: returns control to the caller.
 * ATTACH before START/SYNC would block them, so keep the order above. */

#ifdef __cplusplus
}
#endif

#endif /* __ACQ_DEVICE_H__ */
