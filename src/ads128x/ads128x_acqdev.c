/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     dean-lan     first version
 */

/* ADS128x implementation of the shared acquisition-device class. Registers each
 * instance as the standard device "acq<idx>" through rt_acq_device_register();
 * the framework (acq_device.c) provides the rt_device layer and command
 * dispatch, this file only fills in the chip-specific struct rt_acq_ops. */

#include <rtdbg.h>
#include "acq_device.h"
#include "ads128x_internal.h"

#if defined(ADS128X_USING_ACQDEV)

/* ===================== Chip operations ===================== */
static struct ads128x_device *acq_to_ads(struct rt_acq_device *acq)
{
    return (struct ads128x_device *)acq->user_data;
}

static rt_err_t ads128x_acqdev_set_rate(struct rt_acq_device *acq, rt_uint32_t hz)
{
    return ads128x_set_data_rate(acq_to_ads(acq), hz);
}

static rt_err_t ads128x_acqdev_set_channel(struct rt_acq_device *acq, rt_int8_t ch)
{
    return ads128x_set_mux(acq_to_ads(acq), (rt_uint8_t)ch);
}

static rt_err_t ads128x_acqdev_set_gain(struct rt_acq_device *acq, rt_uint8_t gain)
{
    return ads128x_set_gain(acq_to_ads(acq), gain);
}

static rt_err_t ads128x_acqdev_set_filter(struct rt_acq_device *acq, rt_int32_t mode)
{
    return ads128x_set_filter(acq_to_ads(acq), (enum ads128x_filter)mode);
}

static rt_err_t ads128x_acqdev_start(struct rt_acq_device *acq)
{
    return ads128x_start_continuous(acq_to_ads(acq));
}

static rt_err_t ads128x_acqdev_stop(struct rt_acq_device *acq)
{
    return ads128x_stop_continuous(acq_to_ads(acq));
}

static rt_err_t ads128x_acqdev_sync(struct rt_acq_device *acq)
{
    return ads128x_sync(acq_to_ads(acq));
}

static rt_err_t ads128x_acqdev_reset(struct rt_acq_device *acq)
{
    return ads128x_reset(acq_to_ads(acq));
}

static rt_ssize_t ads128x_acqdev_read_frame(struct rt_acq_device *acq,
                                            struct rt_acq_frame *frame, rt_size_t size)
{
    rt_size_t need = sizeof(frame->timestamp) + sizeof(rt_int32_t);

    if (frame == RT_NULL || size < need)
    {
        return 0;
    }
    frame->timestamp = rt_tick_get();
    frame->ch[0] = ads128x_read_data(acq_to_ads(acq));
    return (rt_ssize_t)need;
}

static rt_err_t ads128x_acqdev_get_info(struct rt_acq_device *acq, struct rt_acq_info *info)
{
    struct ads128x_device *ads = acq_to_ads(acq);

    if (info == RT_NULL)
    {
        return -RT_EINVAL;
    }
    info->model = ads->chip->name;
    info->channels = 1;
    info->resolution = ads->chip->resolution;
    info->min_rate_hz = 250;
    info->max_rate_hz = 4000;
    info->flags = RT_ACQ_FLAG_STREAM | RT_ACQ_FLAG_SYNC_CMD | RT_ACQ_FLAG_RESET |
                  RT_ACQ_FLAG_POWER | RT_ACQ_FLAG_GAIN | RT_ACQ_FLAG_FILTER;
    if (ads->sync_pin >= 0)
    {
        info->flags |= RT_ACQ_FLAG_SYNC_HW;
    }
    return RT_EOK;
}

static rt_err_t ads128x_acqdev_attach(struct rt_acq_device *acq, void *owner)
{
    return ads128x_attach(acq_to_ads(acq), owner);
}

static rt_err_t ads128x_acqdev_detach(struct rt_acq_device *acq)
{
    struct ads128x_device *ads = acq_to_ads(acq);

    return ads128x_detach(ads, ads->owner);
}

static const struct rt_acq_ops ads128x_acq_ops =
{
    .set_rate    = ads128x_acqdev_set_rate,
    .set_channel = ads128x_acqdev_set_channel,
    .set_gain    = ads128x_acqdev_set_gain,
    .set_filter  = ads128x_acqdev_set_filter,
    .start       = ads128x_acqdev_start,
    .stop        = ads128x_acqdev_stop,
    .sync        = ads128x_acqdev_sync,
    .reset       = ads128x_acqdev_reset,
    .read_frame  = ads128x_acqdev_read_frame,
    .get_info    = ads128x_acqdev_get_info,
    .attach      = ads128x_acqdev_attach,
    .detach      = ads128x_acqdev_detach,
};

/* ===================== Registration ===================== */
rt_err_t ads128x_acqdev_register(rt_uint8_t idx)
{
    struct ads128x_device *ads;
    rt_err_t ret;

    if (idx >= ADS128X_MAX_DEVICES)
    {
        return -RT_EINVAL;
    }
    ads = &ads128x_dev[idx];
    if (ads->spi_dev.bus == RT_NULL)
    {
        LOG_E("ads128x: instance %u not initialized (call ads128x_init_ex first)", idx);
        return -RT_ERROR;
    }

    /* The shared class allocates a globally unique name ("acqN"). */
    ret = rt_acq_device_register(&ads->acq, &ads128x_acq_ops, ads);
    if (ret == RT_EOK)
    {
        LOG_I("ads128x: registered as %s (%s)", ads->acq.parent.name, ads->chip->name);
    }
    return ret;
}

#endif /* defined(ADS128X_USING_ACQDEV) */
