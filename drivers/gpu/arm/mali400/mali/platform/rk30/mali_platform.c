/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions
 *      for a default platform
 */

/* #define ENABLE_DEBUG_LOG */
#include "custom_log.h"

#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/regulator/driver.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/of.h>
#include <linux/rockchip/cpu.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"


u32 mali_group_error;

/**
 * anchor_of_device_of_mali_gpu.
 */
static struct device *mali_dev;

/**
 * 设置 current_dvfs_level.
 *
 * @param level
 *	待设置为 current 的 dvfs_level 实例,
 *	在 dvfs_level_list 中的 index,
 *	即 index_of_new_current_level.
 *
 * @return
 *	0, 成功.
 *	其他 value, 失败.
 */
int mali_set_level(struct device *dev, int level)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	/* gpu_clk_freq_of_new_current_level. */
	unsigned long freq;
	int ret;
	/* index_of_old_current_level. */
	unsigned int current_level;

	_mali_osk_mutex_wait(drv_data->clock_set_lock);

	current_level = drv_data->dvfs.current_level;
	freq = drv_data->fv_info[level].freq;

	if (level == current_level) {
		D("we are already in the target level, to exit.");
		_mali_osk_mutex_signal(drv_data->clock_set_lock);
		return 0;
	}

	/* .KP : 调用 dvfs_module 的接口, 将 cpu_clk 设置为 'freq'. */
	ret = dvfs_clk_set_rate(drv_data->clk, freq);
	if (ret) {
		_mali_osk_mutex_signal(drv_data->clock_set_lock);
		return ret;
	}

	D("have set gpu_clk to %lu of new_level %d, " "the old_level is %d.",
	  freq,
	  level,
	  current_level);
	/* update index_of_current_dvfs_level. */
	drv_data->dvfs.current_level = level;

	_mali_osk_mutex_signal(drv_data->clock_set_lock);

	return 0;
}

/**
 * 初始化 gpu_dvfs_node 和 gpu_power_domain.
 */
static int mali_clock_init(struct device *dev)
{
	int ret;

	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	drv_data->pd = devm_clk_get(dev, "pd_gpu");
	if (IS_ERR(drv_data->pd)) {
		ret = PTR_ERR(drv_data->pd);
		/* rk3228 gpu has no power domain,save NULL for compatible*/
		if (ret != -ENOENT) {
			dev_err(dev, "get pd_clk failed, %d\n", ret);
			return ret;
		}
		drv_data->pd = NULL;
	}

	if (drv_data->pd) {
		ret = clk_prepare_enable(drv_data->pd);
		if (ret) {
			dev_err(dev, "prepare pd_clk failed, %d\n", ret);
			return ret;
		}
	}

	drv_data->clk = clk_get_dvfs_node("clk_gpu");
	if (IS_ERR(drv_data->clk)) {
		ret = PTR_ERR(drv_data->clk);
		dev_err(dev, "prepare clk gpu failed, %d\n", ret);
		return ret;
	}
	clk_enable_dvfs(drv_data->clk);

	ret = dvfs_clk_prepare_enable(drv_data->clk);
	if (ret) {
		dev_err(dev, "prepare clk failed, %d\n", ret);
		return ret;
	}

	drv_data->power_state = true;

	return 0;
}

static void mali_clock_term(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	dvfs_clk_disable_unprepare(drv_data->clk);
	if (drv_data->pd)
		clk_disable_unprepare(drv_data->pd);
	drv_data->power_state = false;
}

/*---------------------------------------------------------------------------*/

static ssize_t show_available_frequencies(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	ssize_t ret = 0;
	u32 i;

	for (i = 0; i < drv_data->fv_info_length; i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%lu\n",
				 drv_data->fv_info[i].freq);

	return ret;
}

static ssize_t show_clock(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	return scnprintf(buf,
			 PAGE_SIZE,
			 "%lu\n",
			 dvfs_clk_get_rate(drv_data->clk));
}

static ssize_t set_clock(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	unsigned long freq;
	ssize_t ret;
	u32 level;

	ret = kstrtoul(buf, 10, &freq);
	if (ret)
		return ret;

	for (level = drv_data->fv_info_length - 1; level > 0; level--) {
		unsigned long tmp  = drv_data->fv_info[level].freq;

		if (tmp <= freq)
			break;
	}

	dev_info(dev, "Using fv_info table %d: for %lu Hz\n", level, freq);

	ret = mali_set_level(dev, level);
	if (ret)
		return ret;

	return count;
}

static ssize_t show_dvfs_enable(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", mali_dvfs_is_enabled(dev));
}

static ssize_t set_dvfs_enable(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	unsigned long enable;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &enable);
	if (ret)
		return ret;

	if (enable == 1)
		mali_dvfs_enable(dev);
	else if (enable == 0)
		mali_dvfs_disable(dev);
	else
		return -EINVAL;

	return count;
}

static ssize_t show_utilisation(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", mali_dvfs_utilisation(dev));
}

static ssize_t error_count_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", mali_group_error);
}

static DEVICE_ATTR(available_frequencies,
		   0644,
		   show_available_frequencies,
		   NULL);
static DEVICE_ATTR(clock, 0644, show_clock, set_clock);
static DEVICE_ATTR(dvfs_enable,
		   0644,
		   show_dvfs_enable,
		   set_dvfs_enable);
static DEVICE_ATTR(utilisation, 0644, show_utilisation, NULL);
static DEVICE_ATTR(error_count, 0644, error_count_show, NULL);

static struct attribute *mali_sysfs_entries[] = {
	&dev_attr_available_frequencies.attr,
	&dev_attr_clock.attr,
	&dev_attr_dvfs_enable.attr,
	&dev_attr_utilisation.attr,
	&dev_attr_error_count.attr,
	NULL,
};

static const struct attribute_group mali_attr_group = {
	.attrs	= mali_sysfs_entries,
};

/**
 * 创建 sysfs_nodes_of_platform_dependent_part.
 */
static int mali_create_sysfs(struct device *dev)
{
	int ret;

	ret = sysfs_create_group(&dev->kobj, &mali_attr_group);
	if (ret)
		dev_err(dev, "create sysfs group error, %d\n", ret);

	return ret;
}

static void mali_remove_sysfs(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &mali_attr_group);
}

static int notify_reboot_event(struct notifier_block *this,
			       unsigned long event,
			       void *ptr)
{
	struct mali_platform_drv_data *drv_data
		= container_of(this,
			       struct mali_platform_drv_data,
			       reboot_event_notifier);

	mali_dvfs_disable(mali_dev);

	drv_data->is_in_reboot_session = true;

	return NOTIFY_OK;
}

/*---------------------------------------------------------------------------*/

/**
 * 对 platform_device_of_mali_gpu,
 * 完成仅和 platform_dependent_part 有关的初始化.
 */
_mali_osk_errcode_t mali_platform_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	/* mali_driver_private_data. */
	struct mali_platform_drv_data *mali_drv_data;
	int ret;

	mali_drv_data = devm_kzalloc(dev, sizeof(*mali_drv_data), GFP_KERNEL);
	if (!mali_drv_data) {
		dev_err(dev, "no mem\n");
		return _MALI_OSK_ERR_NOMEM;
	}

	dev_set_drvdata(dev, mali_drv_data);

	mali_drv_data->dev = dev;

	mali_dev = dev;

	ret = mali_clock_init(dev);
	if (ret)
		goto err_init;

	ret = mali_dvfs_init(dev);
	if (ret)
		goto err_init;

	ret = mali_create_sysfs(dev);
	if (ret)
		goto term_clk;

	mali_drv_data->clock_set_lock =
		_mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,
				     _MALI_OSK_LOCK_ORDER_UTILIZATION);

	mali_drv_data->reboot_event_notifier.notifier_call
		= notify_reboot_event;
	register_reboot_notifier(&mali_drv_data->reboot_event_notifier);

	mali_drv_data->is_in_reboot_session = false;

	return 0;
term_clk:
	mali_clock_term(dev);
err_init:
	return _MALI_OSK_ERR_FAULT;
}

_mali_osk_errcode_t mali_platform_deinit(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	drv_data->is_in_reboot_session = false;
	unregister_reboot_notifier(&drv_data->reboot_event_notifier);

	mali_remove_sysfs(dev);

	mali_clock_term(dev);
	_mali_osk_mutex_term(drv_data->clock_set_lock);

	return 0;
}

/*---------------------------------------------------------------------------*/

/**
 * 对  gpu_power_domain(mali_power_domain), "上电, 开 clk" / "下电, 关 clk".
 * @param bpower_off
 *      true, 下电.
 *      false, 对 gpu_power_domain 上电.
 */
static _mali_osk_errcode_t mali_power_domain_control(bool bpower_off)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(mali_dev);

	if (drv_data->is_in_reboot_session) {
		return 0;
	}

	/* 若要 上电, 则 ... */
	if (!bpower_off) {
		if (!drv_data->power_state) {
			D("to ENABLE clk to gpu_dvfs_node.");
			dvfs_clk_prepare_enable(drv_data->clk);

			if (drv_data->pd) {
				D("to power UP gpu_power_domain.");
				clk_prepare_enable(drv_data->pd);
			}

			drv_data->power_state = true;

			#define DELAY_IN_US (5000)
			if (cpu_is_rk322x())
				usleep_range(DELAY_IN_US, DELAY_IN_US + 100);
		}
	} else {
		if (drv_data->power_state) {
			D("to DISABLE clk to gpu_dvfs_node.");
			dvfs_clk_disable_unprepare(drv_data->clk);

			if (drv_data->pd) {
				D("to power DOWN gpu_power_domain.");
				clk_disable_unprepare(drv_data->pd);
			}

			drv_data->power_state = false;
		}
	}

	return 0;
}

_mali_osk_errcode_t mali_platform_power_mode_change(
			enum mali_power_mode power_mode)
{
	bool bpower_off;/* 下电. */

	switch (power_mode) {
	case MALI_POWER_MODE_ON:
		MALI_DEBUG_PRINT(2, ("MALI_POWER_MODE_ON\r\n"));
		mali_dvfs_enable(mali_dev);
		bpower_off = false;
		break;

	case MALI_POWER_MODE_LIGHT_SLEEP:
		MALI_DEBUG_PRINT(2, ("MALI_POWER_MODE_LIGHT_SLEEP\r\n"));
		mali_dvfs_disable(mali_dev);
		/* 预置将下电. */
		bpower_off = true;
		break;

	case MALI_POWER_MODE_DEEP_SLEEP:
		MALI_DEBUG_PRINT(2, ("MALI_POWER_MODE_DEEP_SLEEP\r\n"));
		mali_dvfs_disable(mali_dev);
		/* 预置将下电. */
		bpower_off = true;
		break;

	default:
		MALI_DEBUG_PRINT(2,
				 ("power_mode(%d) not support\n", power_mode));
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	mali_power_domain_control(bpower_off);

	return 0;
}

/*---------------------------------------------------------------------------*/

/**
 * 将注册到 common_part 中的, 对 mali_utilization_event 的 handler,
 * 即 common_part 会直接将 mali_utilization_event 通知回调到本函数.
 */
void mali_gpu_utilization_handler(struct mali_gpu_utilization_data *data)
{
	if (data->utilization_pp > 256)
		return;

	/* dev_dbg(mali_dev, "utilization:%d\r\n", data->utilization_pp); */

	mali_dvfs_event(mali_dev, data->utilization_pp);
}
