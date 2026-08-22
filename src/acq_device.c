/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     dean-lan     first version
 */

/* Shared implementation of the acquisition-device class. All chips reuse this
 * code: it owns the standard rt_device layer (register, read, control command
 * dispatch and ownership) and forwards to the chip-specific struct rt_acq_ops. */

#include <rtdbg.h>
#include "acq_device.h"

/* ===================== Ownership =====================
 * While owned, control commands (rate/channel/gain/filter/start/stop/sync/reset)
 * from any caller are rejected with -RT_EBUSY; reads stay allowed. */
static rt_bool_t acq_is_owned(struct rt_acq_device *acq)
{
    return acq->owner != RT_NULL;
}

/* ===================== Standard rt_device read ===================== */
static rt_ssize_t acq_rtdev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct rt_acq_device *acq = (struct rt_acq_device *)dev;

    RT_UNUSED(pos);
    if (acq->ops->read_frame == RT_NULL)
    {
        return 0;
    }
    return acq->ops->read_frame(acq, (struct rt_acq_frame *)buffer, size);
}

/* ===================== Standard rt_device control =====================
 * Dispatches RT_ACQ_CTRL_* to the chip ops with ownership checks. */
static rt_err_t acq_rtdev_control(rt_device_t dev, int cmd, void *arg)
{
    struct rt_acq_device *acq = (struct rt_acq_device *)dev;
    const struct rt_acq_ops *ops = acq->ops;
    rt_err_t ret;

    switch (cmd)
    {
    case RT_ACQ_CTRL_SET_RATE:
        if (acq_is_owned(acq) || ops->set_rate == RT_NULL) return -RT_EBUSY;
        return ops->set_rate(acq, *(rt_uint32_t *)arg);

    case RT_ACQ_CTRL_SET_CHANNEL:
        if (acq_is_owned(acq) || ops->set_channel == RT_NULL) return -RT_EBUSY;
        return ops->set_channel(acq, *(rt_int8_t *)arg);

    case RT_ACQ_CTRL_SET_GAIN:
        if (acq_is_owned(acq) || ops->set_gain == RT_NULL) return -RT_EBUSY;
        return ops->set_gain(acq, *(rt_uint8_t *)arg);

    case RT_ACQ_CTRL_SET_FILTER:
        if (acq_is_owned(acq) || ops->set_filter == RT_NULL) return -RT_EBUSY;
        return ops->set_filter(acq, *(rt_int32_t *)arg);

    case RT_ACQ_CTRL_START:
        if (acq_is_owned(acq) || ops->start == RT_NULL) return -RT_EBUSY;
        return ops->start(acq);

    case RT_ACQ_CTRL_STOP:
        if (acq_is_owned(acq) || ops->stop == RT_NULL) return -RT_EBUSY;
        return ops->stop(acq);

    case RT_ACQ_CTRL_SYNC:
        if (acq_is_owned(acq) || ops->sync == RT_NULL) return -RT_EBUSY;
        return ops->sync(acq);

    case RT_ACQ_CTRL_RESET:
        if (acq_is_owned(acq) || ops->reset == RT_NULL) return -RT_EBUSY;
        return ops->reset(acq);

    case RT_ACQ_CTRL_ATTACH:
        if (acq->owner != RT_NULL)
        {
            return -RT_EBUSY;           /* already owned */
        }
        acq->owner = arg;
        if (ops->attach != RT_NULL)
        {
            ret = ops->attach(acq, arg);
            if (ret != RT_EOK)
            {
                acq->owner = RT_NULL;
            }
            return ret;
        }
        return RT_EOK;

    case RT_ACQ_CTRL_DETACH:
        if (acq->owner == RT_NULL)
        {
            return -RT_ERROR;           /* not owned */
        }
        if (ops->detach != RT_NULL)
        {
            ret = ops->detach(acq);
            if (ret != RT_EOK)
            {
                return ret;
            }
        }
        acq->owner = RT_NULL;
        return RT_EOK;

    case RT_ACQ_CTRL_GET_INFO:
        if (ops->get_info == RT_NULL)
        {
            return -RT_ENOSYS;
        }
        return ops->get_info(acq, (struct rt_acq_info *)arg);

    default:
        return -RT_ENOSYS;
    }
}

/* ===================== Registration / lookup ===================== */
rt_err_t rt_acq_device_register(struct rt_acq_device *dev, const char *name,
                                const struct rt_acq_ops *ops, void *user_data)
{
    static const struct rt_device_ops rtdev_ops =
    {
        .read    = acq_rtdev_read,
        .control = acq_rtdev_control,
    };

    if (dev == RT_NULL || name == RT_NULL || ops == RT_NULL)
    {
        return -RT_EINVAL;
    }

    dev->ops = ops;
    dev->user_data = user_data;
    dev->owner = RT_NULL;
    dev->parent.type = RT_Device_Class_Miscellaneous;
    dev->parent.ops = &rtdev_ops;
    return rt_device_register(&dev->parent, name, RT_DEVICE_FLAG_RDONLY);
}

rt_err_t rt_acq_device_unregister(struct rt_acq_device *dev)
{
    if (dev == RT_NULL)
    {
        return -RT_EINVAL;
    }
    return rt_device_unregister(&dev->parent);
}

struct rt_acq_device *rt_acq_find(const char *name)
{
    rt_device_t dev = rt_device_find(name);

    if (dev == RT_NULL)
    {
        return RT_NULL;
    }
    return (struct rt_acq_device *)dev;
}

/* Convenience command wrapper over the standard device control path. */
rt_err_t rt_acq_control(struct rt_acq_device *dev, int cmd, void *arg)
{
    if (dev == RT_NULL)
    {
        return -RT_EINVAL;
    }
    return acq_rtdev_control(&dev->parent, cmd, arg);
}
