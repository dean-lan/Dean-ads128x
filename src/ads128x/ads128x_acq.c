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
#include "acq_device.h"

#if defined(ADS128X_USING_ACQ) && defined(PKG_USING_DDAQ)

#include <ddaq.h>

#ifndef ADS128X_ACQ_PRIORITY
#define ADS128X_ACQ_PRIORITY        10
#endif
#ifndef ADS128X_ACQ_RING_DEPTH
#define ADS128X_ACQ_RING_DEPTH      32      /* samples buffered per device in ISR */
#endif
#ifndef ADS128X_ACQ_STACK_SIZE
#define ADS128X_ACQ_STACK_SIZE      2048
#endif
#ifndef ADS128X_ACQ_WAIT_TIMEOUT
#define ADS128X_ACQ_WAIT_TIMEOUT    1000    /* ms, tolerated DRDY gap between chips */
#endif
#ifndef ADS128X_ACQ_MAX_BATCH
#define ADS128X_ACQ_MAX_BATCH       16      /* frames per ddaq_publish() call */
#endif

/* Per-device ISR software ring: the DRDY ISR pushes one sample, the acquisition
 * thread pops complete frames. Guarded by a short spinlock section so the ring
 * is safe across ISR and thread contexts (and across cores under SMP). */
struct ads128x_acq_ring
{
    rt_int32_t buf[ADS128X_ACQ_RING_DEPTH];
    rt_uint16_t rp;                 /* read position (consumer only) */
    rt_uint16_t wp;                 /* write position (producer only) */
    rt_uint16_t count;
};

static struct ads128x_acq_ring acq_ring[ADS128X_MAX_DEVICES];
static struct rt_semaphore acq_sem;
static struct rt_thread *acq_thread = RT_NULL;
static rt_uint16_t acq_topic_id;
static rt_uint16_t acq_batch = 1;
static rt_uint16_t acq_dev_mask;    /* bitmask of initialized devices */
static volatile rt_bool_t acq_stop;

RT_DEFINE_SPINLOCK(acq_ring_lock);

static void acq_ring_push(rt_uint8_t idx, rt_int32_t value)
{
    struct ads128x_acq_ring *r = &acq_ring[idx];
    rt_base_t level = rt_spin_lock_irqsave(&acq_ring_lock);

    if (r->count < ADS128X_ACQ_RING_DEPTH)
    {
        r->buf[r->wp] = value;
        r->wp = (rt_uint16_t)((r->wp + 1) % ADS128X_ACQ_RING_DEPTH);
        r->count++;
    }
    rt_spin_unlock_irqrestore(&acq_ring_lock, level);
}

static rt_int32_t acq_ring_pop(rt_uint8_t idx)
{
    struct ads128x_acq_ring *r = &acq_ring[idx];
    rt_base_t level = rt_spin_lock_irqsave(&acq_ring_lock);
    rt_int32_t value = 0;

    if (r->count > 0)
    {
        value = r->buf[r->rp];
        r->rp = (rt_uint16_t)((r->rp + 1) % ADS128X_ACQ_RING_DEPTH);
        r->count--;
    }
    rt_spin_unlock_irqrestore(&acq_ring_lock, level);
    return value;
}

static rt_uint16_t acq_ring_avail(rt_uint8_t idx)
{
    struct ads128x_acq_ring *r = &acq_ring[idx];
    rt_base_t level = rt_spin_lock_irqsave(&acq_ring_lock);
    rt_uint16_t c = r->count;

    rt_spin_unlock_irqrestore(&acq_ring_lock, level);
    return c;
}

/* DRDY ISR entry: read the fresh conversion into this device's ring and wake the
 * acquisition thread. Keep the ISR short: one 4-byte SPI read plus a push. */
void ads128x_acq_isr(ads128x_device_t dev, rt_uint8_t idx)
{
    rt_int32_t value;

    if (idx >= ADS128X_MAX_DEVICES || dev == RT_NULL)
    {
        return;
    }
    value = ads128x_read_data(dev);
    acq_ring_push(idx, value);
    rt_sem_release(&acq_sem);
}

/* Aggregated DRDY ISR: for setups where all DRDY lines are gated onto a single
 * interrupt (wired-OR) with SYNC-aligned conversions. Reads every initialized
 * device into its ring and wakes the worker exactly once. */
void ads128x_acq_isr_all(void)
{
    rt_uint8_t i;

    if (acq_thread == RT_NULL)
    {
        return;
    }
    for (i = 0; i < ADS128X_MAX_DEVICES; i++)
    {
        if (acq_dev_mask & (1u << i))
        {
            rt_int32_t value = ads128x_read_data(&ads128x_dev[i]);
            acq_ring_push(i, value);
        }
    }
    rt_sem_release(&acq_sem);
}

/* ===================== Group control =====================
 * Implements the unified RT_ACQ_CTRL_* commands for the whole group, choosing
 * shared vs. independent strategy per wiring:
 *   - SYNC: one pulse on a shared SYNC pin, otherwise a per-device SYNC command
 *   - RESET: a shared RESET line resets every chip at once (per-device pin config)
 *   - ATTACH/DETACH: hand the whole group to an owner (DeanAcq)
 * Used directly or as a dacq_source.control callback. */
rt_err_t ads128x_acq_ctrl(int cmd, void *data)
{
    rt_uint8_t i;

    switch (cmd)
    {
    case RT_ACQ_CTRL_START:
        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if (acq_dev_mask & (1u << i))
            {
                ads128x_start_continuous(&ads128x_dev[i]);
            }
        }
        return RT_EOK;

    case RT_ACQ_CTRL_STOP:
        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if (acq_dev_mask & (1u << i))
            {
                ads128x_stop_continuous(&ads128x_dev[i]);
            }
        }
        return RT_EOK;

    case RT_ACQ_CTRL_SYNC:
    {
        /* Shared SYNC line: one pulse on the first initialized chip that owns a
         * SYNC pin aligns every chip; otherwise issue per-device SYNC commands. */
        rt_uint8_t first = 0xFF;

        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if ((acq_dev_mask & (1u << i)) && ads128x_dev[i].sync_pin >= 0)
            {
                first = i;
                break;
            }
        }
        if (first != 0xFF)
        {
            return ads128x_sync_hw(&ads128x_dev[first]);
        }
        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if (acq_dev_mask & (1u << i))
            {
                ads128x_sync(&ads128x_dev[i]);
            }
        }
        return RT_EOK;
    }

    case RT_ACQ_CTRL_RESET:
        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if (acq_dev_mask & (1u << i))
            {
                ads128x_reset(&ads128x_dev[i]);     /* shared RESET line resets all at once */
            }
        }
        return RT_EOK;

    case RT_ACQ_CTRL_ATTACH:
        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if (acq_dev_mask & (1u << i))
            {
                ads128x_attach(&ads128x_dev[i], data);
            }
        }
        return RT_EOK;

    case RT_ACQ_CTRL_DETACH:
        for (i = 0; i < ADS128X_MAX_DEVICES; i++)
        {
            if (acq_dev_mask & (1u << i))
            {
                ads128x_detach(&ads128x_dev[i], ads128x_dev[i].owner);
            }
        }
        return RT_EOK;

    default:
        return -RT_ENOSYS;
    }
}

/* Acquisition thread: waits for data-ready, assembles one frame per conversion
 * (one sample from every initialized device, aligned by SYNC) and publishes
 * `batch` frames in a single ddaq_publish() call. */
static void ads128x_acq_entry(void *param)
{
    struct ads128x_acq_frame *frames;
    rt_size_t frame_size = sizeof(struct ads128x_acq_frame);
    rt_uint16_t fi = 0;
    rt_uint8_t i;

    RT_UNUSED(param);

    frames = (struct ads128x_acq_frame *)rt_malloc((rt_size_t)acq_batch * frame_size);
    if (frames == RT_NULL)
    {
        LOG_E("ads128x: acq: no memory for %d frames", acq_batch);
        acq_thread = RT_NULL;
        return;
    }

    LOG_I("ads128x: acq started (dev_mask=0x%02X topic=%u batch=%u)",
          acq_dev_mask, acq_topic_id, acq_batch);

    while (!acq_stop)
    {
        rt_sem_take(&acq_sem, rt_tick_from_millisecond(ADS128X_ACQ_WAIT_TIMEOUT));

        /* Assemble as many complete frames as all initialized devices allow. */
        for (;;)
        {
            rt_bool_t all = RT_TRUE;

            for (i = 0; i < ADS128X_MAX_DEVICES; i++)
            {
                if ((acq_dev_mask & (1u << i)) && acq_ring_avail(i) == 0)
                {
                    all = RT_FALSE;
                    break;
                }
            }
            if (!all)
            {
                break;
            }

            frames[fi].timestamp = rt_tick_get();
            for (i = 0; i < ADS128X_MAX_DEVICES; i++)
            {
                frames[fi].ch[i] = (acq_dev_mask & (1u << i)) ? acq_ring_pop(i) : 0;
            }
            fi++;
            if (fi >= acq_batch)
            {
                ddaq_publish(acq_topic_id, frames, (rt_size_t)fi * frame_size);
                fi = 0;
            }
        }
    }

    /* Publish any leftover partial batch, then quit. */
    if (fi > 0)
    {
        ddaq_publish(acq_topic_id, frames, (rt_size_t)fi * frame_size);
    }

    rt_free(frames);
    acq_thread = RT_NULL;
    LOG_I("ads128x: acq stopped");
}

rt_err_t ads128x_acq_start(rt_uint16_t topic_id, rt_uint16_t batch)
{
    rt_uint8_t i;
    rt_uint16_t mask = 0;
    rt_err_t ret;

    if (acq_thread != RT_NULL)
    {
        return -RT_EBUSY;
    }
    if (batch == 0 || batch > ADS128X_ACQ_MAX_BATCH)
    {
        return -RT_EINVAL;
    }

    /* Put every initialized device into continuous mode and align conversions
     * with the SYNC command so that all DRDYs assert almost simultaneously. */
    for (i = 0; i < ADS128X_MAX_DEVICES; i++)
    {
        if (ads128x_dev[i].spi_dev.bus == RT_NULL)
        {
            continue;
        }
        mask |= (1u << i);
        ret = ads128x_start_continuous(&ads128x_dev[i]);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }
    if (mask == 0)
    {
        LOG_E("ads128x: acq: no device initialized");
        return -RT_EINVAL;
    }
    for (i = 0; i < ADS128X_MAX_DEVICES; i++)
    {
        if (mask & (1u << i))
        {
            ads128x_sync(&ads128x_dev[i]);
        }
    }

    acq_dev_mask = mask;
    acq_topic_id = topic_id;
    acq_batch = batch;
    acq_stop = RT_FALSE;
    rt_sem_init(&acq_sem, "adacq", 0, RT_IPC_FLAG_FIFO);

    acq_thread = rt_thread_create("adacq", ads128x_acq_entry, RT_NULL,
                                  ADS128X_ACQ_STACK_SIZE,
                                  ADS128X_ACQ_PRIORITY, 20);
    if (acq_thread == RT_NULL)
    {
        LOG_E("ads128x: acq: create thread failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(acq_thread);
    return RT_EOK;
}

rt_err_t ads128x_acq_stop(void)
{
    if (acq_thread == RT_NULL)
    {
        return -RT_ERROR;
    }
    acq_stop = RT_TRUE;
    return RT_EOK;
}

rt_bool_t ads128x_acq_is_running(void)
{
    return acq_thread != RT_NULL;
}

#endif /* defined(ADS128X_USING_ACQ) && defined(PKG_USING_DDAQ) */
