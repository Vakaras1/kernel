/**
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming chm@rock-chips.com
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>

#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/pmu.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/psci.h>
#include <video/rk_vpu_service.h>

#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#endif

#if defined(CONFIG_ROCKCHIP_IOMMU) & defined(CONFIG_ION_ROCKCHIP)
#define CONFIG_VCODEC_MMU
#include <linux/rockchip-iovmm.h>
#include <linux/dma-buf.h>
#endif

#include "vcodec_hw_info.h"
#include "vcodec_hw_vpu.h"
#include "vcodec_hw_rkv.h"
#include "vcodec_hw_vpu2.h"

#include <linux/clk-private.h>

/*
 * debug flag usage:
 * +------+-------------------+
 * | 8bit |      24bit        |
 * +------+-------------------+
 *  0~23 bit is for different information type
 * 24~31 bit is for information print format
 */

#define DEBUG_POWER				0x00000001
#define DEBUG_CLOCK				0x00000002
#define DEBUG_IRQ_STATUS			0x00000004
#define DEBUG_IOMMU				0x00000008
#define DEBUG_IOCTL				0x00000010
#define DEBUG_FUNCTION				0x00000020
#define DEBUG_REGISTER				0x00000040
#define DEBUG_EXTRA_INFO			0x00000080
#define DEBUG_TIMING				0x00000100
#define DEBUG_TASK_INFO				0x00000200
#define DEBUG_DUMP_ERR_REG			0x00000400

#define DEBUG_SET_REG				0x00001000
#define DEBUG_GET_REG				0x00002000
#define DEBUG_PPS_FILL				0x00004000
#define DEBUG_IRQ_CHECK				0x00008000
#define DEBUG_CACHE_32B				0x00010000

#define PRINT_FUNCTION				0x80000000
#define PRINT_LINE				0x40000000

static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "bit switch for vcodec_service debug information");

#define VCODEC_CLOCK_ENABLE	1
#define IOMMU_PAGE_SIZE		4096

static struct vpu_service_info *vpu_dvfs;

/*
 * hardware information organization
 *
 * In order to support multiple hardware with different version the hardware
 * information is organized as follow:
 *
 * 1. First, index hardware by register size / position.
 *    These information is fix for each hardware and do not relate to runtime
 *    work flow. It only related to resource allocation.
 *    Descriptor: struct vpu_hw_info
 *
 * 2. Then, index hardware by runtime configuration
 *    These information is related to runtime setting behave including enable
 *    register, irq register and other key control flag
 *    Descriptor: struct vpu_task_info
 *
 * 3. Final, on iommu case the fd translation is required
 *    Descriptor: struct vpu_trans_info
 */

enum VPU_FREQ {
	VPU_FREQ_200M,
	VPU_FREQ_266M,
	VPU_FREQ_300M,
	VPU_FREQ_400M,
	VPU_FREQ_500M,
	VPU_FREQ_600M,
	VPU_FREQ_DEFAULT,
	VPU_FREQ_BUT,
};

enum VPU_CLIENT_TYPE {
	VPU_ENC                 = 0x0,
	VPU_DEC                 = 0x1,
	VPU_PP                  = 0x2,
	VPU_DEC_PP              = 0x3,
	VPU_TYPE_BUTT,
};

struct extra_info_elem {
	u32 index;
	u32 offset;
};

#define EXTRA_INFO_MAGIC	0x4C4A46

struct extra_info_for_iommu {
	u32 magic;
	u32 cnt;
	struct extra_info_elem elem[20];
};

struct vpu_subdev_data;
struct vpu_service_info;
struct vpu_reg;

struct vcodec_hw_ops {
	void (*power_on)(struct vpu_service_info *pservice);
	void (*power_off)(struct vpu_service_info *pservice);
	void (*get_freq)(struct vpu_subdev_data *data, struct vpu_reg *reg);
	void (*set_freq)(struct vpu_service_info *pservice,
			 struct vpu_reg *reg);
	void (*reduce_freq)(struct vpu_service_info *pservice);
};

struct vcodec_hw_var {
	enum pmu_idle_req pmu_type;
	struct vcodec_hw_ops *ops;
	int (*init)(struct vpu_service_info *pservice);
	void (*config)(struct vpu_subdev_data *data);
};

#define MHZ					(1000*1000)
#define SIZE_REG(reg)				((reg)*4)

static struct vcodec_info vcodec_info_set[] = {
	[0] = {
		.hw_id		= VPU_ID_8270,
		.hw_info	= &hw_vpu_8270,
		.task_info	= task_vpu,
		.trans_info	= trans_vpu,
	},
	[1] = {
		.hw_id		= VPU_ID_4831,
		.hw_info	= &hw_vpu_4831,
		.task_info	= task_vpu,
		.trans_info	= trans_vpu,
	},
	[2] = {
		.hw_id		= VPU_DEC_ID_9190,
		.hw_info	= &hw_vpu_9190,
		.task_info	= task_vpu,
		.trans_info	= trans_vpu,
	},
	[3] = {
		.hw_id		= HEVC_ID,
		.hw_info	= &hw_rkhevc,
		.task_info	= task_rkv,
		.trans_info	= trans_rkv,
	},
	[4] = {
		.hw_id		= RKV_DEC_ID,
		.hw_info	= &hw_rkvdec,
		.task_info	= task_rkv,
		.trans_info	= trans_rkv,
	},
	[5] = {
		.hw_id		= VPU2_ID,
		.hw_info	= &hw_vpu2,
		.task_info	= task_vpu2,
		.trans_info	= trans_vpu2,
	},
	[6] = {
		.hw_id		= RKV_DEC_ID2,
		.hw_info	= &hw_rkvdec,
		.task_info	= task_rkv,
		.trans_info	= trans_rkv,
	},
};

#define DEBUG
#ifdef DEBUG
#define vpu_debug_func(type, fmt, args...)			\
	do {							\
		if (unlikely(debug & type)) {			\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)
#define vpu_debug(type, fmt, args...)				\
	do {							\
		if (unlikely(debug & type)) {			\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)
#else
#define vpu_debug_func(level, fmt, args...)
#define vpu_debug(level, fmt, args...)
#endif

#define vpu_debug_enter() vpu_debug_func(DEBUG_FUNCTION, "enter\n")
#define vpu_debug_leave() vpu_debug_func(DEBUG_FUNCTION, "leave\n")

#define vpu_err(fmt, args...)				\
		pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

enum VPU_DEC_FMT {
	VPU_DEC_FMT_H264,
	VPU_DEC_FMT_MPEG4,
	VPU_DEC_FMT_H263,
	VPU_DEC_FMT_JPEG,
	VPU_DEC_FMT_VC1,
	VPU_DEC_FMT_MPEG2,
	VPU_DEC_FMT_MPEG1,
	VPU_DEC_FMT_VP6,
	VPU_DEC_FMT_RESERV0,
	VPU_DEC_FMT_VP7,
	VPU_DEC_FMT_VP8,
	VPU_DEC_FMT_AVS,
	VPU_DEC_FMT_RES
};

/**
 * struct for process session which connect to vpu
 *
 * @author ChenHengming (2011-5-3)
 */
struct vpu_session {
	enum VPU_CLIENT_TYPE type;
	/* a linked list of data so we can access them for debugging */
	struct list_head list_session;
	/* a linked list of register data waiting for process */
	struct list_head waiting;
	/* a linked list of register data in processing */
	struct list_head running;
	/* a linked list of register data processed */
	struct list_head done;
	wait_queue_head_t wait;
	pid_t pid;
	atomic_t task_running;
};

/**
 * struct for process register set
 *
 * @author ChenHengming (2011-5-4)
 */
struct vpu_reg {
	enum VPU_CLIENT_TYPE type;
	enum VPU_FREQ freq;
	struct vpu_session *session;
	struct vpu_subdev_data *data;
	struct vpu_task_info *task;
	const struct vpu_trans_info *trans;

	/* link to vpu service session */
	struct list_head session_link;
	/* link to register set list */
	struct list_head status_link;

	unsigned long size;
	struct list_head mem_region_list;
	u32 dec_base;
	u32 *reg;
};

struct vpu_device {
	atomic_t irq_count_codec;
	atomic_t irq_count_pp;
	unsigned int iosize;
	u32 *regs;
};

enum vcodec_device_id {
	VCODEC_DEVICE_ID_VPU,
	VCODEC_DEVICE_ID_HEVC,
	VCODEC_DEVICE_ID_COMBO,
	VCODEC_DEVICE_ID_RKVDEC,
	VCODEC_DEVICE_ID_BUTT
};

enum VCODEC_RUNNING_MODE {
	VCODEC_RUNNING_MODE_NONE = -1,
	VCODEC_RUNNING_MODE_VPU,
	VCODEC_RUNNING_MODE_HEVC,
	VCODEC_RUNNING_MODE_RKVDEC
};

struct vcodec_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	unsigned long iova;	/* virtual address for iommu */
	unsigned long len;
	u32 reg_idx;
	struct ion_handle *hdl;
};

enum vpu_ctx_state {
	MMU_ACTIVATED	= BIT(0),
	MMU_PAGEFAULT	= BIT(1)
};

struct vpu_subdev_data {
	struct cdev cdev;
	dev_t dev_t;
	struct class *cls;
	struct device *child_dev;

	int irq_enc;
	int irq_dec;
	struct vpu_service_info *pservice;

	u32 *regs;
	enum VCODEC_RUNNING_MODE mode;
	struct list_head lnk_service;

	struct device *dev;

	struct vpu_device enc_dev;
	struct vpu_device dec_dev;

	enum VPU_HW_ID hw_id;
	struct vpu_hw_info *hw_info;
	struct vpu_task_info *task_info;
	const struct vpu_trans_info *trans_info;

	u32 reg_size;
	unsigned long state;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dir;
	struct dentry *debugfs_file_regs;
#endif

	struct device *mmu_dev;
};

struct vpu_service_info {
	struct wake_lock wake_lock;
	struct delayed_work power_off_work;
	ktime_t last; /* record previous power-on time */
	/* vpu service structure global lock */
	struct mutex lock;
	/* link to link_reg in struct vpu_reg */
	struct list_head waiting;
	/* link to link_reg in struct vpu_reg */
	struct list_head running;
	/* link to link_reg in struct vpu_reg */
	struct list_head done;
	/* link to list_session in struct vpu_session */
	struct list_head session;
	atomic_t total_running;
	atomic_t enabled;
	atomic_t power_on_cnt;
	atomic_t power_off_cnt;
	struct vpu_reg *reg_codec;
	struct vpu_reg *reg_pproc;
	struct vpu_reg *reg_resev;
	struct vpu_dec_config dec_config;
	struct vpu_enc_config enc_config;

	bool auto_freq;
	bool bug_dec_addr;
	atomic_t freq_status;

	wait_queue_head_t *wait_secure_isr;
	bool secure_isr;
	atomic_t secure_mode;
	bool secure_irq_status;
	struct clk *aclk_vcodec;
	struct clk *hclk_vcodec;
	struct clk *clk_core;
	struct clk *clk_cabac;
	struct clk *pd_video;

	unsigned long aclk_vcodec_default_rate;
	unsigned long clk_core_default_rate;
	unsigned long clk_cabac_default_rate;

#ifdef CONFIG_RESET_CONTROLLER
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_niu_a;
	struct reset_control *rst_niu_h;
	struct reset_control *rst_core;
	struct reset_control *rst_cabac;
#endif
	struct device *dev;

	u32 irq_status;
	atomic_t reset_request;
	struct ion_client *ion_client;
	struct list_head mem_region_list;

	enum vcodec_device_id dev_id;

	enum VCODEC_RUNNING_MODE curr_mode;
	u32 prev_mode;

	struct delayed_work simulate_work;

	u32 mode_bit;
	u32 mode_ctrl;
	u32 *reg_base;
	u32 ioaddr;
	struct regmap *grf;
	u32 *grf_base;

	char *name;

	u32 subcnt;
	struct list_head subdev_list;

	struct vcodec_hw_ops *hw_ops;
	const struct vcodec_hw_var *hw_var;

	struct ion_handle *pf;
	ion_phys_addr_t pf_pa;
	unsigned long war_iova;
	struct regmap *cru;
	struct clk *p_cpll;
	struct clk *p_gpll;
	unsigned long cpll_rate;
	unsigned long gpll_rate;
};

#ifdef CONFIG_COMPAT
struct compat_vpu_request {
	compat_uptr_t req;
	u32 size;
};
#endif

/* debugfs root directory for all device (vpu, hevc).*/
static struct dentry *parent;

#ifdef CONFIG_DEBUG_FS
static int vcodec_debugfs_init(void);
static void vcodec_debugfs_exit(void);
static struct dentry *vcodec_debugfs_create_device_dir(
		char *dirname, struct dentry *parent);
static int debug_vcodec_open(struct inode *inode, struct file *file);

static const struct file_operations debug_vcodec_fops = {
	.open = debug_vcodec_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

#define VDPU_SOFT_RESET_REG	101
#define VDPU_CLEAN_CACHE_REG	516
#define VEPU_CLEAN_CACHE_REG	772
#define HEVC_CLEAN_CACHE_REG	260

#define VPU_REG_ENABLE(base, reg)	writel_relaxed(1, base + reg)

#define VDPU_SOFT_RESET(base)	VPU_REG_ENABLE(base, VDPU_SOFT_RESET_REG)
#define VDPU_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, VDPU_CLEAN_CACHE_REG)
#define VEPU_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, VEPU_CLEAN_CACHE_REG)
#define HEVC_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, HEVC_CLEAN_CACHE_REG)

#define VPU_POWER_OFF_DELAY		(4 * HZ) /* 4s */
#define VPU_TIMEOUT_DELAY		(2 * HZ) /* 2s */

static void time_record(struct vpu_task_info *task, int is_end)
{
	if (unlikely(debug & DEBUG_TIMING) && task)
		do_gettimeofday((is_end) ? (&task->end) : (&task->start));
}

static void time_diff(struct vpu_task_info *task)
{
	vpu_debug(DEBUG_TIMING, "%s task: %ld ms\n", task->name,
		  (task->end.tv_sec  - task->start.tv_sec)  * 1000 +
		  (task->end.tv_usec - task->start.tv_usec) / 1000);
}

static void vcodec_enter_mode(struct vpu_subdev_data *data)
{
	int bits;
	u32 raw = 0;
	struct vpu_service_info *pservice = data->pservice;
#if defined(CONFIG_VCODEC_MMU)
	struct vpu_subdev_data *subdata, *n;
#endif
	if (pservice->subcnt < 2 || pservice->mode_ctrl == 0) {
#if defined(CONFIG_VCODEC_MMU)
		if (data->mmu_dev && !test_bit(MMU_ACTIVATED, &data->state)) {
			set_bit(MMU_ACTIVATED, &data->state);
			if (atomic_read(&pservice->enabled))
				rockchip_iovmm_activate(data->dev);
			else
				BUG_ON(!atomic_read(&pservice->enabled));
		}
#endif
		return;
	}

	if (pservice->curr_mode == data->mode)
		return;

	vpu_debug(DEBUG_IOMMU, "vcodec enter mode %d\n", data->mode);
#if defined(CONFIG_VCODEC_MMU)
	list_for_each_entry_safe(subdata, n,
				 &pservice->subdev_list, lnk_service) {
		if (data != subdata && subdata->mmu_dev &&
		    test_bit(MMU_ACTIVATED, &subdata->state)) {
			clear_bit(MMU_ACTIVATED, &subdata->state);
			rockchip_iovmm_deactivate(subdata->dev);
		}
	}
#endif
	bits = 1 << pservice->mode_bit;
#ifdef CONFIG_MFD_SYSCON
	if (pservice->grf) {
		regmap_read(pservice->grf, pservice->mode_ctrl, &raw);

		if (data->mode == VCODEC_RUNNING_MODE_HEVC)
			regmap_write(pservice->grf, pservice->mode_ctrl,
				     raw | bits | (bits << 16));
		else
			regmap_write(pservice->grf, pservice->mode_ctrl,
				     (raw & (~bits)) | (bits << 16));
	} else if (pservice->grf_base) {
		u32 *grf_base = pservice->grf_base;

		raw = readl_relaxed(grf_base + pservice->mode_ctrl / 4);
		if (data->mode == VCODEC_RUNNING_MODE_HEVC)
			writel_relaxed(raw | bits | (bits << 16),
				       grf_base + pservice->mode_ctrl / 4);
		else
			writel_relaxed((raw & (~bits)) | (bits << 16),
				       grf_base + pservice->mode_ctrl / 4);
	} else {
		vpu_err("no grf resource define, switch decoder failed\n");
		return;
	}
#else
	if (pservice->grf_base) {
		u32 *grf_base = pservice->grf_base;

		raw = readl_relaxed(grf_base + pservice->mode_ctrl / 4);
		if (data->mode == VCODEC_RUNNING_MODE_HEVC)
			writel_relaxed(raw | bits | (bits << 16),
				       grf_base + pservice->mode_ctrl / 4);
		else
			writel_relaxed((raw & (~bits)) | (bits << 16),
				       grf_base + pservice->mode_ctrl / 4);
	} else {
		vpu_err("no grf resource define, switch decoder failed\n");
		return;
	}
#endif
#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev && !test_bit(MMU_ACTIVATED, &data->state)) {
		set_bit(MMU_ACTIVATED, &data->state);
		if (atomic_read(&pservice->enabled))
			rockchip_iovmm_activate(data->dev);
		else
			BUG_ON(!atomic_read(&pservice->enabled));
	}
#endif
	pservice->prev_mode = pservice->curr_mode;
	pservice->curr_mode = data->mode;
}

static void vcodec_exit_mode(struct vpu_subdev_data *data)
{
#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
		clear_bit(MMU_ACTIVATED, &data->state);
		rockchip_iovmm_deactivate(data->dev);
	}
#endif
	/*
	 * In case of VPU Combo, it require HW switch its running mode
	 * before the other HW component start work. set current HW running
	 * mode to none, can ensure HW switch to its reqired mode properly.
	 */
	data->pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
}

static int vpu_get_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	struct device *dev = pservice->dev;

	switch (pservice->dev_id) {
	case VCODEC_DEVICE_ID_HEVC:
		pservice->pd_video = devm_clk_get(dev, "pd_hevc");
		if (IS_ERR(pservice->pd_video)) {
			dev_err(dev, "failed on clk_get pd_hevc\n");
			return -1;
		}
		/* shall fallthrough to get more clock resources */
	case VCODEC_DEVICE_ID_COMBO:
	case VCODEC_DEVICE_ID_RKVDEC:
		pservice->clk_cabac = devm_clk_get(dev, "clk_cabac");
		if (IS_ERR(pservice->clk_cabac)) {
			dev_err(dev, "failed on clk_get clk_cabac\n");
			pservice->clk_cabac = NULL;
		} else {
			pservice->clk_cabac_default_rate =
				clk_get_rate(pservice->clk_cabac);
		}

		pservice->clk_core = devm_clk_get(dev, "clk_core");
		if (IS_ERR(pservice->clk_core)) {
			dev_err(dev, "failed on clk_get clk_core\n");
			return -1;
		} else {
			pservice->clk_core_default_rate =
				clk_get_rate(pservice->clk_core);
		}
		/* shall fallthrough to get more clock resources */
	case VCODEC_DEVICE_ID_VPU:
		pservice->aclk_vcodec = devm_clk_get(dev, "aclk_vcodec");
		if (IS_ERR(pservice->aclk_vcodec)) {
			dev_err(dev, "failed on clk_get aclk_vcodec\n");
			return -1;
		} else {
			pservice->aclk_vcodec_default_rate =
				clk_get_rate(pservice->aclk_vcodec);
		}

		pservice->hclk_vcodec = devm_clk_get(dev, "hclk_vcodec");
		if (IS_ERR(pservice->hclk_vcodec)) {
			dev_err(dev, "failed on clk_get hclk_vcodec\n");
			return -1;
		}
		if (pservice->pd_video == NULL) {
			pservice->pd_video = devm_clk_get(dev, "pd_video");
			if (IS_ERR(pservice->pd_video)) {
				pservice->pd_video = NULL;
				dev_info(dev, "do not have pd_video\n");
			}
		}
		break;
	default:
		break;
	}

	return 0;
#else
	return 0;
#endif
}

static void vpu_put_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	if (pservice->pd_video)
		devm_clk_put(pservice->dev, pservice->pd_video);
	if (pservice->aclk_vcodec)
		devm_clk_put(pservice->dev, pservice->aclk_vcodec);
	if (pservice->hclk_vcodec)
		devm_clk_put(pservice->dev, pservice->hclk_vcodec);
	if (pservice->clk_core)
		devm_clk_put(pservice->dev, pservice->clk_core);
	if (pservice->clk_cabac)
		devm_clk_put(pservice->dev, pservice->clk_cabac);
#endif
}

static inline void safe_reset(struct reset_control *rst)
{
	if (rst)
		reset_control_assert(rst);
}

static inline void safe_unreset(struct reset_control *rst)
{
	if (rst)
		reset_control_deassert(rst);
}

static void rkvdec_set_clk_by_cru(unsigned long vcodec_rate,
				  unsigned long core_rate,
				  unsigned long cabac_rate)
{
	static unsigned long vcodec_old_rate, core_old_rate, cabac_old_rate;
	int aclk_div, core_div, cabac_div;

	if (IS_ERR_OR_NULL(vpu_dvfs->cru)) {
		clk_set_rate(vpu_dvfs->aclk_vcodec, vcodec_rate);
		clk_set_rate(vpu_dvfs->clk_core, core_rate);
		clk_set_rate(vpu_dvfs->clk_cabac, cabac_rate);
	} else {
		if (vcodec_rate != vcodec_old_rate) {
			if (vpu_dvfs->cpll_rate % vcodec_rate == 0) {
				aclk_div = vpu_dvfs->cpll_rate / vcodec_rate;
				regmap_write(vpu_dvfs->cru, 0x1c0,
					     0x00df0000 | (aclk_div - 1));
				vpu_dvfs->aclk_vcodec->rate =
					vpu_dvfs->cpll_rate / aclk_div;
				clk_set_parent(vpu_dvfs->aclk_vcodec,
					       vpu_dvfs->p_cpll);
			} else {
				aclk_div = vpu_dvfs->gpll_rate / vcodec_rate;
				regmap_write(vpu_dvfs->cru, 0x1c0,
					     0x00df0040 | aclk_div);
				vpu_dvfs->aclk_vcodec->rate =
					vpu_dvfs->gpll_rate / (aclk_div + 1);
				clk_set_parent(vpu_dvfs->aclk_vcodec,
					       vpu_dvfs->p_gpll);
			}
			vcodec_old_rate = vcodec_rate;
		}
		if (core_rate != core_old_rate) {
			if (vpu_dvfs->cpll_rate % core_rate == 0) {
				core_div = vpu_dvfs->cpll_rate / core_rate;
				regmap_write(vpu_dvfs->cru, 0x1c4,
					     0x00df0000 | (core_div - 1));
				vpu_dvfs->clk_core->rate =
					vpu_dvfs->cpll_rate / core_div;
				clk_set_parent(vpu_dvfs->clk_core,
					       vpu_dvfs->p_cpll);
			} else {
				core_div = vpu_dvfs->gpll_rate / core_rate;
				regmap_write(vpu_dvfs->cru, 0x1c4,
					     0x00df0040 | core_div);
				vpu_dvfs->clk_core->rate =
					vpu_dvfs->gpll_rate / (core_div + 1);
				clk_set_parent(vpu_dvfs->clk_core,
					       vpu_dvfs->p_gpll);
			}
			core_old_rate = core_rate;
		}
		if (cabac_rate != cabac_old_rate) {
			if (vpu_dvfs->cpll_rate % cabac_rate == 0) {
				cabac_div = vpu_dvfs->cpll_rate / cabac_rate;
				regmap_write(vpu_dvfs->cru, 0x1c0,
					     0xdf000000 |
					     ((cabac_div - 1) << 8));
				vpu_dvfs->clk_cabac->rate =
					vpu_dvfs->cpll_rate / cabac_div;
				clk_set_parent(vpu_dvfs->clk_cabac,
					       vpu_dvfs->p_cpll);
			} else {
				cabac_div = vpu_dvfs->gpll_rate / cabac_rate;
				regmap_write(vpu_dvfs->cru, 0x1c0,
					     0xdf004000 | (cabac_div << 8));
				vpu_dvfs->clk_cabac->rate =
					vpu_dvfs->gpll_rate / (cabac_div + 1);
				clk_set_parent(vpu_dvfs->clk_cabac,
					       vpu_dvfs->p_gpll);
			}
			cabac_old_rate = cabac_rate;
		}
	}
}

static DEFINE_MUTEX(rkvdec_set_clk_lock);

void rkvdec_set_clk(unsigned long vcodec_rate,
		    unsigned long core_rate,
		    unsigned long cabac_rate,
		    int thermal_en)
{
	static unsigned long vcodec_old_rate = 500 * MHZ;
	static unsigned long core_old_rate = 250 * MHZ;
	static unsigned long cabac_old_rate = 400 * MHZ;
	static int thermal_st;

	mutex_lock(&rkvdec_set_clk_lock);
	if (thermal_en < 0) {
		vcodec_old_rate = vcodec_rate;
		core_old_rate = core_rate;
		cabac_old_rate = cabac_rate;
		if (!thermal_st)
			rkvdec_set_clk_by_cru(vcodec_rate,
					      core_rate,
					      cabac_rate);
		else
			rkvdec_set_clk_by_cru(vcodec_rate / 4,
					      core_rate / 4,
					      cabac_rate / 4);
		mutex_unlock(&rkvdec_set_clk_lock);
		return;
	}

	if (thermal_en == 0)  {
		rkvdec_set_clk_by_cru(vcodec_old_rate,
				      core_old_rate,
				      cabac_old_rate);
	} else if (thermal_en == 1)  {
		rkvdec_set_clk_by_cru(vcodec_old_rate / 4,
				      core_old_rate / 4,
				      cabac_old_rate / 4);
	}
	thermal_st = thermal_en;
	mutex_unlock(&rkvdec_set_clk_lock);
}

static void dvfs_rkvdec_set_clk(u32 vcodec_rate, u32 core_rate, u32 cabac_rate)
{
	static struct clk *core_clk, *cabac_clk;
	static struct dvfs_node *clk_rkvdec_dvfs_node;

	if (IS_ERR_OR_NULL(clk_rkvdec_dvfs_node)) {
		clk_rkvdec_dvfs_node = clk_get_dvfs_node("aclk_rkvdec");
		if (IS_ERR_OR_NULL(clk_rkvdec_dvfs_node))
			return;
	}

	if (IS_ERR_OR_NULL(core_clk)) {
		core_clk = clk_get(NULL, "clk_vdec_core");
		if (IS_ERR_OR_NULL(core_clk))
			return;
	}

	if (IS_ERR_OR_NULL(cabac_clk)) {
		cabac_clk = clk_get(NULL, "clk_vdec_cabac");
		if (IS_ERR_OR_NULL(cabac_clk))
			return;
	}

	if (core_rate > clk_get_rate(core_clk)) {
		dvfs_clk_set_rate(clk_rkvdec_dvfs_node, vcodec_rate);
		clk_set_rate(core_clk, core_rate);
		clk_set_rate(cabac_clk, cabac_rate);
	} else {
		clk_set_rate(core_clk, core_rate);
		clk_set_rate(cabac_clk, cabac_rate);
		dvfs_clk_set_rate(clk_rkvdec_dvfs_node, vcodec_rate);
	}
}

static void vpu_reset(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	enum pmu_idle_req type = IDLE_REQ_VIDEO;
	int ret = -1;

	atomic_set(&pservice->reset_request, 0);

	if (pservice->hw_var) {
		type = pservice->hw_var->pmu_type;
	} else {
		if (pservice->dev_id == VCODEC_DEVICE_ID_HEVC)
			type = IDLE_REQ_HEVC;
	}

	pr_info("%s: reset start", dev_name(pservice->dev));

	WARN_ON(pservice->reg_codec != NULL);
	WARN_ON(pservice->reg_pproc != NULL);
	WARN_ON(pservice->reg_resev != NULL);
	pservice->reg_codec = NULL;
	pservice->reg_pproc = NULL;
	pservice->reg_resev = NULL;

#ifdef CONFIG_RESET_CONTROLLER
	if (pservice->rst_a && pservice->rst_h) {
		if (rockchip_pmu_ops.set_idle_request)
			ret = rockchip_pmu_ops.set_idle_request(type, true);
		if (ret < 0) {
			pr_info("msch idle request and reset rkvdec\n");
			sip_smc_vpu_reset(0, 0, 0);
		} else {
			if (pservice->hw_ops->reduce_freq)
				pservice->hw_ops->reduce_freq(pservice);

			safe_reset(pservice->rst_niu_a);
			safe_reset(pservice->rst_niu_h);
			safe_reset(pservice->rst_a);
			safe_reset(pservice->rst_h);
			safe_reset(pservice->rst_core);
			safe_reset(pservice->rst_cabac);
			udelay(2);
			safe_unreset(pservice->rst_niu_h);
			safe_unreset(pservice->rst_niu_a);
			safe_unreset(pservice->rst_a);
			safe_unreset(pservice->rst_h);
			safe_unreset(pservice->rst_core);
			safe_unreset(pservice->rst_cabac);

			if (rockchip_pmu_ops.set_idle_request)
				rockchip_pmu_ops.set_idle_request(type, false);
		}
	}
#endif

#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev && test_bit(MMU_PAGEFAULT, &data->state)) {
		clear_bit(MMU_ACTIVATED, &data->state);
		clear_bit(MMU_PAGEFAULT, &data->state);
	} else if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
		clear_bit(MMU_ACTIVATED, &data->state);
		if (atomic_read(&pservice->enabled))
			rockchip_iovmm_deactivate(data->dev);
		else
			BUG_ON(!atomic_read(&pservice->enabled));
	}
#endif
	pr_info("done\n");
}

static void reg_deinit(struct vpu_subdev_data *data, struct vpu_reg *reg);
static void vpu_service_session_clear(struct vpu_subdev_data *data,
				      struct vpu_session *session)
{
	struct vpu_reg *reg, *n;

	list_for_each_entry_safe(reg, n, &session->waiting, session_link) {
		reg_deinit(data, reg);
	}
	list_for_each_entry_safe(reg, n, &session->running, session_link) {
		reg_deinit(data, reg);
	}
	list_for_each_entry_safe(reg, n, &session->done, session_link) {
		reg_deinit(data, reg);
	}
}

static void dump_reg(u32 *regs, int count)
{
	int i;

	pr_info("dumping vpu_device registers:");

	for (i = 0; i < count; i++)
		pr_info("reg[%02d]: %08x\n", i, readl_relaxed(regs + i));
}

static void vpu_service_dump(struct vpu_service_info *pservice)
{
}

#if VCODEC_CLOCK_ENABLE
static void set_div_clk(struct clk *clock, int divide)
{
	struct clk *parent = clk_get_parent(clock);
	unsigned long rate = clk_get_rate(parent);

	clk_set_rate(clock, (rate / divide) + 1);
}
#endif

static void vpu_service_power_off(struct vpu_service_info *pservice)
{
	int total_running;
	int ret = atomic_add_unless(&pservice->enabled, -1, 0);
#if defined(CONFIG_VCODEC_MMU)
	struct vpu_subdev_data *data = NULL, *n;
#endif
	if (!ret)
		return;

	total_running = atomic_read(&pservice->total_running);
	if (total_running) {
		pr_alert("alert: power off when %d task running!!\n",
			 total_running);
		mdelay(50);
		pr_alert("alert: delay 50 ms for running task\n");
		vpu_service_dump(pservice);
	}

	pr_info("%s: power off...", dev_name(pservice->dev));

	udelay(5);

#if defined(CONFIG_VCODEC_MMU)
	list_for_each_entry_safe(data, n, &pservice->subdev_list, lnk_service) {
		if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
			clear_bit(MMU_ACTIVATED, &data->state);
			rockchip_iovmm_deactivate(data->dev);
		}
	}
	pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
#endif

	if (pservice->hw_ops->power_off)
		pservice->hw_ops->power_off(pservice);

	atomic_add(1, &pservice->power_off_cnt);
	wake_unlock(&pservice->wake_lock);
	pr_info("done\n");
}

static inline void vpu_queue_power_off_work(struct vpu_service_info *pservice)
{
	queue_delayed_work(system_nrt_wq, &pservice->power_off_work,
			   VPU_POWER_OFF_DELAY);
}

static void vpu_power_off_work(struct work_struct *work_s)
{
	struct delayed_work *dlwork = container_of(work_s,
			struct delayed_work, work);
	struct vpu_service_info *pservice = container_of(dlwork,
			struct vpu_service_info, power_off_work);

	if (mutex_trylock(&pservice->lock)) {
		vpu_service_power_off(pservice);
		mutex_unlock(&pservice->lock);
	} else {
		/* Come back later if the device is busy... */
		vpu_queue_power_off_work(pservice);
	}
}

static void vpu_service_power_on(struct vpu_service_info *pservice)
{
	int ret;
	ktime_t now = ktime_get();

	if (ktime_to_ns(ktime_sub(now, pservice->last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&pservice->power_off_work);
		vpu_queue_power_off_work(pservice);
		pservice->last = now;
	}
	ret = atomic_add_unless(&pservice->enabled, 1, 1);
	if (!ret)
		return;

	pr_info("%s: power on\n", dev_name(pservice->dev));

	if (pservice->hw_ops->power_on)
		pservice->hw_ops->power_on(pservice);

	udelay(5);
	atomic_add(1, &pservice->power_on_cnt);
	wake_lock(&pservice->wake_lock);
}

static inline bool reg_check_interlace(struct vpu_reg *reg)
{
	u32 type = (reg->reg[3] & (1 << 23));

	return (type > 0);
}

static inline enum VPU_DEC_FMT reg_check_fmt(struct vpu_reg *reg)
{
	enum VPU_DEC_FMT type = (enum VPU_DEC_FMT)((reg->reg[3] >> 28) & 0xf);

	return type;
}

static inline int reg_probe_width(struct vpu_reg *reg)
{
	int width_in_mb = reg->reg[4] >> 23;

	return width_in_mb * 16;
}

static inline int reg_probe_hevc_y_stride(struct vpu_reg *reg)
{
	int y_virstride = reg->reg[8];

	return y_virstride;
}

static int vcodec_fd_to_iova(struct vpu_subdev_data *data,
			     struct vpu_reg *reg, int fd)
{
	struct vpu_service_info *pservice = data->pservice;
	struct ion_handle *hdl;
	int ret = 0;
	struct vcodec_mem_region *mem_region;

	hdl = ion_import_dma_buf(pservice->ion_client, fd);
	if (IS_ERR(hdl)) {
		vpu_err("import dma-buf from fd %d failed\n", fd);
		return PTR_ERR(hdl);
	}
	mem_region = kzalloc(sizeof(*mem_region), GFP_KERNEL);

	if (mem_region == NULL) {
		vpu_err("allocate memory for iommu memory region failed\n");
		ion_free(pservice->ion_client, hdl);
		return -ENOMEM;
	}

	mem_region->hdl = hdl;
	if (data->mmu_dev)
		ret = ion_map_iommu(data->dev, pservice->ion_client,
				    mem_region->hdl, &mem_region->iova,
				    &mem_region->len);
	else
		ret = ion_phys(pservice->ion_client,
			       mem_region->hdl,
			       (ion_phys_addr_t *)&mem_region->iova,
			       (size_t *)&mem_region->len);

	if (ret < 0) {
		vpu_err("fd %d ion map iommu failed\n", fd);
		kfree(mem_region);
		ion_free(pservice->ion_client, hdl);
		return ret;
	}
	INIT_LIST_HEAD(&mem_region->reg_lnk);
	list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);
	return mem_region->iova;
}

/*
 * NOTE: rkvdec/rkhevc put scaling list address in pps buffer hardware will read
 * it by pps id in video stream data.
 *
 * So we need to translate the address in iommu case. The address data is also
 * 10bit fd + 22bit offset mode.
 * Because userspace decoder do not give the pps id in the register file sets
 * kernel driver need to translate each scaling list address in pps buffer which
 * means 256 pps for H.264, 64 pps for H.265.
 *
 * In order to optimize the performance kernel driver ask userspace decoder to
 * set all scaling list address in pps buffer to the same one which will be used
 * on current decoding task. Then kernel driver can only translate the first
 * address then copy it all pps buffer.
 */
static void fill_scaling_list_addr_in_pps(
		struct vpu_subdev_data *data,
		struct vpu_reg *reg,
		char *pps,
		int pps_info_count,
		int pps_info_size,
		int scaling_list_addr_offset)
{
	int base = scaling_list_addr_offset;
	int scaling_fd = 0;
	u32 scaling_offset;

	scaling_offset  = (u32)pps[base + 0];
	scaling_offset += (u32)pps[base + 1] << 8;
	scaling_offset += (u32)pps[base + 2] << 16;
	scaling_offset += (u32)pps[base + 3] << 24;

	scaling_fd = scaling_offset & 0x3ff;
	scaling_offset = scaling_offset >> 10;

	if (scaling_fd > 0) {
		int i = 0;
		u32 tmp = vcodec_fd_to_iova(data, reg, scaling_fd);

		if (!IS_ERR_VALUE(tmp)) {
			tmp += scaling_offset;

			for (i = 0; i < pps_info_count;
			      i++, base += pps_info_size) {
				pps[base + 0] = (tmp >>  0) & 0xff;
				pps[base + 1] = (tmp >>  8) & 0xff;
				pps[base + 2] = (tmp >> 16) & 0xff;
				pps[base + 3] = (tmp >> 24) & 0xff;
			}
		}
	}
}

static int vcodec_bufid_to_iova(struct vpu_subdev_data *data, const u8 *tbl,
				int size, struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = reg->task;
	enum FORMAT_TYPE type;
	struct ion_handle *hdl;
	int ret = 0;
	struct vcodec_mem_region *mem_region;
	int i;
	int offset = 0;

	if (tbl == NULL || size <= 0) {
		dev_err(pservice->dev, "input arguments invalidate\n");
		return -1;
	}

	if (task->get_fmt) {
		type = task->get_fmt(reg->reg);
	} else {
		pr_err("invalid task with NULL get_fmt\n");
		return -1;
	}

	for (i = 0; i < size; i++) {
		int usr_fd = reg->reg[tbl[i]] & 0x3FF;

		/* if userspace do not set the fd at this register, skip */
		if (usr_fd == 0)
			continue;

		/*
		 * special offset scale case
		 *
		 * This translation is for fd + offset translation.
		 * One register has 32bits. We need to transfer both buffer file
		 * handle and the start address offset so we packet file handle
		 * and offset together using below format.
		 *
		 *  0~9  bit for buffer file handle range 0 ~ 1023
		 * 10~31 bit for offset range 0 ~ 4M
		 *
		 * But on 4K case the offset can be larger the 4M
		 * So on H.264 4K vpu/vpu2 decoder we scale the offset by 16
		 * But MPEG4 will use the same register for colmv and it do not
		 * need scale.
		 *
		 * RKVdec do not have this issue.
		 */
		if ((type == FMT_H264D || type == FMT_VP9D) &&
		    task->reg_dir_mv > 0 && task->reg_dir_mv == tbl[i])
			offset = reg->reg[tbl[i]] >> 10 << 4;
		else
			offset = reg->reg[tbl[i]] >> 10;

		vpu_debug(DEBUG_IOMMU, "pos %3d fd %3d offset %10d\n",
			  tbl[i], usr_fd, offset);

		hdl = ion_import_dma_buf(pservice->ion_client, usr_fd);
		if (IS_ERR(hdl)) {
			dev_err(pservice->dev,
				"import dma-buf from fd %d failed, reg[%d]\n",
				usr_fd, tbl[i]);
			return PTR_ERR(hdl);
		}

		if (task->reg_pps > 0 && task->reg_pps == tbl[i]) {
			int pps_info_offset;
			int pps_info_count;
			int pps_info_size;
			int scaling_list_addr_offset;

			switch (type) {
			case FMT_H264D: {
				pps_info_offset = offset;
				pps_info_count = 256;
				pps_info_size = 32;
				scaling_list_addr_offset = 23;
			} break;
			case FMT_H265D: {
				pps_info_offset = 0;
				pps_info_count = 64;
				pps_info_size = 80;
				scaling_list_addr_offset = 74;
			} break;
			default: {
				pps_info_offset = 0;
				pps_info_count = 0;
				pps_info_size = 0;
				scaling_list_addr_offset = 0;
			} break;
			}

			vpu_debug(DEBUG_PPS_FILL,
				  "scaling list filling parameter:\n");
			vpu_debug(DEBUG_PPS_FILL,
				  "pps_info_offset %d\n", pps_info_offset);
			vpu_debug(DEBUG_PPS_FILL,
				  "pps_info_count  %d\n", pps_info_count);
			vpu_debug(DEBUG_PPS_FILL,
				  "pps_info_size   %d\n", pps_info_size);
			vpu_debug(DEBUG_PPS_FILL,
				  "scaling_list_addr_offset %d\n",
				  scaling_list_addr_offset);

			if (pps_info_count) {
				char *pps = (char *)ion_map_kernel(
						pservice->ion_client, hdl);
				vpu_debug(DEBUG_PPS_FILL,
					  "scaling list setting pps %p\n", pps);
				pps += pps_info_offset;

				fill_scaling_list_addr_in_pps(
						data, reg, pps,
						pps_info_count,
						pps_info_size,
						scaling_list_addr_offset);
			}
		}

		mem_region = kzalloc(sizeof(*mem_region), GFP_KERNEL);

		if (!mem_region) {
			ion_free(pservice->ion_client, hdl);
			return -ENOMEM;
		}

		mem_region->hdl = hdl;
		mem_region->reg_idx = tbl[i];

		if (data->mmu_dev)
			ret = ion_map_iommu(data->dev,
					    pservice->ion_client,
					    mem_region->hdl,
					    &mem_region->iova,
					    &mem_region->len);
		else
			ret = ion_phys(pservice->ion_client,
				       mem_region->hdl,
				       (ion_phys_addr_t *)&mem_region->iova,
				       (size_t *)&mem_region->len);

		if (ret < 0) {
			dev_err(pservice->dev, "reg %d fd %d ion map iommu failed\n",
				tbl[i], usr_fd);
			kfree(mem_region);
			ion_free(pservice->ion_client, hdl);
			return ret;
		}

		/*
		 * special for vpu dec num 12: record decoded length
		 * hacking for decoded length
		 * NOTE: not a perfect fix, the fd is not recorded
		 */
		if (task->reg_len > 0 && task->reg_len == tbl[i]) {
			reg->dec_base = mem_region->iova + offset;
			vpu_debug(DEBUG_REGISTER, "dec_set %08x\n",
				  reg->dec_base);
		}

		reg->reg[tbl[i]] = mem_region->iova + offset;
		INIT_LIST_HEAD(&mem_region->reg_lnk);
		list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);
	}

	return 0;
}

static int vcodec_reg_address_translate(struct vpu_subdev_data *data,
					struct vpu_reg *reg)
{
	enum FORMAT_TYPE type = reg->task->get_fmt(reg->reg);

	if (type < FMT_TYPE_BUTT) {
		const struct vpu_trans_info *info = &reg->trans[type];
		const u8 *tbl = info->table;
		int size = info->count;

		return vcodec_bufid_to_iova(data, tbl, size, reg);
	}
	pr_err("found invalid format type!\n");
	return -1;
}

static void translate_extra_info(struct vpu_reg *reg,
				 struct extra_info_for_iommu *ext_inf)
{
	if (ext_inf != NULL && ext_inf->magic == EXTRA_INFO_MAGIC) {
		int i;

		for (i = 0; i < ext_inf->cnt; i++) {
			vpu_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n",
				  ext_inf->elem[i].index,
				  ext_inf->elem[i].offset);
			reg->reg[ext_inf->elem[i].index] +=
				ext_inf->elem[i].offset;
		}
	}
}

static struct vpu_reg *reg_init(struct vpu_subdev_data *data,
				struct vpu_session *session,
				void __user *src, u32 size)
{
	struct vpu_service_info *pservice = data->pservice;
	int extra_size = 0;
	struct extra_info_for_iommu extra_info;
	struct vpu_reg *reg = kzalloc(sizeof(*reg) + data->reg_size,
				      GFP_KERNEL);

	vpu_debug_enter();

	if (NULL == reg) {
		vpu_err("error: kmalloc failed\n");
		return NULL;
	}

	if (size > data->reg_size) {
		extra_size = size - data->reg_size;
		size = data->reg_size;
	}
	reg->session = session;
	reg->data = data;
	reg->type = session->type;
	reg->size = size;
	reg->freq = VPU_FREQ_DEFAULT;
	reg->task = &data->task_info[session->type];
	reg->trans = data->trans_info;
	reg->reg = (u32 *)&reg[1];
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

	INIT_LIST_HEAD(&reg->mem_region_list);

	if (copy_from_user(&reg->reg[0], (void __user *)src, size)) {
		vpu_err("error: copy_from_user failed.\n");
		kfree(reg);
		return NULL;
	}

	if (extra_size) {
		if (copy_from_user(&extra_info, (u8 *)src + size, extra_size)) {
			vpu_err("error: copy_from_user failed.\n");
			kfree(reg);
			return NULL;
		}
	} else {
		memset(&extra_info, 0, sizeof(extra_info));
	}

	if (0 > vcodec_reg_address_translate(data, reg)) {
		vpu_err("error: translate reg address failed.\n");

		if (unlikely(debug & DEBUG_DUMP_ERR_REG))
			dump_reg((u32 *)src, size >> 2);

		kfree(reg);
		return NULL;
	}

	translate_extra_info(reg, &extra_info);

	mutex_lock(&pservice->lock);
	list_add_tail(&reg->status_link, &pservice->waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&pservice->lock);

	if (pservice->auto_freq && pservice->hw_ops->get_freq)
		pservice->hw_ops->get_freq(data, reg);

	vpu_debug_leave();
	return reg;
}

static void reg_deinit(struct vpu_subdev_data *data, struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vcodec_mem_region *mem_region = NULL, *n;

	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	if (reg == pservice->reg_codec)
		pservice->reg_codec = NULL;
	if (reg == pservice->reg_pproc)
		pservice->reg_pproc = NULL;

	/* release memory region attach to this registers table. */
	list_for_each_entry_safe(mem_region, n,
				 &reg->mem_region_list, reg_lnk) {
		ion_free(pservice->ion_client, mem_region->hdl);
		list_del_init(&mem_region->reg_lnk);
		kfree(mem_region);
	}

	kfree(reg);
}

static void reg_from_wait_to_run(struct vpu_service_info *pservice,
				 struct vpu_reg *reg)
{
	vpu_debug_enter();
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
	vpu_debug_leave();
}

static void reg_copy_from_hw(struct vpu_reg *reg, u32 *src, u32 count)
{
	int i;
	u32 *dst = reg->reg;

	vpu_debug_enter();
	for (i = 0; i < count; i++, src++)
		*dst++ = readl_relaxed(src);

	if (unlikely(debug & DEBUG_GET_REG)) {
		dst = (u32 *)&reg->reg[0];

		for (i = 0; i < count; i++)
			pr_info("get reg[%02d] %08x\n", i, dst[i]);
	}

	vpu_debug_leave();
}

static void reg_from_run_to_done(struct vpu_subdev_data *data,
				 struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_hw_info *hw_info = data->hw_info;
	struct vpu_task_info *task = reg->task;

	vpu_debug_enter();

	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->done);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->done);

	switch (reg->type) {
	case VPU_ENC: {
		pservice->reg_codec = NULL;
		reg_copy_from_hw(reg, data->enc_dev.regs, hw_info->enc_reg_num);
		reg->reg[task->reg_irq] = pservice->irq_status;
	} break;
	case VPU_DEC: {
		pservice->reg_codec = NULL;
		reg_copy_from_hw(reg, data->dec_dev.regs, hw_info->dec_reg_num);

		/* revert hack for decoded length */
		if (task->reg_len > 0) {
			int reg_len = task->reg_len;
			u32 dec_get = reg->reg[reg_len];
			s32 dec_length = dec_get - reg->dec_base;

			vpu_debug(DEBUG_REGISTER,
				  "dec_get %08x dec_length %d\n",
				  dec_get, dec_length);
			reg->reg[reg_len] = dec_length << 10;
		}

		reg->reg[task->reg_irq] = pservice->irq_status;
	} break;
	case VPU_PP: {
		pservice->reg_pproc = NULL;
		reg_copy_from_hw(reg, data->dec_dev.regs, hw_info->dec_reg_num);
		writel_relaxed(0, data->dec_dev.regs + task->reg_irq);
	} break;
	case VPU_DEC_PP: {
		u32 pipe_mode;
		u32 *regs = data->dec_dev.regs;

		pservice->reg_codec = NULL;
		pservice->reg_pproc = NULL;

		reg_copy_from_hw(reg, data->dec_dev.regs, hw_info->dec_reg_num);

		/* NOTE: remove pp pipeline mode flag first */
		pipe_mode = readl_relaxed(regs + task->reg_pipe);
		pipe_mode &= ~task->pipe_mask;
		writel_relaxed(pipe_mode, regs + task->reg_pipe);

		/* revert hack for decoded length */
		if (task->reg_len > 0) {
			int reg_len = task->reg_len;
			u32 dec_get = reg->reg[reg_len];
			s32 dec_length = dec_get - reg->dec_base;

			vpu_debug(DEBUG_REGISTER,
				  "dec_get %08x dec_length %d\n",
				  dec_get, dec_length);
			reg->reg[reg_len] = dec_length << 10;
		}

		reg->reg[task->reg_irq] = pservice->irq_status;
	} break;
	default: {
		vpu_err("error: copy reg from hw with unknown type %d\n",
			reg->type);
	} break;
	}
	vcodec_exit_mode(data);

	atomic_sub(1, &reg->session->task_running);
	atomic_sub(1, &pservice->total_running);
	wake_up(&reg->session->wait);

	vpu_debug_leave();
}

static void reg_copy_to_hw(struct vpu_subdev_data *data, struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = reg->task;
	struct vpu_hw_info *hw_info = data->hw_info;
	int i;
	u32 *src = (u32 *)&reg->reg[0];
	u32 enable_mask = task->enable_mask;
	u32 gating_mask = task->gating_mask;
	u32 reg_en = task->reg_en;

	vpu_debug_enter();

	atomic_add(1, &pservice->total_running);
	atomic_add(1, &reg->session->task_running);

	if (pservice->auto_freq && pservice->hw_ops->set_freq)
		pservice->hw_ops->set_freq(pservice, reg);

	vcodec_enter_mode(data);

	switch (reg->type) {
	case VPU_ENC: {
		u32 *dst = data->enc_dev.regs;
		u32 base = 0;
		u32 end  = hw_info->enc_reg_num;
		/* u32 reg_gating = task->reg_gating; */

		pservice->reg_codec = reg;

		vpu_debug(DEBUG_TASK_INFO, "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		VEPU_CLEAN_CACHE(dst);

		if (debug & DEBUG_SET_REG)
			for (i = base; i < end; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		/*
		 * NOTE: encoder need to setup mode first
		 */
		writel_relaxed(src[reg_en] & enable_mask, dst + reg_en);

		/* NOTE: encoder gating is not on enable register */
		/* src[reg_gating] |= gating_mask; */

		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		writel(src[reg_en], dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	case VPU_DEC: {
		u32 *dst = data->dec_dev.regs;
		u32 len = hw_info->dec_reg_num;
		u32 base = hw_info->base_dec;
		u32 end  = hw_info->end_dec;

		pservice->reg_codec = reg;

		vpu_debug(DEBUG_TASK_INFO, "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		VDPU_CLEAN_CACHE(dst);

		/* on rkvdec set cache size to 64byte */
		if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC) {
			u32 *cache_base = dst + 0x100;
			u32 val = (debug & DEBUG_CACHE_32B) ? (0x3) : (0x13);

			writel_relaxed(val, cache_base + 0x07);
			writel_relaxed(val, cache_base + 0x17);
		}

		if (debug & DEBUG_SET_REG)
			for (i = 0; i < len; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		/*
		 * NOTE: The end register is invalid. Do NOT write to it
		 *       Also the base register must be written
		 */
		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		if (pservice->hw_var && pservice->hw_var->config)
			pservice->hw_var->config(data);

		writel(src[reg_en] | gating_mask, dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	case VPU_PP: {
		u32 *dst = data->dec_dev.regs;
		u32 base = hw_info->base_pp;
		u32 end  = hw_info->end_pp;

		pservice->reg_pproc = reg;

		vpu_debug(DEBUG_TASK_INFO, "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		if (debug & DEBUG_SET_REG)
			for (i = base; i < end; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		writel(src[reg_en] | gating_mask, dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	case VPU_DEC_PP: {
		u32 *dst = data->dec_dev.regs;
		u32 base = hw_info->base_dec_pp;
		u32 end  = hw_info->end_dec_pp;

		pservice->reg_codec = reg;
		pservice->reg_pproc = reg;

		vpu_debug(DEBUG_TASK_INFO, "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		/* VDPU_SOFT_RESET(dst); */
		VDPU_CLEAN_CACHE(dst);

		if (debug & DEBUG_SET_REG)
			for (i = base; i < end; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		/* NOTE: dec output must be disabled */

		writel(src[reg_en] | gating_mask, dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	default: {
		vpu_err("error: unsupport session type %d", reg->type);
		atomic_sub(1, &pservice->total_running);
		atomic_sub(1, &reg->session->task_running);
	} break;
	}

	vpu_debug_leave();
}

static void try_set_reg(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;

	vpu_debug_enter();
	if (!list_empty(&pservice->waiting)) {
		struct vpu_reg *reg_codec = pservice->reg_codec;
		struct vpu_reg *reg_pproc = pservice->reg_pproc;
		int can_set = 0;
		bool change_able = (reg_codec == NULL) && (reg_pproc == NULL);
		int reset_request = atomic_read(&pservice->reset_request);
		struct vpu_reg *reg = list_entry(pservice->waiting.next,
				struct vpu_reg, status_link);

		vpu_service_power_on(pservice);

		if (change_able || !reset_request) {
			switch (reg->type) {
			case VPU_ENC: {
				if (change_able)
					can_set = 1;
			} break;
			case VPU_DEC: {
				if (reg_codec == NULL)
					can_set = 1;
				if (pservice->auto_freq && (reg_pproc != NULL))
					can_set = 0;
			} break;
			case VPU_PP: {
				if (reg_codec == NULL) {
					if (reg_pproc == NULL)
						can_set = 1;
				} else {
					if ((reg_codec->type == VPU_DEC) &&
					    (reg_pproc == NULL))
						can_set = 1;

					/*
					 * NOTE:
					 * can not charge frequency
					 * when vpu is working
					 */
					if (pservice->auto_freq)
						can_set = 0;
				}
			} break;
			case VPU_DEC_PP: {
				if (change_able)
					can_set = 1;
				} break;
			default: {
				pr_err("undefined reg type %d\n", reg->type);
			} break;
			}
		}

		/* then check reset request */
		if (reset_request && !change_able)
			reset_request = 0;

		/* do reset before setting registers */
		if (reset_request)
			vpu_reset(data);

		if (can_set) {
			reg_from_wait_to_run(pservice, reg);
			reg_copy_to_hw(reg->data, reg);
		}
	} else {
		if (pservice->hw_ops->reduce_freq)
			pservice->hw_ops->reduce_freq(pservice);
	}

	vpu_debug_leave();
}

static int return_reg(struct vpu_subdev_data *data,
		      struct vpu_reg *reg, u32 __user *dst)
{
	struct vpu_hw_info *hw_info = data->hw_info;
	size_t size = reg->size;
	u32 base;

	vpu_debug_enter();
	switch (reg->type) {
	case VPU_ENC: {
		base = 0;
	} break;
	case VPU_DEC: {
		base = hw_info->base_dec_pp;
	} break;
	case VPU_PP: {
		base = hw_info->base_pp;
	} break;
	case VPU_DEC_PP: {
		base = hw_info->base_dec_pp;
	} break;
	default: {
		vpu_err("error: copy reg to user with unknown type %d\n",
			reg->type);
		return -EFAULT;
	} break;
	}

	if (copy_to_user(dst, &reg->reg[base], size)) {
		vpu_err("error: copy_to_user failed\n");
		return -EFAULT;
	}

	reg_deinit(data, reg);
	vpu_debug_leave();
	return 0;
}

static long vpu_service_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	struct vpu_subdev_data *data =
		container_of(filp->f_dentry->d_inode->i_cdev,
			     struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_session *session = (struct vpu_session *)filp->private_data;

	vpu_debug_enter();
	if (NULL == session)
		return -EINVAL;

	switch (cmd) {
	case VPU_IOC_SET_CLIENT_TYPE: {
		int secure_mode;

		session->type = (enum VPU_CLIENT_TYPE)arg;
		secure_mode = (session->type & 0xffff0000) >> 16;
		session->type = session->type & 0xffff;
		atomic_set(&pservice->secure_mode, secure_mode);
		vpu_debug(DEBUG_IOCTL, "pid %d set client type %d\n",
			  session->pid, session->type);
	} break;
	case VPU_IOC_GET_HW_FUSE_STATUS: {
		struct vpu_request req;

		vpu_debug(DEBUG_IOCTL, "pid %d get hw status %d\n",
			  session->pid, session->type);
		if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
			vpu_err("error: get hw status copy_from_user failed\n");
			return -EFAULT;
		} else {
			void *config = (session->type != VPU_ENC) ?
				       ((void *)&pservice->dec_config) :
				       ((void *)&pservice->enc_config);
			size_t size = (session->type != VPU_ENC) ?
				      (sizeof(struct vpu_dec_config)) :
				      (sizeof(struct vpu_enc_config));
			if (copy_to_user((void __user *)req.req,
					 config, size)) {
				vpu_err("error: get hw status copy_to_user failed type %d\n",
					session->type);
				return -EFAULT;
			}
		}
	} break;
	case VPU_IOC_SET_REG: {
		struct vpu_request req;
		struct vpu_reg *reg;

		vpu_debug(DEBUG_IOCTL, "pid %d set reg type %d\n",
			  session->pid, session->type);
		if (atomic_read(&pservice->secure_mode) == 1) {
			vpu_service_power_on(pservice);
			pservice->wait_secure_isr = &(session->wait);
			if (!pservice->secure_isr &&
			    !pservice->secure_irq_status) {
				enable_irq(data->irq_dec);
			}
		} else {
			if (copy_from_user(&req, (void __user *)arg,
					   sizeof(struct vpu_request))) {
				vpu_err("error: set reg copy_from_user failed\n");
				return -EFAULT;
			}
			reg = reg_init(data, session, (void __user *)req.req, req.size);
			if (NULL == reg) {
				return -EFAULT;
			} else {
				mutex_lock(&pservice->lock);
				try_set_reg(data);
				mutex_unlock(&pservice->lock);
			}
		}
	} break;
	case VPU_IOC_GET_REG: {
		struct vpu_request req;
		struct vpu_reg *reg;
		int ret;

		vpu_debug(DEBUG_IOCTL, "pid %d get reg type %d\n",
			  session->pid, session->type);
		if (atomic_read(&pservice->secure_mode) == 1) {
			ret = wait_event_timeout(session->wait,
						 pservice->secure_isr,
						 VPU_TIMEOUT_DELAY);
			if (ret < 0) {
				pr_info("warning: secure wait timeout\n");
				ret = 0;
			}
			pservice->secure_isr = false;
		} else {
			if (copy_from_user(&req, (void __user *)arg,
					   sizeof(struct vpu_request))) {
				vpu_err("error: get reg copy_from_user failed\n");
				return -EFAULT;
			}

			ret = wait_event_timeout(session->wait,
						 !list_empty(&session->done),
						 VPU_TIMEOUT_DELAY);

			if (!list_empty(&session->done)) {
				if (ret < 0)
					vpu_err("warning: pid %d wait task error ret %d\n",
						session->pid, ret);
				ret = 0;
			} else {
				if (unlikely(ret < 0)) {
					vpu_err("error: pid %d wait task ret %d\n",
						session->pid, ret);
				} else if (ret == 0) {
					vpu_err("error: pid %d wait %d task done timeout\n",
						session->pid,
						atomic_read(&session->task_running));
					ret = -ETIMEDOUT;
				}
			}

			if (ret < 0) {
				int task_running = atomic_read(&session->task_running);

				mutex_lock(&pservice->lock);
				vpu_service_dump(pservice);
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running,
						   &pservice->total_running);
					pr_err("%d task is running but not return, reset hardware...",
					       task_running);
					vpu_reset(data);
					pr_err("done\n");
				}
				vpu_service_session_clear(data, session);
				mutex_unlock(&pservice->lock);
				return ret;
			}

			mutex_lock(&pservice->lock);
			reg = list_entry(session->done.next,
					 struct vpu_reg, session_link);
			return_reg(data, reg, (u32 __user *)req.req);
			mutex_unlock(&pservice->lock);
		}
	} break;
	case VPU_IOC_PROBE_IOMMU_STATUS: {
		int iommu_enable = 1;

		vpu_debug(DEBUG_IOCTL, "VPU_IOC_PROBE_IOMMU_STATUS\n");

		if (copy_to_user((void __user *)arg,
				 &iommu_enable, sizeof(int))) {
			vpu_err("error: iommu status copy_to_user failed\n");
			return -EFAULT;
		}
	} break;
	default: {
		vpu_err("error: unknow vpu service ioctl cmd %x\n", cmd);
	} break;
	}
	vpu_debug_leave();
	return 0;
}

#ifdef CONFIG_COMPAT

#define COMPAT_VPU_IOC_SET_CLIENT_TYPE		_IOW(VPU_IOC_MAGIC, 1, u32)
#define COMPAT_VPU_IOC_GET_HW_FUSE_STATUS	_IOW(VPU_IOC_MAGIC, 2, u32)

#define COMPAT_VPU_IOC_SET_REG			_IOW(VPU_IOC_MAGIC, 3, u32)
#define COMPAT_VPU_IOC_GET_REG			_IOW(VPU_IOC_MAGIC, 4, u32)

#define COMPAT_VPU_IOC_PROBE_IOMMU_STATUS	_IOR(VPU_IOC_MAGIC, 5, u32)
#define COMPAT_VPU_IOC_SET_DRIVER_DATA		_IOW(VPU_IOC_MAGIC, 64, u32)

static long compat_vpu_service_ioctl(struct file *filp, unsigned int cmd,
				     unsigned long arg)
{
	struct vpu_subdev_data *data =
		container_of(filp->f_dentry->d_inode->i_cdev,
			     struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_session *session = (struct vpu_session *)filp->private_data;

	vpu_debug_enter();
	vpu_debug(3, "cmd %x, COMPAT_VPU_IOC_SET_CLIENT_TYPE %x\n", cmd,
		  (u32)COMPAT_VPU_IOC_SET_CLIENT_TYPE);
	if (NULL == session)
		return -EINVAL;

	switch (cmd) {
	case COMPAT_VPU_IOC_SET_CLIENT_TYPE: {
		int secure_mode;

		session->type = (enum VPU_CLIENT_TYPE)arg;
		secure_mode = (session->type & 0xffff0000) >> 16;
		session->type = session->type & 0xffff;
		atomic_set(&pservice->secure_mode, secure_mode);
		vpu_debug(DEBUG_IOCTL, "compat set client type %d\n",
			  session->type);
	} break;
	case COMPAT_VPU_IOC_GET_HW_FUSE_STATUS: {
		struct compat_vpu_request req;

		vpu_debug(DEBUG_IOCTL, "compat get hw status %d\n",
			  session->type);
		if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
				   sizeof(struct compat_vpu_request))) {
			vpu_err("error: compat get hw status copy_from_user failed\n");
			return -EFAULT;
		} else {
			void *config = (session->type != VPU_ENC) ?
				       ((void *)&pservice->dec_config) :
				       ((void *)&pservice->enc_config);
			size_t size = (session->type != VPU_ENC) ?
				      (sizeof(struct vpu_dec_config)) :
				      (sizeof(struct vpu_enc_config));

			if (copy_to_user(compat_ptr((compat_uptr_t)req.req),
					 config, size)) {
				vpu_err("error: compat get hw status copy_to_user failed type %d\n",
					session->type);
				return -EFAULT;
			}
		}
	} break;
	case COMPAT_VPU_IOC_SET_REG: {
		struct compat_vpu_request req;
		struct vpu_reg *reg;

		if (atomic_read(&pservice->secure_mode) == 1) {
			vpu_service_power_on(pservice);
			pservice->wait_secure_isr = &(session->wait);
			if (!pservice->secure_isr &&
			    !pservice->secure_irq_status) {
				enable_irq(data->irq_dec);
			}
		} else {
			vpu_debug(DEBUG_IOCTL, "compat set reg type %d\n",
				  session->type);
			if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
					   sizeof(struct compat_vpu_request))) {
				vpu_err("compat set_reg copy_from_user failed\n");
				return -EFAULT;
			}
			reg = reg_init(data, session,
				       compat_ptr((compat_uptr_t)req.req),
				       req.size);
			if (NULL == reg) {
				return -EFAULT;
			} else {
				mutex_lock(&pservice->lock);
				try_set_reg(data);
				mutex_unlock(&pservice->lock);
			}
		}
	} break;
	case COMPAT_VPU_IOC_GET_REG: {
		struct compat_vpu_request req;
		struct vpu_reg *reg;
		int ret;

		if (atomic_read(&pservice->secure_mode) == 1) {
			ret = wait_event_timeout(session->wait,
						 pservice->secure_isr,
						 VPU_TIMEOUT_DELAY);
			if (ret < 0) {
				pr_info("warning: secure wait timeout\n");
				ret = 0;
			}
			pservice->secure_isr = false;
		} else {
			vpu_debug(DEBUG_IOCTL, "compat get reg type %d\n",
				  session->type);
			if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
					   sizeof(struct compat_vpu_request))) {
				vpu_err("compat get reg copy_from_user failed\n");
				return -EFAULT;
			}

			ret = wait_event_timeout(session->wait,
						 !list_empty(&session->done),
						 VPU_TIMEOUT_DELAY);

			if (!list_empty(&session->done)) {
				if (ret < 0)
					vpu_err("warning: pid %d wait task error ret %d\n",
						session->pid, ret);
				ret = 0;
			} else {
				if (unlikely(ret < 0)) {
					vpu_err("error: pid %d wait task ret %d\n",
						session->pid, ret);
				} else if (ret == 0) {
					vpu_err("error: pid %d wait %d task done timeout\n",
						session->pid,
					atomic_read(&session->task_running));
					ret = -ETIMEDOUT;
				}
			}

			if (ret < 0) {
				int task_running = atomic_read(&session->task_running);

				mutex_lock(&pservice->lock);
				vpu_service_dump(pservice);
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running,
						   &pservice->total_running);
					pr_err("%d task is running but not return, reset hardware...",
					       task_running);
					vpu_reset(data);
					pr_err("done\n");
				}
				vpu_service_session_clear(data, session);
				mutex_unlock(&pservice->lock);
				return ret;
			}

			mutex_lock(&pservice->lock);
			reg = list_entry(session->done.next,
					 struct vpu_reg, session_link);
			return_reg(data, reg,
				   compat_ptr((compat_uptr_t)req.req));
			mutex_unlock(&pservice->lock);
		}
	} break;
	case COMPAT_VPU_IOC_PROBE_IOMMU_STATUS: {
		int iommu_enable = 1;

		vpu_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_PROBE_IOMMU_STATUS\n");

		if (copy_to_user(compat_ptr((compat_uptr_t)arg),
				 &iommu_enable, sizeof(int))) {
			vpu_err("error: VPU_IOC_PROBE_IOMMU_STATUS copy_to_user failed\n");
			return -EFAULT;
		}
	} break;
	case COMPAT_VPU_IOC_SET_DRIVER_DATA: {
		u32 val;

		if (copy_from_user(&val,
				   compat_ptr((compat_uptr_t)arg),
				   sizeof(int))) {
			vpu_err("error: COMPAT_VPU_IOC_SET_DRIVER_DATA copy_from_user failed\n");
			return -EFAULT;
		}
		if (pservice->grf)
			regmap_write(pservice->grf, 0x5d8, val);
		else if (pservice->grf_base)
			writel(val, pservice->grf_base + (0x5d8 >> 2));
	} break;
	default: {
		vpu_err("error: unknow vpu service ioctl cmd %x\n", cmd);
	} break;
	}
	vpu_debug_leave();
	return 0;
}
#endif

static int vpu_service_check_hw(struct vpu_subdev_data *data)
{
	int ret = -EINVAL, i = 0;
	u32 hw_id = readl_relaxed(data->regs);

	hw_id = (hw_id >> 16) & 0xFFFF;
	pr_info("checking hw id %x\n", hw_id);
	data->hw_info = NULL;
	for (i = 0; i < ARRAY_SIZE(vcodec_info_set); i++) {
		struct vcodec_info *info = &vcodec_info_set[i];

		if (hw_id == info->hw_id) {
			data->hw_id = info->hw_id;
			data->hw_info = info->hw_info;
			data->task_info = info->task_info;
			data->trans_info = info->trans_info;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int vpu_service_open(struct inode *inode, struct file *filp)
{
	struct vpu_subdev_data *data = container_of(
			inode->i_cdev, struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_session *session = kmalloc(sizeof(*session), GFP_KERNEL);

	vpu_debug_enter();

	if (NULL == session) {
		vpu_err("error: unable to allocate memory for vpu_session.");
		return -ENOMEM;
	}

	pservice->secure_isr = false;
	pservice->secure_irq_status = true;

	session->type	= VPU_TYPE_BUTT;
	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	mutex_lock(&pservice->lock);
	list_add_tail(&session->list_session, &pservice->session);
	filp->private_data = (void *)session;
	mutex_unlock(&pservice->lock);

	pr_debug("dev opened\n");
	vpu_debug_leave();
	return nonseekable_open(inode, filp);
}

static int vpu_service_release(struct inode *inode, struct file *filp)
{
	struct vpu_subdev_data *data = container_of(
			inode->i_cdev, struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	int task_running;
	struct vpu_session *session = (struct vpu_session *)filp->private_data;

	vpu_debug_enter();
	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		pr_err("error: session %d still has %d task running when closing\n",
		       session->pid, task_running);
		msleep(50);
	}
	wake_up(&session->wait);
	if (atomic_read(&pservice->secure_mode)) {
		atomic_set(&pservice->secure_mode, 0);
		if (!pservice->secure_irq_status) {
			enable_irq(data->irq_dec);
			pservice->secure_irq_status = true;
		}
	}
	mutex_lock(&pservice->lock);
	/* remove this filp from the asynchronusly notified filp's */
	list_del_init(&session->list_session);
	vpu_service_session_clear(data, session);
	kfree(session);
	filp->private_data = NULL;
	mutex_unlock(&pservice->lock);

	pr_debug("dev closed\n");
	vpu_debug_leave();
	return 0;
}

static const struct file_operations vpu_service_fops = {
	.unlocked_ioctl = vpu_service_ioctl,
	.open		= vpu_service_open,
	.release	= vpu_service_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = compat_vpu_service_ioctl,
#endif
};

static irqreturn_t vdpu_irq(int irq, void *dev_id);
static irqreturn_t vdpu_isr(int irq, void *dev_id);
static irqreturn_t vepu_irq(int irq, void *dev_id);
static irqreturn_t vepu_isr(int irq, void *dev_id);
static void get_hw_info(struct vpu_subdev_data *data);

#ifdef CONFIG_IOMMU_API
static inline void platform_set_sysmmu(struct device *iommu,
				       struct device *dev)
{
	dev->archdata.iommu = iommu;
}
#else
static inline void platform_set_sysmmu(struct device *iommu,
				       struct device *dev)
{
}
#endif

#if defined(CONFIG_VCODEC_MMU)

#define IOMMU_GET_BUS_ID(x)	(((x) >> 6) & 0x1f)

int vcodec_sysmmu_fault_hdl(struct device *dev,
			    enum rk_iommu_inttype itype,
			    unsigned long pgtable_base,
			    unsigned long fault_addr, unsigned int status)
{
	struct platform_device *pdev;
	struct vpu_service_info *pservice;
	struct vpu_subdev_data *data;

	vpu_debug_enter();

	if (dev == NULL) {
		pr_err("invalid NULL dev\n");
		return 0;
	}

	pdev = container_of(dev, struct platform_device, dev);
	if (pdev == NULL) {
		pr_err("invalid NULL platform_device\n");
		return 0;
	}

	data = platform_get_drvdata(pdev);
	if (data == NULL) {
		pr_err("invalid NULL vpu_subdev_data\n");
		return 0;
	}

	pservice = data->pservice;
	if (pservice == NULL) {
		pr_err("invalid NULL vpu_service_info\n");
		return 0;
	}

	if (pservice->reg_codec) {
		struct vpu_reg *reg = pservice->reg_codec;
		struct vcodec_mem_region *mem, *n;
		int i = 0;

		pr_err("vcodec, fault addr 0x%08lx\n", fault_addr);
		if (!list_empty(&reg->mem_region_list)) {
			list_for_each_entry_safe(mem, n, &reg->mem_region_list,
						 reg_lnk) {
				pr_err("vcodec, reg[%02u] mem region [%02d] 0x%lx %lx\n",
				       mem->reg_idx, i, mem->iova, mem->len);
				i++;
			}
		} else {
			pr_err("no memory region mapped\n");
		}

		if (reg->data) {
			struct vpu_subdev_data *data = reg->data;
			u32 *base = (u32 *)data->dec_dev.regs;
			u32 len = data->hw_info->dec_reg_num;

			pr_err("current errror register set:\n");

			for (i = 0; i < len; i++)
				pr_err("reg[%02d] %08x\n",
				       i, readl_relaxed(base + i));
		}

		set_bit(MMU_PAGEFAULT, &data->state);

		/*
		 * defeat workaround, invalidate address generated when rk322x
		 * vdec pre-fetch colmv data.
		 */
		if (IOMMU_GET_BUS_ID(status) == 2 && pservice->pf) {
			/* avoid another page fault occur after page fault */
			if (pservice->war_iova)
				rockchip_iovmm_unmap_iova(dev,
							  pservice->war_iova);

			/* get the page align iova */
			pservice->war_iova = round_down(fault_addr,
							IOMMU_PAGE_SIZE);

			rockchip_iovmm_map_iova(dev, pservice->war_iova,
						pservice->pf_pa,
						IOMMU_PAGE_SIZE);
		} else {
			vpu_reset(data);
		}
	}

	return 0;
}

static struct device *rockchip_get_sysmmu_dev(const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL;

	dn = of_find_compatible_node(NULL, NULL, compt);
	if (!dn) {
		pr_err("can't find device node %s \r\n", compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if (!pd) {
		pr_err("can't find platform device in device node %s\n", compt);
		return  NULL;
	}
	ret = &pd->dev;

	return ret;
}
#endif

/* special hw ops */
static void vcodec_power_on_default(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	if (pservice->aclk_vcodec)
		clk_prepare_enable(pservice->aclk_vcodec);
	if (pservice->hclk_vcodec)
		clk_prepare_enable(pservice->hclk_vcodec);
	if (pservice->clk_core)
		clk_prepare_enable(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_prepare_enable(pservice->clk_cabac);
	if (pservice->pd_video)
		clk_prepare_enable(pservice->pd_video);
#endif
}

static void vcodec_power_on_rk312x(struct vpu_service_info *pservice)
{
#define BIT_VCODEC_CLK_SEL	(1<<10)
	writel_relaxed(readl_relaxed(RK_GRF_VIRT + RK312X_GRF_SOC_CON1)
		| BIT_VCODEC_CLK_SEL | (BIT_VCODEC_CLK_SEL << 16),
		RK_GRF_VIRT + RK312X_GRF_SOC_CON1);

	vcodec_power_on_default(pservice);
}

static void vcodec_power_on_rk322x(struct vpu_service_info *pservice)
{
	unsigned long rate = 300*MHZ;

	vcodec_power_on_default(pservice);

	if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC)
		rate = 500*MHZ;

	if (pservice->aclk_vcodec)
		clk_set_rate(pservice->aclk_vcodec,  rate);
	if (pservice->clk_core)
		clk_set_rate(pservice->clk_core,  300*MHZ);
	if (pservice->clk_cabac)
		clk_set_rate(pservice->clk_cabac, 300*MHZ);
}

static void vcodec_power_on_rk322xh(struct vpu_service_info *pservice)
{
	dvfs_rkvdec_set_clk(500 * MHZ, 250 * MHZ, 400 * MHZ);
	vcodec_power_on_default(pservice);
}

static void vcodec_power_off_default(struct vpu_service_info *pservice)
{
	if (pservice->pd_video)
		clk_disable_unprepare(pservice->pd_video);
	if (pservice->hclk_vcodec)
		clk_disable_unprepare(pservice->hclk_vcodec);
	if (pservice->aclk_vcodec)
		clk_disable_unprepare(pservice->aclk_vcodec);
	if (pservice->clk_core)
		clk_disable_unprepare(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_disable_unprepare(pservice->clk_cabac);
}

static void vcodec_power_off_rk322x(struct vpu_service_info *pservice)
{
	/*
	 * rk322x do not have power domain
	 * we just lower the clock to minimize the power consumption
	 */
	if (pservice->aclk_vcodec)
		set_div_clk(pservice->aclk_vcodec, 32);
	if (pservice->clk_core)
		set_div_clk(pservice->clk_core, 32);
	if (pservice->clk_cabac)
		set_div_clk(pservice->clk_cabac, 32);
}

static void vcodec_power_off_rk322xh(struct vpu_service_info *pservice)
{
	vcodec_power_off_default(pservice);
	dvfs_rkvdec_set_clk(50 * MHZ, 50 * MHZ, 50 * MHZ);
}

static void vcodec_get_reg_freq_default(struct vpu_subdev_data *data,
					struct vpu_reg *reg)
{
	if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
		if (reg_check_fmt(reg) == VPU_DEC_FMT_H264) {
			if (reg_probe_width(reg) > 2560) {
				/*
				 * raise frequency for resolution larger
				 * than 1440p avc.
				 */
				reg->freq = VPU_FREQ_600M;
			}
		} else {
			if (reg_check_interlace(reg))
				reg->freq = VPU_FREQ_400M;
		}
	}
	if (data->hw_id == HEVC_ID) {
		if (reg_probe_hevc_y_stride(reg) > 60000)
			reg->freq = VPU_FREQ_400M;
	}
	if (reg->type == VPU_PP)
		reg->freq = VPU_FREQ_400M;
}

static void vcodec_set_freq_default(struct vpu_service_info *pservice,
				    struct vpu_reg *reg)
{
	enum VPU_FREQ curr = atomic_read(&pservice->freq_status);

	if (curr == reg->freq)
		return;

	atomic_set(&pservice->freq_status, reg->freq);
	switch (reg->freq) {
	case VPU_FREQ_200M: {
		clk_set_rate(pservice->aclk_vcodec, 200*MHZ);
	} break;
	case VPU_FREQ_266M: {
		clk_set_rate(pservice->aclk_vcodec, 266*MHZ);
	} break;
	case VPU_FREQ_300M: {
		clk_set_rate(pservice->aclk_vcodec, 300*MHZ);
	} break;
	case VPU_FREQ_400M: {
		clk_set_rate(pservice->aclk_vcodec, 400*MHZ);
	} break;
	case VPU_FREQ_500M: {
		clk_set_rate(pservice->aclk_vcodec, 500*MHZ);
	} break;
	case VPU_FREQ_600M: {
		clk_set_rate(pservice->aclk_vcodec, 600*MHZ);
	} break;
	default: {
		clk_set_rate(pservice->aclk_vcodec,
			     pservice->aclk_vcodec_default_rate);
		clk_set_rate(pservice->clk_core,
			     pservice->clk_core_default_rate);
		clk_set_rate(pservice->clk_cabac,
			     pservice->clk_cabac_default_rate);
		break;
	}
	}
}

static void vcodec_set_freq_rk322xh(struct vpu_service_info *pservice,
				    struct vpu_reg *reg)
{
	if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC) {
		if (reg->reg[1] & 0x00800000) {
			if (rkv_dec_get_fmt(reg->reg) == FMT_H264D) {
				rkvdec_set_clk(400 * MHZ, 250 * MHZ,
						400 * MHZ, -1);
			} else {
				rkvdec_set_clk(500 * MHZ, 250 * MHZ,
						400 * MHZ, -1);
			}
		} else {
			if (rkv_dec_get_fmt(reg->reg) == FMT_H264D) {
				rkvdec_set_clk(400 * MHZ, 300 * MHZ,
						400 * MHZ, -1);
			} else {
				rkvdec_set_clk(500 * MHZ, 300 * MHZ,
						400 * MHZ, -1);
			}
		}
	}
}

static void vcodec_set_freq_rk322x(struct vpu_service_info *pservice,
				   struct vpu_reg *reg)
{
	enum VPU_FREQ curr = atomic_read(&pservice->freq_status);

	if (curr == reg->freq)
		return;

	/*
	 * special process for rk322x
	 * rk322x rkvdec has more clocks to set
	 * vpu/vpu2 still only need to set aclk
	 */
	if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC) {
		clk_set_rate(pservice->clk_core,  300*MHZ);
		clk_set_rate(pservice->clk_cabac, 300*MHZ);
		clk_set_rate(pservice->aclk_vcodec, 500 * MHZ);
	} else {
		clk_set_rate(pservice->aclk_vcodec, 300 * MHZ);
	}
}

static void vcodec_set_freq_rk2928g(struct vpu_service_info *pservice,
				    struct vpu_reg *reg)
{
	enum VPU_FREQ curr = atomic_read(&pservice->freq_status);

	if (curr == reg->freq)
		return;

	clk_set_rate(pservice->aclk_vcodec, 400 * MHZ);
}

static void vcodec_reduce_freq_rk322x(struct vpu_service_info *pservice)
{
	if (list_empty(&pservice->running)) {
		unsigned long rate = clk_get_rate(pservice->aclk_vcodec);

		if (pservice->aclk_vcodec)
			set_div_clk(pservice->aclk_vcodec, 32);

		atomic_set(&pservice->freq_status, rate / 32);
	}
}

static void vcodec_reduce_freq_rk322xh(struct vpu_service_info *pservice)
{
	if (list_empty(&pservice->running))
		rkvdec_set_clk(100 * MHZ, 100 * MHZ, 100 * MHZ, -1);
}

static int vcodec_spec_init_rk322xh(struct vpu_service_info *pservice)
{
	int ret;
	size_t len;

	/*
	 * allocate memory using to create iommu pages for workaround the rkvdec
	 * defeat that generating a invalidate address when pre-fetch colmv data
	 * during decoding h265 with tile mode.
	 */
	pservice->pf = ion_alloc(pservice->ion_client, IOMMU_PAGE_SIZE,
				 IOMMU_PAGE_SIZE, ION_HEAP(ION_CMA_HEAP_ID), 0);
	if (IS_ERR_OR_NULL(pservice->pf)) {
		vpu_err("allocate colmv war buffer failed\n");
		pservice->pf = NULL;
	} else {
		ret = ion_phys(pservice->ion_client, pservice->pf,
			       &pservice->pf_pa, &len);
		if (ret < 0) {
			vpu_err("get colmv war buffer phy address failed\n");
			ion_free(pservice->ion_client, pservice->pf);
			pservice->pf = NULL;
		}
	}

	return 0;
}

static void vcodec_spec_config_rk322xh(struct vpu_subdev_data *data)
{
	u32 *dst = data->dec_dev.regs;
	u32 cfg;

	/*
	 * HW defeat workaround: VP9 power save optimization cause decoding
	 * corruption, disable optimization here.
	 * H265, H264 decoder on rk3328 rkvdec also found decoding corruption.
	 * disable rkvdec optimaztion all.
	 */
	cfg = readl(dst + 99);
	writel(cfg & (~(1 << 12)), dst + 99);
}

static struct vcodec_hw_ops hw_ops_default = {
	.power_on = vcodec_power_on_default,
	.power_off = vcodec_power_off_default,
	.get_freq = vcodec_get_reg_freq_default,
	.set_freq = vcodec_set_freq_default,
	.reduce_freq = NULL,
};

static struct vcodec_hw_ops hw_ops_rk322xh_rkvdec = {
	.power_on = vcodec_power_on_rk322xh,
	.power_off = vcodec_power_off_rk322xh,
	.get_freq = vcodec_get_reg_freq_default,
	.set_freq = vcodec_set_freq_rk322xh,
	.reduce_freq = vcodec_reduce_freq_rk322xh,
};

static struct vcodec_hw_var rk322xh_rkvdec_var = {
	.pmu_type = IDLE_REQ_VIDEO,
	.ops = &hw_ops_rk322xh_rkvdec,
	.init = vcodec_spec_init_rk322xh,
	.config = vcodec_spec_config_rk322xh,
};

static struct vcodec_hw_var rk322xh_vpucombo_var = {
	.pmu_type = IDLE_REQ_VPU,
	.ops = &hw_ops_default,
	.init = NULL,
	.config = NULL,
};

static void vcodec_set_hw_ops(struct vpu_service_info *pservice)
{
	if (pservice->hw_var) {
		pservice->hw_ops = pservice->hw_var->ops;
	} else {
		pservice->hw_ops = &hw_ops_default;
		if (cpu_is_rk322x()) {
			pservice->hw_ops->power_on = vcodec_power_on_rk322x;
			pservice->hw_ops->power_off = vcodec_power_off_rk322x;
			pservice->hw_ops->get_freq = NULL;
			pservice->hw_ops->set_freq = vcodec_set_freq_rk322x;
			pservice->hw_ops->reduce_freq =
				vcodec_reduce_freq_rk322x;
		} else if (soc_is_rk2928g()) {
			pservice->hw_ops->get_freq = NULL;
			pservice->hw_ops->set_freq = vcodec_set_freq_rk2928g;
		} else if (cpu_is_rk312x()) {
			pservice->hw_ops->power_on = vcodec_power_on_rk312x;
		}
	}
}

static int vcodec_subdev_probe(struct platform_device *pdev,
			       struct vpu_service_info *pservice)
{
	int ret = 0;
	struct resource *res = NULL;
	u32 ioaddr = 0;
	u8 *regs = NULL;
	struct vpu_hw_info *hw_info = NULL;
	struct device *dev = &pdev->dev;
	char *name = (char *)dev_name(dev);
	struct device_node *np = pdev->dev.of_node;
	struct vpu_subdev_data *data =
		devm_kzalloc(dev, sizeof(struct vpu_subdev_data), GFP_KERNEL);
	u32 iommu_en = 0;

	of_property_read_u32(np, "iommu_enabled", &iommu_en);

	pr_info("probe device %s\n", dev_name(dev));

	data->pservice = pservice;
	data->dev = dev;

	of_property_read_string(np, "name", (const char **)&name);
	of_property_read_u32(np, "dev_mode", (u32 *)&data->mode);

	if (pservice->reg_base == 0) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		data->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(data->regs)) {
			ret = PTR_ERR(data->regs);
			goto err;
		}
		ioaddr = res->start;
	} else {
		data->regs = pservice->reg_base;
		ioaddr = pservice->ioaddr;
	}

	if (pservice->hw_ops->power_on)
		pservice->hw_ops->power_on(pservice);

	clear_bit(MMU_ACTIVATED, &data->state);
	vcodec_enter_mode(data);
	ret = vpu_service_check_hw(data);
	if (ret < 0) {
		vpu_err("error: hw info check faild\n");
		goto err;
	}

	hw_info = data->hw_info;
	regs = (u8 *)data->regs;

	if (hw_info->dec_reg_num) {
		data->dec_dev.iosize = hw_info->dec_io_size;
		data->dec_dev.regs = (u32 *)(regs + hw_info->dec_offset);
	}

	if (hw_info->enc_reg_num) {
		data->enc_dev.iosize = hw_info->enc_io_size;
		data->enc_dev.regs = (u32 *)(regs + hw_info->enc_offset);
	}

	data->reg_size = max(hw_info->dec_io_size, hw_info->enc_io_size);

	data->irq_enc = platform_get_irq_byname(pdev, "irq_enc");
	if (data->irq_enc > 0) {
		ret = devm_request_threaded_irq(dev, data->irq_enc,
						vepu_irq, vepu_isr,
						IRQF_SHARED, dev_name(dev),
						(void *)data);
		if (ret) {
			dev_err(dev, "error: can't request vepu irq %d\n",
				data->irq_enc);
			goto err;
		}
	}
	data->irq_dec = platform_get_irq_byname(pdev, "irq_dec");
	if (data->irq_dec > 0) {
		ret = devm_request_threaded_irq(dev, data->irq_dec,
						vdpu_irq, vdpu_isr,
						IRQF_SHARED, dev_name(dev),
						(void *)data);
		if (ret) {
			dev_err(dev, "error: can't request vdpu irq %d\n",
				data->irq_dec);
			goto err;
		}
	}
	atomic_set(&data->dec_dev.irq_count_codec, 0);
	atomic_set(&data->dec_dev.irq_count_pp, 0);
	atomic_set(&data->enc_dev.irq_count_codec, 0);
	atomic_set(&data->enc_dev.irq_count_pp, 0);

#if defined(CONFIG_VCODEC_MMU)
	if (iommu_en) {
		char mmu_dev_dts_name[40];

		if (data->mode == VCODEC_RUNNING_MODE_HEVC)
			sprintf(mmu_dev_dts_name,
				HEVC_IOMMU_COMPATIBLE_NAME);
		else if (data->mode == VCODEC_RUNNING_MODE_VPU)
			sprintf(mmu_dev_dts_name,
				VPU_IOMMU_COMPATIBLE_NAME);
		else if (data->mode == VCODEC_RUNNING_MODE_RKVDEC)
			sprintf(mmu_dev_dts_name, VDEC_IOMMU_COMPATIBLE_NAME);
		else
			sprintf(mmu_dev_dts_name,
				HEVC_IOMMU_COMPATIBLE_NAME);

		data->mmu_dev =
			rockchip_get_sysmmu_dev(mmu_dev_dts_name);

		if (data->mmu_dev)
			platform_set_sysmmu(data->mmu_dev, dev);

		rockchip_iovmm_set_fault_handler(dev, vcodec_sysmmu_fault_hdl);
	}
#endif

	get_hw_info(data);
	pservice->auto_freq = true;

	if (pservice->hw_ops->power_off)
		pservice->hw_ops->power_off(pservice);

	vcodec_exit_mode(data);
	/* create device node */
	ret = alloc_chrdev_region(&data->dev_t, 0, 1, name);
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		goto err;
	}

	cdev_init(&data->cdev, &vpu_service_fops);

	data->cdev.owner = THIS_MODULE;
	data->cdev.ops = &vpu_service_fops;

	ret = cdev_add(&data->cdev, data->dev_t, 1);

	if (ret) {
		dev_err(dev, "add dev_t failed\n");
		goto err;
	}

	data->cls = class_create(THIS_MODULE, name);

	if (IS_ERR(data->cls)) {
		ret = PTR_ERR(data->cls);
		dev_err(dev, "class_create err:%d\n", ret);
		goto err;
	}

	data->child_dev = device_create(data->cls, dev,
		data->dev_t, NULL, name);

	platform_set_drvdata(pdev, data);

	INIT_LIST_HEAD(&data->lnk_service);
	list_add_tail(&data->lnk_service, &pservice->subdev_list);

#ifdef CONFIG_DEBUG_FS
	data->debugfs_dir = vcodec_debugfs_create_device_dir(name, parent);
	if (!IS_ERR_OR_NULL(data->debugfs_dir))
		data->debugfs_file_regs =
			debugfs_create_file("regs", 0664, data->debugfs_dir,
					    data, &debug_vcodec_fops);
	else
		vpu_err("create debugfs dir %s failed\n", name);
#endif
	return 0;
err:
	if (pservice->hw_ops->power_off)
		pservice->hw_ops->power_off(pservice);
	if (data->child_dev) {
		device_destroy(data->cls, data->dev_t);
		cdev_del(&data->cdev);
		unregister_chrdev_region(data->dev_t, 1);
	}

	if (data->cls)
		class_destroy(data->cls);
	return -1;
}

static void vcodec_subdev_remove(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;

	mutex_lock(&pservice->lock);
	cancel_delayed_work_sync(&pservice->power_off_work);
	if (pservice->hw_ops->power_off)
		pservice->hw_ops->power_off(pservice);
	mutex_unlock(&pservice->lock);

	device_destroy(data->cls, data->dev_t);
	class_destroy(data->cls);
	cdev_del(&data->cdev);
	unregister_chrdev_region(data->dev_t, 1);

#ifdef CONFIG_DEBUG_FS
	if (!IS_ERR_OR_NULL(data->debugfs_dir))
		debugfs_remove_recursive(data->debugfs_dir);
#endif
}

static void vcodec_read_property(struct device_node *np,
				 struct vpu_service_info *pservice)
{
	pservice->mode_bit = 0;
	pservice->mode_ctrl = 0;
	pservice->subcnt = 0;
	pservice->grf_base = NULL;

	of_property_read_u32(np, "subcnt", &pservice->subcnt);

	if (pservice->subcnt > 1) {
		of_property_read_u32(np, "mode_bit", &pservice->mode_bit);
		of_property_read_u32(np, "mode_ctrl", &pservice->mode_ctrl);
	}
#ifdef CONFIG_MFD_SYSCON
	pservice->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR_OR_NULL(pservice->grf)) {
		pservice->grf = NULL;
#ifdef CONFIG_ARM
		pservice->grf_base = RK_GRF_VIRT;
#else
		vpu_err("can't find vpu grf property\n");
		return;
#endif
	}
#else
#ifdef CONFIG_ARM
	pservice->grf_base = RK_GRF_VIRT;
#else
	vpu_err("can't find vpu grf property\n");
	return;
#endif
#endif

#ifdef CONFIG_RESET_CONTROLLER
	pservice->rst_a = devm_reset_control_get(pservice->dev, "video_a");
	pservice->rst_h = devm_reset_control_get(pservice->dev, "video_h");
	pservice->rst_niu_a = devm_reset_control_get(pservice->dev, "niu_a");
	pservice->rst_niu_h = devm_reset_control_get(pservice->dev, "niu_h");
	pservice->rst_core = devm_reset_control_get(pservice->dev, "video");
	pservice->rst_cabac = devm_reset_control_get(pservice->dev, "cabac");

	if (IS_ERR_OR_NULL(pservice->rst_a)) {
		pr_warn("No aclk reset resource define\n");
		pservice->rst_a = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_h)) {
		pr_warn("No hclk reset resource define\n");
		pservice->rst_h = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_niu_a)) {
		pr_warn("No noc aclk reset resource define\n");
		pservice->rst_niu_a = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_niu_h)) {
		pr_warn("No noc hclk reset resource define\n");
		pservice->rst_niu_h = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_core)) {
		pr_warn("No core reset resource define\n");
		pservice->rst_core = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_cabac)) {
		pr_warn("No cabac reset resource define\n");
		pservice->rst_cabac = NULL;
	}
#endif

	of_property_read_string(np, "name", (const char **)&pservice->name);
}

#if defined(CONFIG_OF)
static const struct of_device_id vcodec_service_dt_ids[] = {
	{
		.compatible = "rockchip,rk322xh-rkvdec",
		.data = &rk322xh_rkvdec_var,
	},
	{
		.compatible = "rockchip,rk322xh-vpu-combo",
		.data = &rk322xh_vpucombo_var,
	},
	{.compatible = "vpu_service",},
	{.compatible = "rockchip,hevc_service",},
	{.compatible = "rockchip,vpu_combo",},
	{.compatible = "rockchip,rkvdec",},
	{},
};
#endif

static void vcodec_init_drvdata(struct vpu_service_info *pservice)
{
	const struct of_device_id *match = NULL;

	pservice->dev_id = VCODEC_DEVICE_ID_VPU;
	pservice->curr_mode = -1;

	wake_lock_init(&pservice->wake_lock, WAKE_LOCK_SUSPEND, "vpu");
	INIT_LIST_HEAD(&pservice->waiting);
	INIT_LIST_HEAD(&pservice->running);
	mutex_init(&pservice->lock);

	INIT_LIST_HEAD(&pservice->done);
	INIT_LIST_HEAD(&pservice->session);
	INIT_LIST_HEAD(&pservice->subdev_list);

	pservice->reg_pproc	= NULL;
	atomic_set(&pservice->total_running, 0);
	atomic_set(&pservice->enabled,       0);
	atomic_set(&pservice->power_on_cnt,  0);
	atomic_set(&pservice->power_off_cnt, 0);
	atomic_set(&pservice->reset_request, 0);

	INIT_DELAYED_WORK(&pservice->power_off_work, vpu_power_off_work);
	pservice->last.tv64 = 0;

	match = of_match_node(vcodec_service_dt_ids, pservice->dev->of_node);
	if (match)
		pservice->hw_var = match->data;

	pservice->ion_client = rockchip_ion_client_create("vpu");
	if (IS_ERR(pservice->ion_client)) {
		vpu_err("failed to create ion client for vcodec ret %ld\n",
			PTR_ERR(pservice->ion_client));
	} else {
		vpu_debug(DEBUG_IOMMU, "vcodec ion client create success!\n");
	}

	vcodec_set_hw_ops(pservice);

	if (pservice->hw_var && pservice->hw_var->init)
		pservice->hw_var->init(pservice);
}

static int rkvdec_dvfs_notifier_call(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	static int thermal_en;
	struct dvfs_node *dvfs_node;
	int temp, delta_temp = 0;

	dvfs_node = container_of(nb, struct dvfs_node, dvfs_nb);
	if (!dvfs_node->temp_limit_enable)
		return NOTIFY_OK;

	temp = rockchip_tsadc_get_temp(dvfs_node->tsadc_ch, 0);
	/* debounce */
	delta_temp = (dvfs_node->old_temp > temp) ? (dvfs_node->old_temp - temp) :
			(temp - dvfs_node->old_temp);
	if (delta_temp <= 1)
		return NOTIFY_OK;

	if ((temp >= dvfs_node->target_temp) && !thermal_en) {
		thermal_en = 1;
		rkvdec_set_clk(0, 0, 0, thermal_en);
	} else if ((temp < dvfs_node->target_temp) && thermal_en) {
		thermal_en = 0;
		rkvdec_set_clk(0, 0, 0, thermal_en);
	}

	return NOTIFY_OK;
}

static int vcodec_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct vpu_service_info *pservice =
		devm_kzalloc(dev, sizeof(struct vpu_service_info), GFP_KERNEL);
	struct dvfs_node *clk_rkvdec_dvfs_node;

	pservice->dev = dev;

	vcodec_read_property(np, pservice);
	vcodec_init_drvdata(pservice);

	if (strncmp(pservice->name, "hevc_service", 12) == 0)
		pservice->dev_id = VCODEC_DEVICE_ID_HEVC;
	else if (strncmp(pservice->name, "vpu_service", 11) == 0)
		pservice->dev_id = VCODEC_DEVICE_ID_VPU;
	else if (strncmp(pservice->name, "rkvdec", 6) == 0)
		pservice->dev_id = VCODEC_DEVICE_ID_RKVDEC;
	else
		pservice->dev_id = VCODEC_DEVICE_ID_COMBO;

	if (0 > vpu_get_clk(pservice))
		goto err;

	if (of_property_read_bool(np, "reg")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

		pservice->reg_base = devm_ioremap_resource(pservice->dev, res);
		if (IS_ERR(pservice->reg_base)) {
			vpu_err("ioremap registers base failed\n");
			ret = PTR_ERR(pservice->reg_base);
			goto err;
		}
		pservice->ioaddr = res->start;
	} else {
		pservice->reg_base = 0;
	}

	if (of_property_read_bool(np, "subcnt")) {
		for (i = 0; i < pservice->subcnt; i++) {
			struct device_node *sub_np;
			struct platform_device *sub_pdev;

			sub_np = of_parse_phandle(np, "rockchip,sub", i);
			sub_pdev = of_find_device_by_node(sub_np);

			vcodec_subdev_probe(sub_pdev, pservice);
		}
	} else {
		vcodec_subdev_probe(pdev, pservice);
	}

	if (of_device_is_compatible(np, "rockchip,rk322xh-rkvdec")) {
		pservice->cru =
			syscon_regmap_lookup_by_compatible("rockchip,rk322xh-cru");
		if (!IS_ERR(pservice->cru)) {
			pservice->p_cpll = clk_get(NULL, "clk_cpll");
			pservice->p_gpll = clk_get(NULL, "clk_gpll");
			pservice->cpll_rate = clk_get_rate(pservice->p_cpll);
			pservice->gpll_rate = clk_get_rate(pservice->p_gpll);
		}
		vpu_dvfs = pservice;

		clk_rkvdec_dvfs_node = clk_get_dvfs_node("aclk_rkvdec");
		if (clk_rkvdec_dvfs_node) {
			clk_enable_dvfs(clk_rkvdec_dvfs_node);
			register_dvfs_notifier_callback(clk_rkvdec_dvfs_node,
							rkvdec_dvfs_notifier_call);
		}
	}

	pr_info("init success\n");

	return 0;

err:
	pr_info("init failed\n");

	vpu_put_clk(pservice);
	wake_lock_destroy(&pservice->wake_lock);

	return ret;
}

static int vcodec_remove(struct platform_device *pdev)
{
	struct vpu_subdev_data *data = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(data->pservice->pf))
		ion_free(data->pservice->ion_client, data->pservice->pf);

	vcodec_subdev_remove(data);
	return 0;
}

static struct platform_driver vcodec_driver = {
	.probe = vcodec_probe,
	.remove = vcodec_remove,
	.driver = {
		.name = "vcodec",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(vcodec_service_dt_ids),
#endif
	},
};

static void get_hw_info(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_dec_config *dec = &pservice->dec_config;
	struct vpu_enc_config *enc = &pservice->enc_config;

	if (cpu_is_rk2928() || cpu_is_rk3036() ||
	    cpu_is_rk30xx() || cpu_is_rk312x() ||
	    cpu_is_rk3188() || cpu_is_rk322x())
		dec->max_dec_pic_width = 1920;
	else
		dec->max_dec_pic_width = 4096;

	if (data->mode == VCODEC_RUNNING_MODE_VPU) {
		dec->h264_support = 3;
		dec->jpeg_support = 1;
		dec->mpeg4_support = 2;
		dec->vc1_support = 3;
		dec->mpeg2_support = 1;
		dec->pp_support = 1;
		dec->sorenson_support = 1;
		dec->ref_buf_support = 3;
		dec->vp6_support = 1;
		dec->vp7_support = 1;
		dec->vp8_support = 1;
		dec->avs_support = 1;
		dec->jpeg_ext_support = 0;
		dec->custom_mpeg4_support = 1;
		dec->reserve = 0;
		dec->mvc_support = 1;

		if (data->enc_dev.iosize != 0) {
			u32 config_reg = readl_relaxed(data->enc_dev.regs + 63);

			enc->max_encoded_width = config_reg & ((1 << 11) - 1);
			enc->h264_enabled = 1;
			enc->mpeg4_enabled = (config_reg >> 26) & 1;
			enc->jpeg_enabled = 1;
			enc->vs_enabled = (config_reg >> 24) & 1;
			enc->rgb_enabled = (config_reg >> 28) & 1;
			enc->reg_size = data->reg_size;
			enc->reserv[0] = 0;
			enc->reserv[1] = 0;
		}

		pservice->auto_freq = true;
		vpu_debug(DEBUG_EXTRA_INFO, "vpu_service set to auto frequency mode\n");
		atomic_set(&pservice->freq_status, VPU_FREQ_BUT);

		pservice->bug_dec_addr = cpu_is_rk30xx();
	} else if (data->mode == VCODEC_RUNNING_MODE_RKVDEC) {
		pservice->auto_freq = true;
		atomic_set(&pservice->freq_status, VPU_FREQ_BUT);
	} else {
		/* disable frequency switch in hevc.*/
		pservice->auto_freq = false;
	}
}

static bool check_irq_err(struct vpu_task_info *task, u32 irq_status)
{
	vpu_debug(DEBUG_IRQ_CHECK, "task %s status %08x mask %08x\n",
		  task->name, irq_status, task->error_mask);

	return (task->error_mask & irq_status) ? true : false;
}

static irqreturn_t vdpu_irq(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = NULL;
	struct vpu_device *dev = &data->dec_dev;
	u32 hw_id = data->hw_info->hw_id;
	u32 raw_status;
	u32 dec_status;

	/* this interrupt can be cleared here, no need in security zone */
	if (atomic_read(&pservice->secure_mode)) {
		disable_irq_nosync(data->irq_dec);
		pservice->secure_isr = true;
		pservice->secure_irq_status = false;
		wake_up(pservice->wait_secure_isr);
		return IRQ_WAKE_THREAD;
	}
	task = &data->task_info[TASK_DEC];

	raw_status = readl_relaxed(dev->regs + task->reg_irq);
	dec_status = raw_status;

	vpu_debug(DEBUG_TASK_INFO, "vdpu_irq reg %d status %x mask: irq %x ready %x error %0x\n",
		  task->reg_irq, dec_status,
		  task->irq_mask, task->ready_mask, task->error_mask);

	if (dec_status & task->irq_mask) {
		time_record(task, 1);
		vpu_debug(DEBUG_IRQ_STATUS, "vdpu_irq dec status %08x\n",
			  dec_status);

		writel_relaxed(0, dev->regs + task->reg_irq);

		/* set clock gating to save power */
		writel(task->gating_mask, dev->regs + task->reg_en);

		if (check_irq_err(task, dec_status)) {
			atomic_add(1, &pservice->reset_request);

			if (unlikely(debug & DEBUG_DUMP_ERR_REG))
				dump_reg(dev->regs, dev->iosize >> 2);
		}

		atomic_add(1, &dev->irq_count_codec);
		time_diff(task);
		pservice->irq_status = raw_status;
	}

	task = &data->task_info[TASK_PP];
	if (hw_id != HEVC_ID && hw_id != RKV_DEC_ID) {
		u32 pp_status = readl_relaxed(dev->regs + task->irq_mask);

		if (pp_status & task->irq_mask) {
			time_record(task, 1);
			vpu_debug(DEBUG_IRQ_STATUS, "vdpu_irq pp status %08x\n",
				  pp_status);

			if (check_irq_err(task, dec_status)) {
				atomic_add(1, &pservice->reset_request);

				if (unlikely(debug & DEBUG_DUMP_ERR_REG))
					dump_reg(dev->regs, dev->iosize >> 2);
			}

			/* clear pp IRQ */
			writel_relaxed(pp_status & (~task->reg_irq),
				       dev->regs + task->irq_mask);
			atomic_add(1, &dev->irq_count_pp);
			time_diff(task);
		}
	}

	if (atomic_read(&dev->irq_count_pp) ||
	    atomic_read(&dev->irq_count_codec))
		return IRQ_WAKE_THREAD;
	else
		return IRQ_NONE;
}

static irqreturn_t vdpu_isr(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_device *dev = &data->dec_dev;

	if (atomic_read(&pservice->secure_mode))
		return IRQ_HANDLED;
	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (pservice->reg_codec == NULL) {
			vpu_err("error: dec isr with no task waiting\n");
		} else {
#if defined(CONFIG_VCODEC_MMU)
			if (test_bit(MMU_PAGEFAULT, &data->state) &&
			    pservice->war_iova) {
				rockchip_iovmm_unmap_iova(pservice->dev,
							  pservice->war_iova);
				pservice->war_iova = 0;
				clear_bit(MMU_PAGEFAULT, &data->state);
			}
#endif
			reg_from_run_to_done(data, pservice->reg_codec);
			/* avoid vpu timeout and can't recover problem */
			VDPU_SOFT_RESET(data->regs);
		}
	}

	if (atomic_read(&dev->irq_count_pp)) {
		atomic_sub(1, &dev->irq_count_pp);
		if (pservice->reg_pproc == NULL)
			vpu_err("error: pp isr with no task waiting\n");
		else
			reg_from_run_to_done(data, pservice->reg_pproc);
	}
	try_set_reg(data);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static irqreturn_t vepu_irq(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = &data->task_info[TASK_ENC];
	struct vpu_device *dev = &data->enc_dev;
	u32 irq_status;

	irq_status = readl_relaxed(dev->regs + task->reg_irq);

	vpu_debug(DEBUG_TASK_INFO, "vepu_irq reg %d status %x mask: irq %x ready %x error %0x\n",
		  task->reg_irq, irq_status,
		  task->irq_mask, task->ready_mask, task->error_mask);

	vpu_debug(DEBUG_IRQ_STATUS, "vepu_irq enc status %08x\n", irq_status);

	if (likely(irq_status & task->irq_mask)) {
		time_record(task, 1);

		if (check_irq_err(task, irq_status)) {
			atomic_add(1, &pservice->reset_request);

			if (unlikely(debug & DEBUG_DUMP_ERR_REG))
				dump_reg(dev->regs, dev->iosize >> 2);
		}

		/* clear enc IRQ */
		writel_relaxed(irq_status & (~task->irq_mask),
			       dev->regs + task->reg_irq);

		atomic_add(1, &dev->irq_count_codec);
		time_diff(task);
	}

	pservice->irq_status = irq_status;

	if (atomic_read(&dev->irq_count_codec))
		return IRQ_WAKE_THREAD;
	else
		return IRQ_NONE;
}

static irqreturn_t vepu_isr(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_device *dev = &data->enc_dev;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == pservice->reg_codec)
			vpu_err("error: enc isr with no task waiting\n");
		else
			reg_from_run_to_done(data, pservice->reg_codec);
	}
	try_set_reg(data);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static int __init vcodec_service_init(void)
{
	int ret = platform_driver_register(&vcodec_driver);

	if (ret) {
		vpu_err("Platform device register failed (%d).\n", ret);
		return ret;
	}

#ifdef CONFIG_DEBUG_FS
	vcodec_debugfs_init();
#endif

	return ret;
}

static void __exit vcodec_service_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	vcodec_debugfs_exit();
#endif

	platform_driver_unregister(&vcodec_driver);
}

module_init(vcodec_service_init);
module_exit(vcodec_service_exit);
MODULE_LICENSE("Proprietary");

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static int vcodec_debugfs_init(void)
{
	parent = debugfs_create_dir("vcodec", NULL);
	if (!parent)
		return -1;

	return 0;
}

static void vcodec_debugfs_exit(void)
{
	debugfs_remove(parent);
}

static struct dentry *vcodec_debugfs_create_device_dir(
		char *dirname, struct dentry *parent)
{
	return debugfs_create_dir(dirname, parent);
}

static int debug_vcodec_show(struct seq_file *s, void *unused)
{
	struct vpu_subdev_data *data = s->private;
	struct vpu_service_info *pservice = data->pservice;
	unsigned int i, n;
	struct vpu_reg *reg, *reg_tmp;
	struct vpu_session *session, *session_tmp;

	mutex_lock(&pservice->lock);
	vpu_service_power_on(pservice);
	if (data->hw_info->hw_id != HEVC_ID) {
		seq_puts(s, "\nENC Registers:\n");
		n = data->enc_dev.iosize >> 2;

		for (i = 0; i < n; i++)
			seq_printf(s, "\tswreg%d = %08X\n", i,
				   readl_relaxed(data->enc_dev.regs + i));
	}

	seq_puts(s, "\nDEC Registers:\n");

	n = data->dec_dev.iosize >> 2;
	for (i = 0; i < n; i++)
		seq_printf(s, "\tswreg%d = %08X\n", i,
			   readl_relaxed(data->dec_dev.regs + i));

	seq_puts(s, "\nvpu service status:\n");

	list_for_each_entry_safe(session, session_tmp,
				 &pservice->session, list_session) {
		seq_printf(s, "session pid %d type %d:\n",
			   session->pid, session->type);

		list_for_each_entry_safe(reg, reg_tmp,
					 &session->waiting, session_link) {
			seq_printf(s, "waiting register set %p\n", reg);
		}
		list_for_each_entry_safe(reg, reg_tmp,
					 &session->running, session_link) {
			seq_printf(s, "running register set %p\n", reg);
		}
		list_for_each_entry_safe(reg, reg_tmp,
					 &session->done, session_link) {
			seq_printf(s, "done    register set %p\n", reg);
		}
	}

	seq_printf(s, "\npower counter: on %d off %d\n",
		   atomic_read(&pservice->power_on_cnt),
		   atomic_read(&pservice->power_off_cnt));

	mutex_unlock(&pservice->lock);
	vpu_service_power_off(pservice);

	return 0;
}

static int debug_vcodec_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_vcodec_show, inode->i_private);
}

#endif

