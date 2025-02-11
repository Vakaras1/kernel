#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "../../../arch/arm/mach-rockchip/efuse.h"
#include "rockchip_hdmiv2.h"
#include "rockchip_hdmiv2_hw.h"
#include "../rockchip-hdmi-cec.h"

static const struct phy_mpll_config_tab PHY_MPLL_TABLE[] = {
/*	tmdsclk = (pixclk / ref_cntrl ) * (fbdiv2 * fbdiv1) / nctrl / tmdsmhl
 *	opmode: 0:HDMI1.4	1:HDMI2.0
 *
 *	|pixclock|	tmdsclock|pixrepet|colordepth|prepdiv|tmdsmhl|opmode|
 *		fbdiv2|fbdiv1|ref_cntrl|nctrl|propctrl|intctrl|gmpctrl|
 */
	{27000000,	27000000,	0,	8,	0,	0,	0,
		2,	3,	0,	3,	3,	0,	0},
	{27000000,	27000000,	1,	8,	0,	0,	0,
		2,	3,	0,	3,	3,	0,	0},
	{27000000,	33750000,	0,	10,	1,	0,	0,
		5,	1,	0,	3,	3,	0,	0},
	{27000000,	33750000,	1,	10,	1,	0,	0,
		5,	1,	0,	3,	3,	0,	0},
	{27000000,	40500000,	0,	12,	2,	0,	0,
		3,	3,	0,	3,	3,	0,	0},
	{27000000,	54000000,	0,	16,	3,	0,	0,
		2,	3,	0,	2,	5,	0,	1},
	{59400000,	59400000,	0,	8,	0,	0,	0,
		1,	3,	0,	2,	5,	0,	1},
	{59400000,	74250000,	0,	10,	1,	0,	0,
		5,	0,	0,	2,	5,	0,	1},
	{59400000,	89100000,	0,	12,	2,	0,	0,
		2,	2,	0,	2,	5,	0,	1},
	{59400000,	118800000,	0,	16,	3,	0,	0,
		1,	3,	0,	1,	7,	0,	2},
	{65000000,	65000000,	0,	8,	0,	0,	0,
		1,	3,	0,	2,	5,	0,	1},
	{74250000,      74250000,	0,      8,      0,      0,      0,
		4,      3,      3,      2,      7,      0,      3},
	{74250000,	92812500,	0,	10,	1,	0,	0,
		5,	0,	1,	1,	7,	0,	2},
	{74250000,	111375000,	0,	12,	2,	0,	0,
		1,	2,	0,	1,	7,	0,	2},
	{74250000,	148500000,	0,	16,	3,	0,	0,
		1,	3,	0,	1,	7,	0,	2},
	{83500000,	83500000,	0,	8,	0,	0,	0,
		1,	3,	0,	2,	5,	0,	1},
	{85500000,	85500000,	0,	8,	0,	0,	0,
		1,	3,	0,	2,	5,	0,	1},
	{106500000,	106500000,	0,	8,	0,	0,	0,
		1,	1,	0,	1,	7,	0,	2},
	{108000000,	108000000,	0,	8,	0,	0,	0,
		1,	1,	0,	1,	7,	0,	2},
	{146250000,	146250000,	0,	8,	0,	0,	0,
		1,	1,	0,	1,	7,	0,	2},
	{148500000,	74250000,	0,	8,	0,	0,	0,
		1,	1,	1,	1,	0,	0,	3},
	{148500000,	148500000,	0,	8,	0,	0,	0,
		1,	1,	0,	1,	0,	0,	3},
	{148500000,	185625000,	0,	10,	1,	0,	0,
		5,	0,	3,	0,	7,	0,	3},
	{148500000,	222750000,	0,	12,	2,	0,	0,
		1,	2,	1,	0,	7,	0,	3},
	{148500000,	297000000,	0,	16,	3,	0,	0,
		1,	1,	0,	0,	7,	0,	3},
	{297000000,	148500000,	0,	8,	0,	0,	0,
		1,	0,	1,	0,	0,	0,	3},
	{297000000,	297000000,	0,	8,	0,	0,	0,
		1,	0,	0,	0,	0,	0,	3},
	{297000000,	371250000,	0,	10,	1,	3,	1,
		5,	1,	3,	1,	7,	0,	3},
	{297000000,	445500000,	0,	12,	2,	3,	1,
		1,	2,	0,	1,	7,	0,	3},
	{297000000,	594000000,	0,	16,	3,	3,	1,
		1,	3,	1,	0,	0,	0,	3},
	{594000000,	297000000,	0,	8,	0,	0,	0,
		1,	0,	1,	0,	0,	0,	3},
	{594000000,	371250000,	0,	10,	1,	3,	1,
		5,	0,	3,	1,	7,	0,	3},
	{594000000,	445500000,	0,	12,	2,	3,	1,
		1,	2,	1,	1,	7,	0,	3},
	{594000000,	594000000,	0,	16,	3,	3,	1,
		1,	3,	3,	0,	0,	0,	3},
	{594000000,	594000000,	0,	8,	0,	3,	1,
		1,	3,	3,	0,	0,	0,	3},
};

static const struct ext_pll_config_tab EXT_PLL_TABLE[] = {
	{27000000,	27000000,	8,	1,	90,	3,	2,
		2,	10,	3,	3,	4,	0,	1,	40,
		8,	0xE8FBA7},
	{27000000,	33750000,	10,	1,	90,	1,	3,
		3,	10,	3,	3,	4,	0,	1,	40,
		8,	0xE8FBA7},
	{59400000,	59400000,	8,	1,	99,	3,	1,
		1,	1,	3,	3,	4,	0,	1,	40,
		8,	0xE6AE6B},
	{59400000,	74250000,	10,	1,	99,	0,	3,
		3,	1,	3,	3,	4,	0,	1,	40,
		8,	0xE6AE6B},
	{74250000,	74250000,	8,	1,	99,	1,	2,
		2,	1,	2,	3,	4,	0,	1,	40,
		8,	0xE6AE6B},
	{74250000,	92812500,	10,	4,	495,	1,	2,
		2,	1,	3,	3,	4,	0,	2,	40,
		4,	0x816817},
	{148500000,	148500000,	8,	1,	99,	1,	1,
		1,	1,	2,	2,	2,	0,	2,	40,
		4,	0xE6AE6B},
	{148500000,	185625000,	10,	4,	495,	0,	2,
		2,	1,	3,	2,	2,	0,	4,	40,
		2,	0x816817},
	{297000000,	297000000,	8,	1,	99,	0,	1,
		1,	1,	0,	2,	2,	0,	4,	40,
		2,	0xE6AE6B},
	{297000000,	371250000,	10,	4,	495,	1,	2,
		0,	1,	3,	1,	1,	0,	8,	40,
		1,	0x816817},
	{594000000,	297000000,	8,	1,	99,	0,	1,
		1,	1,	0,	2,	1,	0,	4,	40,
		2,	0xE6AE6B},
	{594000000,	371250000,	10,	4,	495,	1,	2,
		0,	1,	3,	1,	1,	1,	8,	40,
		1,	0x816817},
	{594000000,	594000000,	8,	1,	99,	0,	2,
		0,	1,	0,	1,	1,	0,	8,	40,
		1,	0xE6AE6B},
};

static const struct ext_pll_config_tab RK322XH_V1_PLL_TABLE[] = {
	{27000000,	27000000,	8,	1,	90,	3,	2,
		2,	10,	3,	3,	4,	0,	1,	80,
		8,	0xE8FBA7},
	{27000000,	33750000,	10,	1,	90,	1,	3,
		3,	10,	3,	3,	4,	0,	1,	80,
		8,	0xE8FBA7},
	{59400000,	59400000,	8,	1,	99,	3,	1,
		1,	1,	3,	3,	4,	0,	18,	80,
		8,	0xE6AE6B},
	{59400000,	74250000,	10,	1,	99,	0,	3,
		3,	1,	3,	3,	4,	0,	18,	80,
		8,	0xE6AE6B},
	{74250000,	74250000,	8,	1,	99,	1,	2,
		2,	1,	2,	3,	4,	0,	18,	80,
		8,	0xE6AE6B},
};

/* ddc i2c master reset */
static void rockchip_hdmiv2_i2cm_reset(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SOFTRSTZ,
		     m_I2CM_SOFTRST, v_I2CM_SOFTRST(0));
	usleep_range(90, 100);
}

/*set read/write offset,set read/write mode*/
static void rockchip_hdmiv2_i2cm_write_request(struct hdmi_dev *hdmi_dev,
					       u8 offset, u8 data)
{
	hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset);
	hdmi_writel(hdmi_dev, I2CM_DATAO, data);
	hdmi_msk_reg(hdmi_dev, I2CM_OPERATION, m_I2CM_WR, v_I2CM_WR(1));
}

static void rockchip_hdmiv2_i2cm_read_request(struct hdmi_dev *hdmi_dev,
					      u8 offset)
{
	hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset);
	hdmi_msk_reg(hdmi_dev, I2CM_OPERATION, m_I2CM_RD, v_I2CM_RD(1));
}

static void rockchip_hdmiv2_i2cm_write_data(struct hdmi_dev *hdmi_dev,
					    u8 data, u8 offset)
{
	u8 interrupt = 0;
	int trytime = 2;
	int i;

	while (trytime-- > 0) {
		rockchip_hdmiv2_i2cm_write_request(hdmi_dev, offset, data);
		i = 20;
		while (i--) {
			usleep_range(900, 1000);
			interrupt = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
			if (interrupt)
				hdmi_writel(hdmi_dev,
					    IH_I2CM_STAT0, interrupt);

			if (interrupt & (m_SCDC_READREQ |
					 m_I2CM_DONE | m_I2CM_ERROR))
				break;
		}

		if (interrupt & m_I2CM_DONE) {
			dev_dbg(hdmi_dev->hdmi->dev,
				"[%s] write offset %02x data %02x success\n",
				__func__, offset, data);
			trytime = 0;
		} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
			dev_err(hdmi_dev->hdmi->dev,
				"[%s] write data error\n", __func__);
			rockchip_hdmiv2_i2cm_reset(hdmi_dev);
		}
	}
}

static int rockchip_hdmiv2_i2cm_read_data(struct hdmi_dev *hdmi_dev, u8 offset)
{
	u8 interrupt = 0, val;
	int trytime = 2;
	int i = 20;

	while (trytime-- > 0) {
		rockchip_hdmiv2_i2cm_read_request(hdmi_dev, offset);
		while (i--) {
			usleep_range(900, 1000);
			interrupt = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
			if (interrupt)
				hdmi_writel(hdmi_dev, IH_I2CM_STAT0, interrupt);

			if (interrupt & (m_SCDC_READREQ |
				m_I2CM_DONE | m_I2CM_ERROR))
				break;
		}

		if (interrupt & m_I2CM_DONE) {
			val = hdmi_readl(hdmi_dev, I2CM_DATAI);
			trytime = 0;
		} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
			pr_err("[%s] read data error\n", __func__);
			rockchip_hdmiv2_i2cm_reset(hdmi_dev);
		}
	}
	return val;
}

static void rockchip_hdmiv2_i2cm_mask_int(struct hdmi_dev *hdmi_dev, int mask)
{
	if (!mask) {
		hdmi_msk_reg(hdmi_dev, I2CM_INT,
			     m_I2CM_DONE_MASK, v_I2CM_DONE_MASK(0));
		hdmi_msk_reg(hdmi_dev, I2CM_CTLINT,
			     m_I2CM_NACK_MASK | m_I2CM_ARB_MASK,
			     v_I2CM_NACK_MASK(0) | v_I2CM_ARB_MASK(0));
	} else {
		hdmi_msk_reg(hdmi_dev, I2CM_INT,
			     m_I2CM_DONE_MASK, v_I2CM_DONE_MASK(1));
		hdmi_msk_reg(hdmi_dev, I2CM_CTLINT,
			     m_I2CM_NACK_MASK | m_I2CM_ARB_MASK,
			     v_I2CM_NACK_MASK(1) | v_I2CM_ARB_MASK(1));
	}
}

#define I2C_DIV_FACTOR 1000000
static u16 i2c_count(u16 sfrclock, u16 sclmintime)
{
	unsigned long tmp_scl_period = 0;

	if (((sfrclock * sclmintime) % I2C_DIV_FACTOR) != 0)
		tmp_scl_period = (unsigned long)((sfrclock * sclmintime) +
				(I2C_DIV_FACTOR - ((sfrclock * sclmintime) %
				I2C_DIV_FACTOR))) / I2C_DIV_FACTOR;
	else
		tmp_scl_period = (unsigned long)(sfrclock * sclmintime) /
				I2C_DIV_FACTOR;

	return (u16)(tmp_scl_period);
}

#define EDID_I2C_MIN_SS_SCL_HIGH_TIME	9625
#define EDID_I2C_MIN_SS_SCL_LOW_TIME	10000

static void rockchip_hdmiv2_i2cm_clk_init(struct hdmi_dev *hdmi_dev)
{
	int value;

	/* Set DDC I2C CLK which divided from DDC_CLK. */
	value = i2c_count(24000, EDID_I2C_MIN_SS_SCL_HIGH_TIME);
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_HCNT_0_ADDR,
		    value & 0xff);
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_HCNT_1_ADDR,
		    (value >> 8) & 0xff);
	value = i2c_count(24000, EDID_I2C_MIN_SS_SCL_LOW_TIME);
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_LCNT_0_ADDR,
		    value & 0xff);
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_LCNT_1_ADDR,
		    (value >> 8) & 0xff);
	hdmi_msk_reg(hdmi_dev, I2CM_DIV, m_I2CM_FAST_STD_MODE,
		     v_I2CM_FAST_STD_MODE(STANDARD_MODE));
}

static int rockchip_hdmiv2_scdc_get_sink_version(struct hdmi_dev *hdmi_dev)
{
	return rockchip_hdmiv2_i2cm_read_data(hdmi_dev, SCDC_SINK_VER);
}

static void rockchip_hdmiv2_scdc_set_source_version(struct hdmi_dev *hdmi_dev,
						    u8 version)
{
	rockchip_hdmiv2_i2cm_write_data(hdmi_dev, version, SCDC_SOURCE_VER);
}

static void rockchip_hdmiv2_scdc_read_request(struct hdmi_dev *hdmi_dev,
					      int enable)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SCDC_READ_UPDATE,
		     m_I2CM_READ_REQ_EN, v_I2CM_READ_REQ_EN(enable));
	rockchip_hdmiv2_i2cm_write_data(hdmi_dev, enable, SCDC_CONFIG_0);
}

#ifdef HDMI_20_SCDC
static void rockchip_hdmiv2_scdc_update_read(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SCDC_READ_UPDATE,
		     m_I2CM_READ_UPDATE, v_I2CM_READ_UPDATE(1));
}

static int rockchip_hdmiv2_scdc_get_scambling_status(struct hdmi_dev *hdmi_dev)
{
	int val;

	val = rockchip_hdmiv2_i2cm_read_data(hdmi_dev, SCDC_SCRAMBLER_STAT);
	return val;
}

static void rockchip_hdmiv2_scdc_enable_polling(struct hdmi_dev *hdmi_dev,
						int enable)
{
	rockchip_hdmiv2_scdc_read_request(hdmi_dev, enable);
	hdmi_msk_reg(hdmi_dev, I2CM_SCDC_READ_UPDATE,
		     m_I2CM_UPRD_VSYNC_EN, v_I2CM_UPRD_VSYNC_EN(enable));
}

static int rockchip_hdmiv2_scdc_get_status_reg0(struct hdmi_dev *hdmi_dev)
{
	rockchip_hdmiv2_scdc_read_request(hdmi_dev, 1);
	rockchip_hdmiv2_scdc_update_read(hdmi_dev);
	return hdmi_readl(hdmi_dev, I2CM_SCDC_UPDATE0);
}

static int rockchip_hdmiv2_scdc_get_status_reg1(struct hdmi_dev *hdmi_dev)
{
	rockchip_hdmiv2_scdc_read_request(hdmi_dev, 1);
	rockchip_hdmiv2_scdc_update_read(hdmi_dev);
	return hdmi_readl(hdmi_dev, I2CM_SCDC_UPDATE1);
}
#endif

static void rockchip_hdmiv2_scdc_init(struct hdmi_dev *hdmi_dev)
{
	rockchip_hdmiv2_i2cm_reset(hdmi_dev);
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 1);
	rockchip_hdmiv2_i2cm_clk_init(hdmi_dev);
	/* set scdc i2c addr */
	hdmi_writel(hdmi_dev, I2CM_SLAVE, DDC_I2C_SCDC_ADDR);
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 0);/*enable interrupt*/
}

static void rockchip_hdmiv2_scdc_set_tmds_rate(struct hdmi_dev *hdmi_dev)
{
	int stat;

	mutex_lock(&hdmi_dev->ddc_lock);
	rockchip_hdmiv2_scdc_init(hdmi_dev);
	stat = rockchip_hdmiv2_i2cm_read_data(hdmi_dev,
					      SCDC_TMDS_CONFIG);
	if (hdmi_dev->tmdsclk > 340000000)
		stat |= 2;
	else
		stat &= 0x1;
	rockchip_hdmiv2_i2cm_write_data(hdmi_dev,
					stat, SCDC_TMDS_CONFIG);
	mutex_unlock(&hdmi_dev->ddc_lock);
}

static int rockchip_hdmiv2_scrambling_enable(struct hdmi_dev *hdmi_dev,
					     int enable)
{
	HDMIDBG(2, "%s enable %d\n", __func__, enable);
	if (enable == 1) {
		/* Write on Rx the bit Scrambling_Enable, register 0x20 */
		rockchip_hdmiv2_i2cm_write_data(hdmi_dev, 1, SCDC_TMDS_CONFIG);
		/* TMDS software reset request */
		hdmi_msk_reg(hdmi_dev, MC_SWRSTZREQ,
			     m_TMDS_SWRST, v_TMDS_SWRST(0));
		/* Enable/Disable Scrambling */
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL,
			     m_FC_SCRAMBLE_EN, v_FC_SCRAMBLE_EN(1));
	} else {
		/* Enable/Disable Scrambling */
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL,
			     m_FC_SCRAMBLE_EN, v_FC_SCRAMBLE_EN(0));
		/* TMDS software reset request */
		hdmi_msk_reg(hdmi_dev, MC_SWRSTZREQ,
			     m_TMDS_SWRST, v_TMDS_SWRST(0));
		/* Write on Rx the bit Scrambling_Enable, register 0x20 */
		rockchip_hdmiv2_i2cm_write_data(hdmi_dev, 0, SCDC_TMDS_CONFIG);
	}
	return 0;
}

static const struct ext_pll_config_tab *get_phy_ext_tab(
		struct hdmi_dev *hdmi_dev)
{
	const struct ext_pll_config_tab *ext_pll[2];
	int i, j, table, size[2];

	if (!hdmi_dev || hdmi_dev->pixelclk == 0)
		return NULL;
	HDMIDBG(2, "%s pixClock %u tmdsclk %u colorDepth %d\n",
		__func__, (u32)hdmi_dev->pixelclk, hdmi_dev->tmdsclk,
		hdmi_dev->colordepth);
	ext_pll[1] = RK322XH_V1_PLL_TABLE;
	size[1] =  ARRAY_SIZE(RK322XH_V1_PLL_TABLE);
	ext_pll[0] = EXT_PLL_TABLE;
	size[0] = ARRAY_SIZE(EXT_PLL_TABLE);

	if (hdmi_dev->soctype == HDMI_SOC_RK322XH &&
	    rockchip_get_cpu_version())
		table = 1;
	else
		table = 0;
	for (j = table; j >= 0; j--) {
		for (i = 0; i < size[j]; i++) {
			if ((ext_pll[j][i].pix_clock == hdmi_dev->pixelclk) &&
			    (ext_pll[j][i].tmdsclock == hdmi_dev->tmdsclk) &&
			    (ext_pll[j][i].color_depth == hdmi_dev->colordepth))
				return &ext_pll[j][i];
		}
	}
	return NULL;
}

static const struct phy_mpll_config_tab *get_phy_mpll_tab(
		unsigned int pixclock, unsigned int tmdsclk,
		char pixrepet, char colordepth)
{
	int i;

	if (pixclock == 0)
		return NULL;
	HDMIDBG(2, "%s pixClock %u tmdsclk %u pixRepet %d colorDepth %d\n",
		__func__, pixclock, tmdsclk, pixrepet, colordepth);
	for (i = 0; i < ARRAY_SIZE(PHY_MPLL_TABLE); i++) {
		if ((PHY_MPLL_TABLE[i].pix_clock == pixclock) &&
		    (PHY_MPLL_TABLE[i].tmdsclock == tmdsclk) &&
		    (PHY_MPLL_TABLE[i].pix_repet == pixrepet) &&
		    (PHY_MPLL_TABLE[i].color_depth == colordepth))
			return &PHY_MPLL_TABLE[i];
	}
	return NULL;
}

static void rockchip_hdmiv2_powerdown(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, PHY_MASK, m_PHY_LOCK, v_PHY_LOCK(1));
	if (hdmi_dev->soctype == HDMI_SOC_RK322X ||
	    hdmi_dev->soctype == HDMI_SOC_RK322XH ||
	    hdmi_dev->soctype == HDMI_SOC_RV1108) {
		hdmi_msk_reg(hdmi_dev, PHY_CONF0,
			     m_TXPWRON_SIG | m_ENHPD_RXSENSE_SIG,
			     v_TXPWRON_SIG(0) | v_ENHPD_RXSENSE_SIG(0));
		if (hdmi_dev->soctype == HDMI_SOC_RK322X)
			regmap_write(hdmi_dev->grf_base,
				     RK322X_GRF_SOC_CON2,
				     RK322X_PLL_PDATA_DEN);
		else if (hdmi_dev->soctype == HDMI_SOC_RK322XH)
			regmap_write(hdmi_dev->grf_base,
				     RK322XH_GRF_SOC_CON3,
				     RK322XH_PLL_PDATA_DEN);
		else
			regmap_write(hdmi_dev->grf_base,
				     RV1108_GRF_SOC_CON4,
				     RV1108_PLL_PDATA_DEN);
		if (hdmi_dev->soctype == HDMI_SOC_RV1108 ||
		    hdmi_dev->soctype == HDMI_SOC_RK322XH) {
			/* Disable PHY IRQ */
			rockchip_hdmiv2_write_phy(hdmi_dev, 0x05, 0);
			rockchip_hdmiv2_write_phy(hdmi_dev, 0x07, 0);
		}
	} else {
		hdmi_msk_reg(hdmi_dev, PHY_CONF0,
			     m_PDDQ_SIG | m_TXPWRON_SIG |
			     m_ENHPD_RXSENSE_SIG | m_SVSRET_SIG,
			     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0) |
			     v_ENHPD_RXSENSE_SIG(1) | v_SVSRET_SIG(0));
	}
	if (hdmi_dev->hdmi->property->feature & SUPPORT_CEC_WAKEUP)
		hdmi_writel(hdmi_dev, MC_CLKDIS, 0x5f);
	else
		hdmi_writel(hdmi_dev, MC_CLKDIS, 0x7f);
}

int rockchip_hdmiv2_write_phy(struct hdmi_dev *hdmi_dev,
			      int reg_addr, int val)
{
	int trytime = 2, i = 0, op_status = 0;

	if (hdmi_dev->phybase) {
		writel_relaxed(val, hdmi_dev->phybase + (reg_addr) * 0x04);
		return 0;
	}
	while (trytime--) {
		hdmi_writel(hdmi_dev, PHY_I2CM_ADDRESS, reg_addr);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAO_1, (val >> 8) & 0xff);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAO_0, val & 0xff);
		hdmi_writel(hdmi_dev, PHY_I2CM_OPERATION, m_PHY_I2CM_WRITE);

		i = 20;
		while (i--) {
			usleep_range(900, 1000);
			op_status = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
			if (op_status)
				hdmi_writel(hdmi_dev,
					    IH_I2CMPHY_STAT0,
					    op_status);

			if (op_status & (m_I2CMPHY_DONE | m_I2CMPHY_ERR))
				break;
		}

		if (!(op_status & m_I2CMPHY_DONE))
			dev_err(hdmi_dev->hdmi->dev,
				"[%s] operation error,trytime=%d\n",
				__func__, trytime);
		else
			return 0;
		msleep(100);
	}

	return -1;
}

int rockchip_hdmiv2_read_phy(struct hdmi_dev *hdmi_dev,
			     int reg_addr)
{
	int trytime = 2, i = 0, op_status = 0;
	int val = 0;

	if (hdmi_dev->phybase) {
		val = readl_relaxed(hdmi_dev->phybase + (reg_addr) * 0x04);
		val = val & 0xff;
		return val;
	}

	while (trytime--) {
		hdmi_writel(hdmi_dev, PHY_I2CM_ADDRESS, reg_addr);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAI_1, 0x00);
		hdmi_writel(hdmi_dev, PHY_I2CM_DATAI_0, 0x00);
		hdmi_writel(hdmi_dev, PHY_I2CM_OPERATION, m_PHY_I2CM_READ);

		i = 20;
		while (i--) {
			usleep_range(900, 1000);
			op_status = hdmi_readl(hdmi_dev, IH_I2CMPHY_STAT0);
			if (op_status)
				hdmi_writel(hdmi_dev, IH_I2CMPHY_STAT0,
					    op_status);

			if (op_status & (m_I2CMPHY_DONE | m_I2CMPHY_ERR))
				break;
		}

		if (!(op_status & m_I2CMPHY_DONE)) {
			pr_err("[%s] operation error,trytime=%d\n",
			       __func__, trytime);
		} else {
			val = hdmi_readl(hdmi_dev, PHY_I2CM_DATAI_1);
			val = (val & 0xff) << 8;
			val += (hdmi_readl(hdmi_dev, PHY_I2CM_DATAI_0) & 0xff);
			pr_debug("phy_reg0x%02x: 0x%04x",
				 reg_addr, val);
			return val;
		}
		msleep(100);
	}

	return -1;
}

#define PHY_TIMEOUT	10000
/* #define SRC_DCK	1 */

static int ext_phy1_config(struct hdmi_dev *hdmi_dev)
{
	u32 stat = 0, i = 0, temp;
	const struct ext_pll_config_tab *phy_ext = NULL;

	if (hdmi_dev->soctype == HDMI_SOC_RV1108)
		regmap_write(hdmi_dev->grf_base,
			     RV1108_GRF_SOC_CON4,
			     RV1108_PLL_POWER_DOWN |
			     RV1108_PLL_PDATA_DEN);
	else if (hdmi_dev->soctype == HDMI_SOC_RK322XH)
		regmap_write(hdmi_dev->grf_base,
			     RK322XH_GRF_SOC_CON3,
			     RK322XH_PLL_POWER_DOWN |
			     RK322XH_PLL_PDATA_DEN);
	if (hdmi_dev->tmdsclk_ratio_change &&
	    hdmi_dev->hdmi->edid.scdc_present == 1)
		rockchip_hdmiv2_scdc_set_tmds_rate(hdmi_dev);
#ifdef SRC_DCK
	if (hdmi_dev->dclk_phy)
		clk_set_rate(hdmi_dev->dclk_phy, hdmi_dev->pixelclk);
	rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_CONTROL, 0xc1);
#else
	rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_CONTROL, 0xc0);
	rockchip_hdmiv2_write_phy(hdmi_dev, 0xa0, 0x04);
#endif
	/* config the required PHY I2C register */
	phy_ext = get_phy_ext_tab(hdmi_dev);
	if (phy_ext) {
#ifndef SRC_DCK
		stat = (phy_ext->vco_div_5 & 1) << 1;
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_PLL_CTRL, stat);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_PLL_PRE_DIVIDER,
					  phy_ext->pll_nd);
		if (hdmi_dev->frac_en)
			stat = (((phy_ext->pll_nf - 1) >> 8) & 0x0f) | 0xc0;
		else
			stat = ((phy_ext->pll_nf >> 8) & 0x0f) | 0xf0;
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_PLL_SPM_CTRL, stat);
		if (hdmi_dev->frac_en)
			stat = (phy_ext->pll_nf - 1) & 0xff;
		else
			stat = phy_ext->pll_nf & 0xff;
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_PLL_FB_DIVIDER, stat);
		stat = (phy_ext->pclk_divider_a & EXT_PHY_PCLK_DIVIDERA_MASK) |
		       ((phy_ext->pclk_divider_b & 3) << 5);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_PCLK_DIVIDER1, stat);
		stat = (phy_ext->pclk_divider_d & EXT_PHY_PCLK_DIVIDERD_MASK) |
		       ((phy_ext->pclk_divider_c & 3) << 5);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_PCLK_DIVIDER2, stat);
		stat = ((phy_ext->tmsd_divider_a & 3) << 4) |
		       ((phy_ext->tmsd_divider_b & 3) << 2) |
		       (phy_ext->tmsd_divider_c & 3);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY1_TMDSCLK_DIVIDER, stat);
#endif
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  0xac,
					  (phy_ext->ppll_nf & 0xff));
		if (phy_ext->ppll_no == 1) {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  0xaa,
						  2);
			stat = (phy_ext->ppll_nf >> 8) | phy_ext->ppll_nd;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  0xab,
						  stat);
		} else {
			stat = ((phy_ext->ppll_no / 2) - 1);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  0xad,
						  stat);
			stat = (phy_ext->ppll_nf >> 8) | phy_ext->ppll_nd;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  0xab,
						  stat);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  0xaa,
						  0x0e);
		}
		if (hdmi_dev->frac_en) {
			stat = phy_ext->frac;
			rockchip_hdmiv2_write_phy(hdmi_dev, 0xd3,
						  stat & 0xff);
			rockchip_hdmiv2_write_phy(hdmi_dev, 0xd2,
						  (stat >> 8) & 0xff);
			rockchip_hdmiv2_write_phy(hdmi_dev, 0xd1,
						  (stat >> 16) & 0xff);
		} else {
			rockchip_hdmiv2_write_phy(hdmi_dev, 0xd3, 0);
			rockchip_hdmiv2_write_phy(hdmi_dev, 0xd2, 0);
			rockchip_hdmiv2_write_phy(hdmi_dev, 0xd1, 0);
		}
	} else {
		pr_err("%s no supported phy configuration.\n", __func__);
		return -1;
	}

	if (hdmi_dev->phy_table) {
		for (i = 0; i < hdmi_dev->phy_table_size; i++) {
			temp = hdmi_dev->phy_table[i].maxfreq;
			if (hdmi_dev->tmdsclk <= temp)
				break;
		}
	}

	if (i != hdmi_dev->phy_table_size && hdmi_dev->phy_table) {
		if (hdmi_dev->phy_table[i].pre_emphasis) {
			if (hdmi_dev->phy_table[i].pre_emphasis > 0x3f) {
				stat = 0;
				if (hdmi_dev->phy_table[i].pre_emphasis > 0x5f)
					temp = 3;
				else
					temp = 1;
			} else {
				stat = 7;
				if (hdmi_dev->phy_table[i].pre_emphasis > 0x1f)
					temp = 3;
				else
					temp = 1;
			}
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_PREEMPHASIS_MODE,
						  stat);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_PREEMPHASIS_CTRL,
						  (temp << 4) |
						  (temp << 2) |
						  temp);
			stat = hdmi_dev->phy_table[i].pre_emphasis % 0x20;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_PREEMPHASIS_D2,
						  stat);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_PREEMPHASIS_D1,
						  stat);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_PREEMPHASIS_D0,
						  stat);
		} else {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_PREEMPHASIS_CTRL,
						  0);
		}
		if (hdmi_dev->phy_table[i].slopeboost) {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_DELAY_PATH_EN,
						  0x3f);
			stat = hdmi_dev->phy_table[i].slopeboost % 0x10;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_DELAY_TIME1,
						  stat | 0xc0);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_DELAY_TIME2,
						  (stat << 4) | stat);
		} else {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY1_DELAY_PATH_EN,
						  0x00);
		}
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TMDS_CLK_LEVEL,
					  hdmi_dev->phy_table[i].clk_level);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TMDS_D2_LEVEL,
					  hdmi_dev->phy_table[i].data2_level);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TMDS_D1_LEVEL,
					  hdmi_dev->phy_table[i].data1_level);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TMDS_D0_LEVEL,
					  hdmi_dev->phy_table[i].data0_level);
	}
	/* bit[7:6] of reg c8/c9/ca/c8 is ESD detect threshold:
	 * 00 - 340mV
	 * 01 - 280mV
	 * 10 - 260mV
	 * 11 - 240mV
	 * default is 240mV, now we set it to 340mV
	 */
	rockchip_hdmiv2_write_phy(hdmi_dev, 0xc8, 0);
	rockchip_hdmiv2_write_phy(hdmi_dev, 0xc9, 0);
	rockchip_hdmiv2_write_phy(hdmi_dev, 0xca, 0);
	rockchip_hdmiv2_write_phy(hdmi_dev, 0xcb, 0);

	if (hdmi_dev->tmdsclk > 340000000) {
		/* Set termination resistor to 100ohm */
		stat = clk_get_rate(hdmi_dev->pclk_phy) / 100000;
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TERM_CAL_CTRL1,
					  ((stat >> 8) & 0xff) | 0x80);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TERM_CAL_CTRL2,
					  stat & 0xff);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TERM_RESIS_AUTO,
					  3 << 1);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TERM_CAL_CTRL1,
					  ((stat >> 8) & 0xff));
	} else if (hdmi_dev->tmdsclk > 165000000) {
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TERM_CAL_CTRL1,
					  0x81);
		/* clk termination resistor is 50ohm
		 * data termination resistor is 150ohm
		 */
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xc8, 0x30);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xc9, 0x10);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xca, 0x10);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xcb, 0x10);
	} else {
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY1_TERM_CAL_CTRL1,
					  0x81);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xc9, 0x10);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xca, 0x10);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xcb, 0x10);
	}
	if (hdmi_dev->soctype == HDMI_SOC_RV1108)
		regmap_write(hdmi_dev->grf_base,
			     RV1108_GRF_SOC_CON4,
			     RV1108_PLL_POWER_UP);
	else if (hdmi_dev->soctype == HDMI_SOC_RK322XH)
		regmap_write(hdmi_dev->grf_base,
			     RK322XH_GRF_SOC_CON3,
			     RK322XH_PLL_POWER_UP);
	if (hdmi_dev->tmdsclk_ratio_change)
		msleep(100);
	else
		usleep_range(900, 1000);
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_TXPWRON_SIG, v_TXPWRON_SIG(1));
	i = 0;
	while (i++ < PHY_TIMEOUT) {
		if ((i % 10) == 0) {
			stat = rockchip_hdmiv2_read_phy(hdmi_dev, 0xa9);
			temp = rockchip_hdmiv2_read_phy(hdmi_dev, 0xaf);
			if ((stat & 0x01) && (temp & 0x01))
				break;
			usleep_range(1000, 2000);
		}
	}
	if (!(stat & 0x01) || !(temp & 0x01)) {
		stat = hdmi_readl(hdmi_dev, MC_LOCKONCLOCK);
		dev_err(hdmi_dev->hdmi->dev,
			"PHY PLL not locked: PCLK_ON=%ld,TMDSCLK_ON=%ld\n",
			(stat & m_PCLK_ON) >> 6, (stat & m_TMDSCLK_ON) >> 5);
		return -1;
	}
	if (hdmi_dev->soctype == HDMI_SOC_RV1108)
		regmap_write(hdmi_dev->grf_base,
			     RV1108_GRF_SOC_CON4,
			     RV1108_PLL_PDATA_EN);
	else if (hdmi_dev->soctype == HDMI_SOC_RK322XH)
		regmap_write(hdmi_dev->grf_base,
			     RK322XH_GRF_SOC_CON3,
			     RK322XH_PLL_PDATA_EN);
	/* Enable PHY IRQ */
	rockchip_hdmiv2_write_phy(hdmi_dev, 0x05, 0x22);
	rockchip_hdmiv2_write_phy(hdmi_dev, 0x07, 0x22);
	return 0;
}

static int ext_phy_config(struct hdmi_dev *hdmi_dev)
{
	int stat = 0, i = 0, temp;
	const struct ext_pll_config_tab *phy_ext = NULL;

	if (hdmi_dev->grf_base)
		regmap_write(hdmi_dev->grf_base,
			     RK322X_GRF_SOC_CON2,
			     RK322X_PLL_POWER_DOWN |
			     RK322X_PLL_PDATA_DEN);
	if (hdmi_dev->tmdsclk_ratio_change &&
	    hdmi_dev->hdmi->edid.scdc_present == 1)
		rockchip_hdmiv2_scdc_set_tmds_rate(hdmi_dev);

	/* config the required PHY I2C register */
	phy_ext = get_phy_ext_tab(hdmi_dev);
	if (phy_ext) {
		stat = ((phy_ext->pll_nf >> 1) & EXT_PHY_PLL_FB_BIT8_MASK) |
		       ((phy_ext->vco_div_5 & 1) << 5) |
		       (phy_ext->pll_nd & EXT_PHY_PLL_PRE_DIVIDER_MASK);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_PLL_PRE_DIVIDER, stat);
		stat = phy_ext->pll_nf & 0xff;
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_PLL_FB_DIVIDER, stat);
		stat = (phy_ext->pclk_divider_a & EXT_PHY_PCLK_DIVIDERA_MASK) |
		       ((phy_ext->pclk_divider_b & 3) << 5);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_PCLK_DIVIDER1, stat);
		stat = (phy_ext->pclk_divider_d & EXT_PHY_PCLK_DIVIDERD_MASK) |
		       ((phy_ext->pclk_divider_c & 3) << 5);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_PCLK_DIVIDER2, stat);
		stat = ((phy_ext->tmsd_divider_c & 3) << 4) |
		       ((phy_ext->tmsd_divider_a & 3) << 2) |
		       (phy_ext->tmsd_divider_b & 3);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_TMDSCLK_DIVIDER, stat);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_PPLL_FB_DIVIDER,
					  phy_ext->ppll_nf);

		if (phy_ext->ppll_no == 1) {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_POST_DIVIDER,
						  0);
			stat = 0x20 | phy_ext->ppll_nd;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_PRE_DIVIDER,
						  stat);
		} else if (hdmi_dev->tmdsclk == 27000000 &&
			   rockchip_get_hdmi_flag()) {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_POST_DIVIDER,
						  0x00);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_PRE_DIVIDER,
						  0xe1);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_FB_DIVIDER,
						  10);
		} else {
			stat = ((phy_ext->ppll_no / 2) - 1) << 4;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_POST_DIVIDER,
						  stat);
			stat = 0xe0 | phy_ext->ppll_nd;
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_PPLL_PRE_DIVIDER,
						  stat);
		}
	} else {
		pr_err("%s no supported phy configuration.\n", __func__);
		return -1;
	}

	if (hdmi_dev->phy_table) {
		for (i = 0; i < hdmi_dev->phy_table_size; i++) {
			temp = hdmi_dev->phy_table[i].maxfreq;
			if (hdmi_dev->tmdsclk <= temp)
				break;
		}
	}

	if (i != hdmi_dev->phy_table_size && hdmi_dev->phy_table) {
		if (hdmi_dev->phy_table[i].slopeboost) {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_SIGNAL_CTRL, 0xff);
			temp = hdmi_dev->phy_table[i].slopeboost - 1;
			stat = ((temp & 3) << 6) | ((temp & 3) << 4) |
			       ((temp & 3) << 2) | (temp & 3);
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_SLOPEBOOST, stat);
		} else {
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  EXT_PHY_SIGNAL_CTRL, 0x0f);
		}
		stat = ((hdmi_dev->phy_table[i].pre_emphasis & 3) << 4) |
		       ((hdmi_dev->phy_table[i].pre_emphasis & 3) << 2) |
		       (hdmi_dev->phy_table[i].pre_emphasis & 3);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_PREEMPHASIS, stat);
		stat = ((hdmi_dev->phy_table[i].clk_level & 0xf) << 4) |
		       (hdmi_dev->phy_table[i].data2_level & 0xf);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_LEVEL1, stat);
		stat = ((hdmi_dev->phy_table[i].data1_level & 0xf) << 4) |
		       (hdmi_dev->phy_table[i].data0_level & 0xf);
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_LEVEL2, stat);
	} else {
		rockchip_hdmiv2_write_phy(hdmi_dev,
					  EXT_PHY_SIGNAL_CTRL, 0x0f);
	}
	rockchip_hdmiv2_write_phy(hdmi_dev, 0xf3, 0x22);

	stat = clk_get_rate(hdmi_dev->pclk_phy) / 100000;
	rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_TERM_CAL,
				  ((stat >> 8) & 0xff) | 0x80);
	rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_TERM_CAL_DIV_L,
				  stat & 0xff);
	if (hdmi_dev->tmdsclk > 340000000) {
		stat = EXT_PHY_AUTO_R100_OHMS;
	} else if (hdmi_dev->tmdsclk > 200000000) {
		if (gpio_is_valid(hdmi_dev->io_pullup))
			stat = EXT_PHY_MANAUL_150_OHMS;
		else
			stat = EXT_PHY_AUTO_R50_OHMS;
	} else {
		stat = EXT_PHY_AUTO_ROPEN_CIRCUIT;
	}
	if (stat & EXT_PHY_TERM_CAL_EN_MASK) {
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xfc,
					  stat & 0x7f);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xfd,
					  stat & 0x7f);
		rockchip_hdmiv2_write_phy(hdmi_dev, 0xfe,
					  stat & 0x7f);
	} else {
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_TERM_RESIS_AUTO,
					  stat | 0x20);
		rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_TERM_CAL,
					  (stat >> 8) & 0xff);
	}
	if (hdmi_dev->tmdsclk > 200000000)
		stat = 0;
	else
		stat = 0x11;
	rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_PLL_BW, stat);
	rockchip_hdmiv2_write_phy(hdmi_dev, EXT_PHY_PPLL_BW, 0x27);
	if (hdmi_dev->grf_base)
		regmap_write(hdmi_dev->grf_base,
			     RK322X_GRF_SOC_CON2,
			     RK322X_PLL_POWER_UP);
	if (hdmi_dev->tmdsclk_ratio_change)
		msleep(100);
	else
		usleep_range(900, 1000);
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_TXPWRON_SIG, v_TXPWRON_SIG(1));
	i = 0;
	while (i++ < PHY_TIMEOUT) {
		if ((i % 10) == 0) {
			temp = EXT_PHY_PPLL_POST_DIVIDER;
			stat = rockchip_hdmiv2_read_phy(hdmi_dev, temp);
			if (stat & EXT_PHY_PPLL_LOCK_STATUS_MASK)
				break;
			usleep_range(1000, 2000);
		}
	}
	if ((stat & EXT_PHY_PPLL_LOCK_STATUS_MASK) == 0) {
		stat = hdmi_readl(hdmi_dev, MC_LOCKONCLOCK);
		dev_err(hdmi_dev->hdmi->dev,
			"PHY PLL not locked: PCLK_ON=%ld,TMDSCLK_ON=%ld\n",
			(stat & m_PCLK_ON) >> 6, (stat & m_TMDSCLK_ON) >> 5);
		return -1;
	}

	if (hdmi_dev->grf_base)
		regmap_write(hdmi_dev->grf_base,
			     RK322X_GRF_SOC_CON2,
			     RK322X_PLL_PDATA_EN);

	return 0;
}

static int rockchip_hdmiv2_config_phy(struct hdmi_dev *hdmi_dev)
{
	int stat = 0, i = 0;
	const struct phy_mpll_config_tab *phy_mpll = NULL;

	if (hdmi_dev->soctype == HDMI_SOC_RK322X)
		return ext_phy_config(hdmi_dev);
	else if (hdmi_dev->soctype == HDMI_SOC_RV1108 ||
		 hdmi_dev->soctype == HDMI_SOC_RK322XH)
		return ext_phy1_config(hdmi_dev);
	hdmi_msk_reg(hdmi_dev, PHY_I2CM_DIV,
		     m_PHY_I2CM_FAST_STD, v_PHY_I2CM_FAST_STD(0));
	hdmi_msk_reg(hdmi_dev, PHY_MASK, m_PHY_LOCK, v_PHY_LOCK(1));
	/* power off PHY */
	hdmi_msk_reg(hdmi_dev, PHY_CONF0,
		     m_PDDQ_SIG | m_TXPWRON_SIG | m_SVSRET_SIG,
		     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0) | v_SVSRET_SIG(1));

	if (hdmi_dev->tmdsclk_ratio_change &&
	    hdmi_dev->hdmi->edid.scdc_present == 1)
		rockchip_hdmiv2_scdc_set_tmds_rate(hdmi_dev);

	/* reset PHY */
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(1));
	usleep_range(1000, 2000);
	hdmi_writel(hdmi_dev, MC_PHYRSTZ, v_PHY_RSTZ(0));

	/* Set slave address as PHY GEN2 address */
	hdmi_writel(hdmi_dev, PHY_I2CM_SLAVE, PHY_GEN2_ADDR);

	/* config the required PHY I2C register */
	phy_mpll = get_phy_mpll_tab(hdmi_dev->pixelclk,
				    hdmi_dev->tmdsclk,
				    hdmi_dev->pixelrepeat - 1,
				    hdmi_dev->colordepth);
	if (phy_mpll) {
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_OPMODE_PLLCFG,
					  v_PREP_DIV(phy_mpll->prep_div) |
					  v_TMDS_CNTRL(
					  phy_mpll->tmdsmhl_cntrl) |
					  v_OPMODE(phy_mpll->opmode) |
					  v_FBDIV2_CNTRL(
					  phy_mpll->fbdiv2_cntrl) |
					  v_FBDIV1_CNTRL(
					  phy_mpll->fbdiv1_cntrl) |
					  v_REF_CNTRL(phy_mpll->ref_cntrl) |
					  v_MPLL_N_CNTRL(phy_mpll->n_cntrl));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_PLLCURRCTRL,
					  v_MPLL_PROP_CNTRL(
					  phy_mpll->prop_cntrl) |
					  v_MPLL_INT_CNTRL(
					  phy_mpll->int_cntrl));
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_PLLGMPCTRL,
					  v_MPLL_GMP_CNTRL(
					  phy_mpll->gmp_cntrl));
	}

	if (hdmi_dev->phy_table) {
		for (i = 0; i < hdmi_dev->phy_table_size; i++)
			if (hdmi_dev->tmdsclk <= hdmi_dev->phy_table[i].maxfreq)
				break;
	}
	if (i != hdmi_dev->phy_table_size && hdmi_dev->phy_table) {
		stat = v_OVERRIDE(1) | v_TX_SYMON(1) | v_CLK_SYMON(1) |
		       v_PREEMPHASIS(hdmi_dev->phy_table[i].pre_emphasis) |
		       v_SLOPEBOOST(hdmi_dev->phy_table[i].slopeboost);
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL, stat);

		stat = v_SUP_CLKLVL(hdmi_dev->phy_table[i].clk_level) |
		       v_SUP_TXLVL(hdmi_dev->phy_table[i].data0_level);
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_VLEVCTRL, stat);
	} else {
		pr_info("%s use default phy settings\n", __func__);
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_CLKSYMCTRL,
					  v_OVERRIDE(1) | v_SLOPEBOOST(0) |
					  v_TX_SYMON(1) | v_CLK_SYMON(1) |
					  v_PREEMPHASIS(0));
		if (hdmi_dev->tmdsclk > 340000000)
			rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_VLEVCTRL,
						  v_SUP_TXLVL(9) |
						  v_SUP_CLKLVL(17));
		else if (hdmi_dev->tmdsclk > 165000000)
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  PHYTX_VLEVCTRL,
						  v_SUP_TXLVL(14) |
						  v_SUP_CLKLVL(17));
		else
			rockchip_hdmiv2_write_phy(hdmi_dev,
						  PHYTX_VLEVCTRL,
						  v_SUP_TXLVL(18) |
						  v_SUP_CLKLVL(17));
	}

	if (hdmi_dev->tmdsclk > 340000000)
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
					  v_TX_TERM(R50_OHMS));
	else
		rockchip_hdmiv2_write_phy(hdmi_dev, PHYTX_TERM_RESIS,
					  v_TX_TERM(R100_OHMS));
	/* rockchip_hdmiv2_write_phy(hdmi_dev, 0x05, 0x8000); */
	if (hdmi_dev->tmdsclk_ratio_change)
		msleep(100);
	/* power on PHY */
	hdmi_writel(hdmi_dev, PHY_CONF0, 0x2e);

	/* check if the PHY PLL is locked */

	i = 0;
	while (i++ < PHY_TIMEOUT) {
		if ((i % 10) == 0) {
			stat = hdmi_readl(hdmi_dev, PHY_STAT0);
			if (stat & m_PHY_LOCK)
				break;
			usleep_range(1000, 2000);
		}
	}
	if ((stat & m_PHY_LOCK) == 0) {
		stat = hdmi_readl(hdmi_dev, MC_LOCKONCLOCK);
		dev_err(hdmi_dev->hdmi->dev,
			"PHY PLL not locked: PCLK_ON=%ld,TMDSCLK_ON=%ld\n",
			(stat & m_PCLK_ON) >> 6, (stat & m_TMDSCLK_ON) >> 5);
		return -1;
	}
	hdmi_msk_reg(hdmi_dev, PHY_MASK, m_PHY_LOCK, v_PHY_LOCK(0));
	return 0;
}

static int rockchip_hdmiv2_video_framecomposer(struct hdmi *hdmi_drv,
					       struct hdmi_video *vpara)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;
	int value, vsync_pol, hsync_pol, de_pol;
	struct hdmi_video_timing *timing = NULL;
	struct fb_videomode *mode = NULL;
	u32 sink_version, tmdsclk;

	vsync_pol = hdmi_drv->lcdc->cur_screen->pin_vsync;
	hsync_pol = hdmi_drv->lcdc->cur_screen->pin_hsync;
	de_pol = (hdmi_drv->lcdc->cur_screen->pin_den == 0) ? 1 : 0;

	hdmi_msk_reg(hdmi_dev, A_VIDPOLCFG,
		     m_DATAEN_POL | m_VSYNC_POL | m_HSYNC_POL,
		     v_DATAEN_POL(de_pol) |
		     v_VSYNC_POL(vsync_pol) |
		     v_HSYNC_POL(hsync_pol));

	timing = (struct hdmi_video_timing *)hdmi_vic2timing(vpara->vic);
	if (!timing) {
		dev_err(hdmi_drv->dev,
			"[%s] not found vic %d\n", __func__, vpara->vic);
		return -ENOENT;
	}
	mode = &timing->mode;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		tmdsclk = mode->pixclock / 2;
	else if (vpara->format_3d == HDMI_3D_FRAME_PACKING)
		tmdsclk = 2 * mode->pixclock;
	else
		tmdsclk = mode->pixclock;
	if (vpara->color_output != HDMI_COLOR_YCBCR422) {
		switch (vpara->color_output_depth) {
		case 10:
			tmdsclk += tmdsclk / 4;
			break;
		case 12:
			tmdsclk += tmdsclk / 2;
			break;
		case 16:
			tmdsclk += tmdsclk;
			break;
		case 8:
		default:
			break;
		}
	} else if (vpara->color_output_depth > 12) {
		/* YCbCr422 mode only support up to 12bit */
		vpara->color_output_depth = 12;
	}
	if ((tmdsclk > 594000000) ||
	    (tmdsclk > 340000000 &&
	     tmdsclk > hdmi_drv->edid.maxtmdsclock)) {
		if (vpara->format_3d == HDMI_3D_FRAME_PACKING) {
			pr_err("3d frame packing mode out of max tmdsclk\n");
			return -1;
		} else if (vpara->color_output == HDMI_COLOR_YCBCR444 &&
			   hdmi_drv->edid.ycbcr422) {
			pr_warn("out of max tmdsclk, down to YCbCr422");
			vpara->color_output = HDMI_COLOR_YCBCR422;
			tmdsclk = mode->pixclock;
		} else {
			pr_warn("out of max tmds clock, limit to 8bit\n");
			vpara->color_output_depth = 8;
			if (vpara->color_input == HDMI_COLOR_YCBCR420)
				tmdsclk = mode->pixclock / 2;
			else
				tmdsclk = mode->pixclock;
		}
	}

	if ((tmdsclk > 340000000) ||
	    (tmdsclk < 340000000 && hdmi_dev->tmdsclk > 340000000))
		hdmi_dev->tmdsclk_ratio_change = true;
	else
		hdmi_dev->tmdsclk_ratio_change = false;

	hdmi_dev->tmdsclk = tmdsclk;
	if (vpara->format_3d == HDMI_3D_FRAME_PACKING)
		hdmi_dev->pixelclk = 2 * mode->pixclock;
	else if (vpara->color_input == HDMI_COLOR_YCBCR420 &&
		 hdmi_dev->soctype == HDMI_SOC_RK3288)
		hdmi_dev->pixelclk = mode->pixclock / 2;
	else
		hdmi_dev->pixelclk = mode->pixclock;
	hdmi_dev->pixelrepeat = timing->pixelrepeat;
	/* hdmi_dev->colordepth is used for find pll config.
	 * For YCbCr422, tmdsclk is same on all color depth.
	 */
	if (vpara->color_output == HDMI_COLOR_YCBCR422)
		hdmi_dev->colordepth = 8;
	else
		hdmi_dev->colordepth = vpara->color_output_depth;
	pr_info("pixel clk is %lu tmds clk is %u\n",
		hdmi_dev->pixelclk, hdmi_dev->tmdsclk);
	/*
	 * Only refresh rate 24/30/60 and HD/UHD resolution
	 * need to be enabled frac mode.
	 */
	if (hdmi_drv->frac &&
	    (mode->refresh % 25) &&
	    mode->pixclock != 27000000)
		hdmi_dev->frac_en = 1;
	else
		hdmi_dev->frac_en = 0;
	/* Start/stop HDCP keepout window generation */
	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
		     m_FC_HDCP_KEEPOUT, v_FC_HDCP_KEEPOUT(1));
	if (hdmi_drv->edid.scdc_present == 1 && !hdmi_drv->uboot) {
		if (tmdsclk > 340000000 ||
		    hdmi_drv->edid.lte_340mcsc_scramble) {
			/* used for HDMI 2.0 TX */
			mutex_lock(&hdmi_dev->ddc_lock);
			rockchip_hdmiv2_scdc_init(hdmi_dev);
			sink_version =
			rockchip_hdmiv2_scdc_get_sink_version(hdmi_dev);
			pr_info("sink scdc version is %d\n", sink_version);
			sink_version = hdmi_drv->edid.hf_vsdb_version;
			rockchip_hdmiv2_scdc_set_source_version(hdmi_dev,
								sink_version);
			if (hdmi_drv->edid.rr_capable == 1)
				rockchip_hdmiv2_scdc_read_request(hdmi_dev, 1);
			rockchip_hdmiv2_scrambling_enable(hdmi_dev, 1);
			mutex_unlock(&hdmi_dev->ddc_lock);
		} else {
			mutex_lock(&hdmi_dev->ddc_lock);
			rockchip_hdmiv2_scdc_init(hdmi_dev);
			rockchip_hdmiv2_scrambling_enable(hdmi_dev, 0);
			mutex_unlock(&hdmi_dev->ddc_lock);
		}
	} else {
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL,
			     m_FC_SCRAMBLE_EN, v_FC_SCRAMBLE_EN(0));
	}

	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
		     m_FC_VSYNC_POL | m_FC_HSYNC_POL | m_FC_DE_POL |
		     m_FC_HDMI_DVI | m_FC_INTERLACE_MODE,
		     v_FC_VSYNC_POL(vsync_pol) | v_FC_HSYNC_POL(hsync_pol) |
		     v_FC_DE_POL(de_pol) | v_FC_HDMI_DVI(vpara->sink_hdmi) |
		     v_FC_INTERLACE_MODE(mode->vmode));
	if ((mode->vmode & FB_VMODE_INTERLACED) &&
	    vpara->format_3d != HDMI_3D_FRAME_PACKING)
		hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
			     m_FC_VBLANK, v_FC_VBLANK(1));
	else
		hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
			     m_FC_VBLANK, v_FC_VBLANK(0));

	value = mode->xres;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_INHACTIV1, v_FC_HACTIVE1(value >> 8));
	hdmi_writel(hdmi_dev, FC_INHACTIV0, (value & 0xff));

	if (vpara->format_3d == HDMI_3D_FRAME_PACKING) {
		if (mode->vmode == 0)
			value = 2 * mode->yres +
				mode->upper_margin +
				mode->lower_margin +
				mode->vsync_len;
		else
			value = 2 * mode->yres +
				3 * (mode->upper_margin +
				     mode->lower_margin +
				     mode->vsync_len) + 2;
	} else {
		value = mode->yres;
	}
	hdmi_writel(hdmi_dev, FC_INVACTIV1, v_FC_VACTIVE1(value >> 8));
	hdmi_writel(hdmi_dev, FC_INVACTIV0, (value & 0xff));

	value = mode->hsync_len + mode->left_margin + mode->right_margin;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_INHBLANK1, v_FC_HBLANK1(value >> 8));
	hdmi_writel(hdmi_dev, FC_INHBLANK0, (value & 0xff));

	value = mode->vsync_len + mode->upper_margin + mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_INVBLANK, (value & 0xff));

	value = mode->right_margin;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY1, v_FC_HSYNCINDEAY1(value >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY0, (value & 0xff));

	value = mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_VSYNCINDELAY, (value & 0xff));

	value = mode->hsync_len;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		value = value / 2;
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH1, v_FC_HSYNCWIDTH1(value >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH0, (value & 0xff));

	value = mode->vsync_len;
	hdmi_writel(hdmi_dev, FC_VSYNCINWIDTH, (value & 0xff));

	/* Set the control period minimum duration (min. of 12 pixel
	 * clock cycles, refer to HDMI 1.4b specification)
	 */
	hdmi_writel(hdmi_dev, FC_CTRLDUR, 12);
	hdmi_writel(hdmi_dev, FC_EXCTRLDUR, 32);

	/* spacing < 256^2 * config / tmdsClock, spacing <= 50ms
	 * worst case: tmdsClock == 25MHz => config <= 19
	 */
	hdmi_writel(hdmi_dev, FC_EXCTRLSPAC,
		    (hdmi_dev->tmdsclk / 1000) * 50 / (256 * 512));
#if 0
	if (!hdmi_drv->uboot)
		hdmi_msk_reg(hdmi_dev, MC_SWRSTZREQ,
			     m_TMDS_SWRST, v_TMDS_SWRST(0));

	/*Set PreambleFilter*/
	for (i = 0; i < 3; i++) {
		value = (i + 1) * 11;
		if (i == 0)		/*channel 0*/
			hdmi_writel(hdmi_dev, FC_CH0PREAM, value);
		else if (i == 1)	/*channel 1*/
			hdmi_writel(hdmi_dev, FC_CH1PREAM, value & 0x3f);
		else if (i == 2)	/*channel 2*/
			hdmi_writel(hdmi_dev, FC_CH2PREAM, value & 0x3f);
	}
#endif

	hdmi_writel(hdmi_dev, FC_PRCONF,
		    v_FC_PR_FACTOR(timing->pixelrepeat) |
		    v_FC_PR_FACTOR_OUT(timing->pixelrepeat - 1));

	return 0;
}

static int rockchip_hdmiv2_video_packetizer(struct hdmi_dev *hdmi_dev,
					    struct hdmi_video *vpara)
{
	unsigned char color_depth = COLOR_DEPTH_24BIT_DEFAULT;
	unsigned char output_select = 0;
	unsigned char remap_size = 0;

	if (vpara->color_output == HDMI_COLOR_YCBCR422) {
		switch (vpara->color_output_depth) {
		case 8:
			remap_size = YCC422_16BIT;
			break;
		case 10:
			remap_size = YCC422_20BIT;
			break;
		case 12:
			remap_size = YCC422_24BIT;
			break;
		default:
			remap_size = YCC422_16BIT;
			break;
		}

		output_select = OUT_FROM_YCC422_REMAP;
		/*Config remap size for the different color Depth*/
		hdmi_msk_reg(hdmi_dev, VP_REMAP,
			     m_YCC422_SIZE, v_YCC422_SIZE(remap_size));
	} else {
		switch (vpara->color_output_depth) {
		case 10:
			color_depth = COLOR_DEPTH_30BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case 12:
			color_depth = COLOR_DEPTH_36BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case 16:
			color_depth = COLOR_DEPTH_48BIT;
			output_select = OUT_FROM_PIXEL_PACKING;
			break;
		case 8:
		default:
			color_depth = COLOR_DEPTH_24BIT_DEFAULT;
			output_select = OUT_FROM_8BIT_BYPASS;
			break;
		}
	}
	/*Config Color Depth*/
	hdmi_writel(hdmi_dev, VP_PR_CD, v_COLOR_DEPTH(color_depth));

	if (hdmi_dev->pixelrepeat > 1)
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_PIXEL_REPET_EN | m_BYPASS_SEL,
			     v_PIXEL_REPET_EN(1) | v_BYPASS_SEL(0));
	else
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_PIXEL_REPET_EN | m_BYPASS_SEL,
			     v_PIXEL_REPET_EN(0) | v_BYPASS_SEL(1));

	/*config output select*/
	if (output_select == OUT_FROM_PIXEL_PACKING) { /* pixel packing */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN |
			     m_YCC422_EN | m_OUTPUT_SEL,
			     v_BYPASS_EN(0) | v_PIXEL_PACK_EN(1) |
			     v_YCC422_EN(0) | v_OUTPUT_SEL(output_select));
	} else if (output_select == OUT_FROM_YCC422_REMAP) { /* YCC422 */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN |
			     m_YCC422_EN | m_OUTPUT_SEL,
			     v_BYPASS_EN(0) | v_PIXEL_PACK_EN(0) |
			     v_YCC422_EN(1) | v_OUTPUT_SEL(output_select));
	} else if (output_select == OUT_FROM_8BIT_BYPASS ||
		   output_select == 3) { /* bypass */
		hdmi_msk_reg(hdmi_dev, VP_CONF,
			     m_BYPASS_EN | m_PIXEL_PACK_EN |
			     m_YCC422_EN | m_OUTPUT_SEL,
			     v_BYPASS_EN(1) | v_PIXEL_PACK_EN(0) |
			     v_YCC422_EN(0) | v_OUTPUT_SEL(output_select));
	}

#if defined(HDMI_VIDEO_STUFFING)
	/* YCC422 and pixel packing stuffing*/
	hdmi_msk_reg(hdmi_dev, VP_STUFF, m_PR_STUFFING, v_PR_STUFFING(1));
	hdmi_msk_reg(hdmi_dev, VP_STUFF,
		     m_YCC422_STUFFING | m_PP_STUFFING,
		     v_YCC422_STUFFING(1) | v_PP_STUFFING(1));
#endif
	return 0;
}

static int rockchip_hdmiv2_video_sampler(struct hdmi_dev *hdmi_dev,
					 struct hdmi_video *vpara)
{
	int map_code = 0;

	if (vpara->color_input == HDMI_COLOR_YCBCR422) {
		/* YCC422 mapping is discontinued - only map 1 is supported */
		switch (vpara->color_output_depth) {
		case 8:
			map_code = VIDEO_YCBCR422_8BIT;
			break;
		case 10:
			map_code = VIDEO_YCBCR422_10BIT;
			break;
		case 12:
			map_code = VIDEO_YCBCR422_12BIT;
			break;
		default:
			map_code = VIDEO_YCBCR422_8BIT;
			break;
		}
	} else if (vpara->color_input == HDMI_COLOR_YCBCR420 ||
		   vpara->color_input == HDMI_COLOR_YCBCR444) {
		switch (vpara->color_output_depth) {
		case 10:
			map_code = VIDEO_YCBCR444_10BIT;
			break;
		case 12:
			map_code = VIDEO_YCBCR444_12BIT;
			break;
		case 16:
			map_code = VIDEO_YCBCR444_16BIT;
			break;
		case 8:
		default:
			map_code = VIDEO_YCBCR444_8BIT;
			break;
		}
	} else {
		switch (vpara->color_output_depth) {
		case 10:
			map_code = VIDEO_RGB444_10BIT;
			break;
		case 12:
			map_code = VIDEO_RGB444_12BIT;
			break;
		case 16:
			map_code = VIDEO_RGB444_16BIT;
			break;
		case 8:
		default:
			map_code = VIDEO_RGB444_8BIT;
			break;
		}
	}

	/* Set Data enable signal from external
	 * and set video sample input mapping
	 */
	hdmi_msk_reg(hdmi_dev, TX_INVID0,
		     m_INTERNAL_DE_GEN | m_VIDEO_MAPPING,
		     v_INTERNAL_DE_GEN(0) | v_VIDEO_MAPPING(map_code));

#if defined(HDMI_VIDEO_STUFFING)
	hdmi_writel(hdmi_dev, TX_GYDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_GYDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING,
		     m_GYDATA_STUFF, v_GYDATA_STUFF(1));
	hdmi_writel(hdmi_dev, TX_RCRDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_RCRDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING,
		     m_RCRDATA_STUFF, v_RCRDATA_STUFF(1));
	hdmi_writel(hdmi_dev, TX_BCBDATA0, 0x00);
	hdmi_writel(hdmi_dev, TX_BCBDATA1, 0x00);
	hdmi_msk_reg(hdmi_dev, TX_INSTUFFING,
		     m_BCBDATA_STUFF, v_BCBDATA_STUFF(1));
#endif
	return 0;
}

static const char coeff_csc[][24] = {
		/*   G		R	    B		Bias
		 *   A1    |	A2     |    A3     |	A4    |
		 *   B1    |    B2     |    B3     |    B4    |
		 *   C1    |    C2     |    C3     |    C4    |
		 */
	{	/* CSC_BYPASS */
		0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00,
		0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00,
		0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00,
	},
	{	/* CSC_RGB_0_255_TO_RGB_16_235_8BIT */
		0x36, 0xf7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,		/*G*/
		0x00, 0x00, 0x36, 0xf7, 0x00, 0x00, 0x00, 0x40,		/*R*/
		0x00, 0x00, 0x00, 0x00, 0x36, 0xf7, 0x00, 0x40,		/*B*/
	},
	{	/* CSC_RGB_0_255_TO_RGB_16_235_10BIT */
		0x36, 0xf7, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,		/*G*/
		0x00, 0x00, 0x36, 0xf7, 0x00, 0x00, 0x01, 0x00,		/*R*/
		0x00, 0x00, 0x00, 0x00, 0x36, 0xf7, 0x01, 0x00,		/*B*/
	},
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_8BIT */
		0x20, 0x40, 0x10, 0x80, 0x06, 0x40, 0x00, 0x40,		/*Y*/
		0xe8, 0x80, 0x1c, 0x00, 0xfb, 0x80, 0x02, 0x00,		/*Cr*/
		0xed, 0x80, 0xf6, 0x80, 0x1c, 0x00, 0x02, 0x00,		/*Cb*/
	},
	{	/* CSC_RGB_0_255_TO_ITU601_16_235_10BIT */
		0x20, 0x40, 0x10, 0x80, 0x06, 0x40, 0x01, 0x00,		/*Y*/
		0xe8, 0x80, 0x1c, 0x00, 0xfb, 0x80, 0x08, 0x00,		/*Cr*/
		0xed, 0x80, 0xf6, 0x80, 0x1c, 0x00, 0x08, 0x00,		/*Cb*/
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_8BIT */
		0x27, 0x40, 0x0b, 0xc0, 0x04, 0x00, 0x00, 0x40,		/*Y*/
		0xe6, 0x80, 0x1c, 0x00, 0xfd, 0x80, 0x02, 0x00,		/*Cr*/
		0xea, 0x40, 0xf9, 0x80, 0x1c, 0x00, 0x02, 0x00,		/*Cb*/
	},
	{	/* CSC_RGB_0_255_TO_ITU709_16_235_10BIT */
		0x27, 0x40, 0x0b, 0xc0, 0x04, 0x00, 0x01, 0x00,		/*Y*/
		0xe6, 0x80, 0x1c, 0x00, 0xfd, 0x80, 0x08, 0x00,		/*Cr*/
		0xea, 0x40, 0xf9, 0x80, 0x1c, 0x00, 0x08, 0x00,		/*Cb*/
	},
		/* Y		Cr	    Cb		Bias */
	{	/* CSC_ITU601_16_235_TO_RGB_0_255_8BIT */
		0x20, 0x00, 0x69, 0x26, 0x74, 0xfd, 0x01, 0x0e,		/*G*/
		0x20, 0x00, 0x2c, 0xdd, 0x00, 0x00, 0x7e, 0x9a,		/*R*/
		0x20, 0x00, 0x00, 0x00, 0x38, 0xb4, 0x7e, 0x3b,		/*B*/
	},
	{	/* CSC_ITU709_16_235_TO_RGB_0_255_8BIT */
		0x20, 0x00, 0x71, 0x06, 0x7a, 0x02, 0x00, 0xa7,		/*G*/
		0x20, 0x00, 0x32, 0x64, 0x00, 0x00, 0x7e, 0x6d,		/*R*/
		0x20, 0x00, 0x00, 0x00, 0x3b, 0x61, 0x7e, 0x25,		/*B*/
	},
};

static int rockchip_hdmiv2_video_csc(struct hdmi_dev *hdmi_dev,
				     struct hdmi_video *vpara)
{
	int i, mode, interpolation, decimation, csc_scale = 0;
	const char *coeff = NULL;
	unsigned char color_depth = 0;

	if (vpara->color_input == vpara->color_output) {
		hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL,
			     m_FEED_THROUGH_OFF, v_FEED_THROUGH_OFF(0));
		return 0;
	}

	if (vpara->color_input == HDMI_COLOR_YCBCR422 &&
	    vpara->color_output != HDMI_COLOR_YCBCR422 &&
	    vpara->color_output != HDMI_COLOR_YCBCR420) {
		interpolation = 1;
		hdmi_msk_reg(hdmi_dev, CSC_CFG,
			     m_CSC_INTPMODE, v_CSC_INTPMODE(interpolation));
	}

	if ((vpara->color_input == HDMI_COLOR_RGB_0_255 ||
	     vpara->color_input == HDMI_COLOR_YCBCR444) &&
	     vpara->color_output == HDMI_COLOR_YCBCR422) {
		decimation = 1;
		hdmi_msk_reg(hdmi_dev, CSC_CFG,
			     m_CSC_DECIMODE, v_CSC_DECIMODE(decimation));
	}

	mode = CSC_BYPASS;
	csc_scale = 0;

	switch (vpara->vic) {
	case HDMI_720X480I_60HZ_4_3:
	case HDMI_720X576I_50HZ_4_3:
	case HDMI_720X480P_60HZ_4_3:
	case HDMI_720X576P_50HZ_4_3:
	case HDMI_720X480I_60HZ_16_9:
	case HDMI_720X576I_50HZ_16_9:
	case HDMI_720X480P_60HZ_16_9:
	case HDMI_720X576P_50HZ_16_9:
		if (vpara->color_input == HDMI_COLOR_RGB_0_255 &&
		    vpara->color_output >= HDMI_COLOR_YCBCR444) {
			mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
			csc_scale = 0;
		} else if (vpara->color_input >= HDMI_COLOR_YCBCR444 &&
			   vpara->color_output == HDMI_COLOR_RGB_0_255) {
			mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			csc_scale = 1;
		}
		break;
	default:
		if (vpara->color_input == HDMI_COLOR_RGB_0_255 &&
		    vpara->color_output >= HDMI_COLOR_YCBCR444) {
			mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
			csc_scale = 0;
		} else if (vpara->color_input >= HDMI_COLOR_YCBCR444 &&
			   vpara->color_output == HDMI_COLOR_RGB_0_255) {
			mode = CSC_ITU709_16_235_TO_RGB_0_255_8BIT;
			csc_scale = 1;
		}
		break;
	}

	if ((vpara->color_input == HDMI_COLOR_RGB_0_255) &&
	    (vpara->color_output == HDMI_COLOR_RGB_16_235)) {
		mode = CSC_RGB_0_255_TO_RGB_16_235_8BIT;
		csc_scale = 0;
	}
	if (mode != CSC_BYPASS) {
		switch (vpara->color_output_depth) {
		case 10:
			color_depth = COLOR_DEPTH_30BIT;
			mode += 1;
			break;
		case 12:
			color_depth = COLOR_DEPTH_36BIT;
			mode += 2;
			break;
		case 16:
			color_depth = COLOR_DEPTH_48BIT;
			mode += 3;
			break;
		case 8:
		default:
			color_depth = COLOR_DEPTH_24BIT;
			break;
		}
	}
	coeff = coeff_csc[mode];
	for (i = 0; i < 24; i++)
		hdmi_writel(hdmi_dev, CSC_COEF_A1_MSB + i, coeff[i]);

	hdmi_msk_reg(hdmi_dev, CSC_SCALE,
		     m_CSC_SCALE, v_CSC_SCALE(csc_scale));
	/*config CSC_COLOR_DEPTH*/
	hdmi_msk_reg(hdmi_dev, CSC_SCALE,
		     m_CSC_COLOR_DEPTH, v_CSC_COLOR_DEPTH(color_depth));

	/* enable CSC */
	if (mode == CSC_BYPASS)
		hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL,
			     m_FEED_THROUGH_OFF, v_FEED_THROUGH_OFF(0));
	else
		hdmi_msk_reg(hdmi_dev, MC_FLOWCTRL,
			     m_FEED_THROUGH_OFF, v_FEED_THROUGH_OFF(1));
	return 0;
}

static int hdmi_dev_detect_hotplug(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	u32 value;

	value = hdmi_readl(hdmi_dev, PHY_STAT0);
	HDMIDBG(2, "[%s] reg%x value %02x\n", __func__, PHY_STAT0, value);
	if (value & m_PHY_HPD)
		return HDMI_HPD_ACTIVATED;

	return HDMI_HPD_REMOVED;
}

static int hdmi_dev_read_edid(struct hdmi *hdmi, int block, unsigned char *buff)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	int i = 0, n = 0, index = 0, ret = -1, trytime = 5;
	int offset = (block % 2) * 0x80;
	int interrupt = 0;

	HDMIDBG(2, "[%s] block %d\n", __func__, block);

	rockchip_hdmiv2_i2cm_reset(hdmi_dev);

	/* Set DDC I2C CLK which divided from DDC_CLK to 100KHz. */
	rockchip_hdmiv2_i2cm_clk_init(hdmi_dev);

	/* Enable I2C interrupt for reading edid */
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 0);

	hdmi_writel(hdmi_dev, I2CM_SLAVE, DDC_I2C_EDID_ADDR);
	hdmi_writel(hdmi_dev, I2CM_SEGADDR, DDC_I2C_SEG_ADDR);
	hdmi_writel(hdmi_dev, I2CM_SEGPTR, block / 2);
	for (n = 0; n < HDMI_EDID_BLOCK_SIZE / 8; n++) {
		for (trytime = 0; trytime < 5; trytime++) {
			hdmi_writel(hdmi_dev, I2CM_ADDRESS, offset + 8 * n);
			/* enable extend sequential read operation */
			if (block == 0)
				hdmi_msk_reg(hdmi_dev, I2CM_OPERATION,
					     m_I2CM_RD8, v_I2CM_RD8(1));
			else
				hdmi_msk_reg(hdmi_dev, I2CM_OPERATION,
					     m_I2CM_RD8_EXT,
					     v_I2CM_RD8_EXT(1));

			i = 20;
			while (i--) {
				usleep_range(900, 1000);
				interrupt = hdmi_readl(hdmi_dev,
						       IH_I2CM_STAT0);
				if (interrupt)
					hdmi_writel(hdmi_dev,
						    IH_I2CM_STAT0, interrupt);

				if (interrupt &
				    (m_SCDC_READREQ | m_I2CM_DONE |
				     m_I2CM_ERROR))
					break;
			}

			if (interrupt & m_I2CM_DONE) {
				for (index = 0; index < 8; index++)
					buff[8 * n + index] =
						hdmi_readl(hdmi_dev,
							   I2CM_READ_BUFF0 +
							   index);

				if (n == HDMI_EDID_BLOCK_SIZE / 8 - 1) {
					ret = 0;
					goto exit;
				}
				break;
			} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
				dev_err(hdmi->dev,
					"[%s] edid read %d error\n",
					__func__, offset + 8 * n);
			}
		}
		if (trytime == 5) {
			dev_err(hdmi->dev,
				"[%s] edid read error\n", __func__);
			break;
		}
	}

exit:
	/* Disable I2C interrupt */
	rockchip_hdmiv2_i2cm_mask_int(hdmi_dev, 1);
	return ret;
}

static void hdmi_dev_config_avi(struct hdmi *hdmi,
				struct hdmi_video *vpara)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	unsigned char colorimetry, ext_colorimetry = 0, aspect_ratio, y1y0;
	unsigned char rgb_quan_range = AVI_QUANTIZATION_RANGE_DEFAULT;

	hdmi_msk_reg(hdmi_dev, FC_DATAUTO3, m_AVI_AUTO, v_AVI_AUTO(0));
	hdmi_msk_reg(hdmi_dev, IH_FC_STAT1,
		     m_AVI_INFOFRAME, v_AVI_INFOFRAME(1));
	/* Set AVI infoFrame Data byte1 */
	if (vpara->color_output == HDMI_COLOR_YCBCR444)
		y1y0 = AVI_COLOR_MODE_YCBCR444;
	else if (vpara->color_output == HDMI_COLOR_YCBCR422)
		y1y0 = AVI_COLOR_MODE_YCBCR422;
	else if (vpara->color_output == HDMI_COLOR_YCBCR420)
		y1y0 = AVI_COLOR_MODE_YCBCR420;
	else
		y1y0 = AVI_COLOR_MODE_RGB;

	hdmi_msk_reg(hdmi_dev, FC_AVICONF0,
		     m_FC_ACTIV_FORMAT | m_FC_RGC_YCC,
		     v_FC_RGC_YCC(y1y0) | v_FC_ACTIV_FORMAT(1));

	/* Set AVI infoFrame Data byte2 */
	switch (vpara->vic) {
	case HDMI_720X480I_60HZ_4_3:
	case HDMI_720X576I_50HZ_4_3:
	case HDMI_720X480P_60HZ_4_3:
	case HDMI_720X576P_50HZ_4_3:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_4_3;
		if (vpara->colorimetry == HDMI_COLORIMETRY_NO_DATA)
			colorimetry = AVI_COLORIMETRY_SMPTE_170M;
		break;
	case HDMI_720X480I_60HZ_16_9:
	case HDMI_720X576I_50HZ_16_9:
	case HDMI_720X480P_60HZ_16_9:
	case HDMI_720X576P_50HZ_16_9:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
		if (vpara->colorimetry == HDMI_COLORIMETRY_NO_DATA)
			colorimetry = AVI_COLORIMETRY_SMPTE_170M;
		break;
	default:
		aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
		if (vpara->colorimetry == HDMI_COLORIMETRY_NO_DATA)
			colorimetry = AVI_COLORIMETRY_ITU709;
	}

	if (vpara->colorimetry > HDMI_COLORIMETRY_ITU709) {
		colorimetry = AVI_COLORIMETRY_EXTENDED;
		ext_colorimetry = vpara->colorimetry -
				HDMI_COLORIMETRY_EXTEND_XVYCC_601;
	} else if (vpara->color_output == HDMI_COLOR_RGB_16_235 ||
		 vpara->color_output == HDMI_COLOR_RGB_0_255) {
		colorimetry = AVI_COLORIMETRY_NO_DATA;
	} else if (vpara->colorimetry != HDMI_COLORIMETRY_NO_DATA) {
		colorimetry = vpara->colorimetry;
	}

	hdmi_writel(hdmi_dev, FC_AVICONF1,
		    v_FC_COLORIMETRY(colorimetry) |
		    v_FC_PIC_ASPEC_RATIO(aspect_ratio) |
		    v_FC_ACT_ASPEC_RATIO(ACTIVE_ASPECT_RATE_DEFAULT));

	/* Set AVI infoFrame Data byte3 */
	hdmi_msk_reg(hdmi_dev, FC_AVICONF2,
		     m_FC_EXT_COLORIMETRY | m_FC_QUAN_RANGE,
		     v_FC_EXT_COLORIMETRY(ext_colorimetry) |
		     v_FC_QUAN_RANGE(rgb_quan_range));

	/* Set AVI infoFrame Data byte4 */
	if ((vpara->vic > 92 && vpara->vic < 96) ||
	    (vpara->vic == 98) ||
	    (vpara->vic & HDMI_VIDEO_DMT))
		hdmi_writel(hdmi_dev, FC_AVIVID, 0);
	else
		hdmi_writel(hdmi_dev, FC_AVIVID, vpara->vic & 0xff);
	/* Set AVI infoFrame Data byte5 */
	hdmi_msk_reg(hdmi_dev, FC_AVICONF3, m_FC_YQ | m_FC_CN,
		     v_FC_YQ(YQ_LIMITED_RANGE) | v_FC_CN(CN_GRAPHICS));
	hdmi_msk_reg(hdmi_dev, FC_DATAUTO3, m_AVI_AUTO, v_AVI_AUTO(1));
}

static int hdmi_dev_config_vsi(struct hdmi *hdmi,
			       unsigned char vic_3d, unsigned char format)
{
	int i = 0, id = 0x000c03;
	unsigned char data[3] = {0};

	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(2, "[%s] vic %d format %d.\n", __func__, vic_3d, format);

	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_VSD_AUTO, v_VSD_AUTO(0));
	hdmi_writel(hdmi_dev, FC_VSDIEEEID2, id & 0xff);
	hdmi_writel(hdmi_dev, FC_VSDIEEEID1, (id >> 8) & 0xff);
	hdmi_writel(hdmi_dev, FC_VSDIEEEID0, (id >> 16) & 0xff);

	data[0] = format << 5;	/* PB4 --HDMI_Video_Format */
	switch (format) {
	case HDMI_VIDEO_FORMAT_4KX2K:
		data[1] = vic_3d;	/* PB5--HDMI_VIC */
		data[2] = 0;
		break;
	case HDMI_VIDEO_FORMAT_3D:
		data[1] = vic_3d << 4;	/* PB5--3D_Structure field */
		data[2] = 0;		/* PB6--3D_Ext_Data field */
		break;
	default:
		data[1] = 0;
		data[2] = 0;
		break;
	}

	for (i = 0; i < 3; i++)
		hdmi_writel(hdmi_dev, FC_VSDPAYLOAD0 + i, data[i]);
	hdmi_writel(hdmi_dev, FC_VSDSIZE, 0x6);

	hdmi_writel(hdmi_dev, FC_DATAUTO1, 0);
	hdmi_writel(hdmi_dev, FC_DATAUTO2, 0x11);
	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_VSD_AUTO, v_VSD_AUTO(1));
	return 0;
}

#define HDR_LSB(n) ((n) & 0xff)
#define HDR_MSB(n) (((n) & 0xff00) >> 8)

static void hdmi_dev_config_hdr(struct hdmi *hdmi,
				int eotf,
				struct hdmi_hdr_metadata *hdr)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	/* hdr is supportted after disignid = 0x21 */
	if (!hdmi_dev || hdmi_readl(hdmi_dev, DESIGN_ID) < 0x21)
		return;

	hdmi_writel(hdmi_dev, FC_DRM_HB, 1);/*verion = 0x1*/
	hdmi_writel(hdmi_dev, (FC_DRM_HB + 1), 26);/*length of following data*/
	hdmi_writel(hdmi_dev, FC_DRM_PB, eotf / 2);
	hdmi_writel(hdmi_dev, FC_DRM_PB + 1, 0);

	if (hdr) {
		hdmi_writel(hdmi_dev, FC_DRM_PB + 2, HDR_LSB(hdr->prim_x0));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 3, HDR_MSB(hdr->prim_x0));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 4, HDR_LSB(hdr->prim_y0));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 5, HDR_MSB(hdr->prim_y0));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 6, HDR_LSB(hdr->prim_x1));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 7, HDR_MSB(hdr->prim_x1));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 8, HDR_LSB(hdr->prim_y1));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 9, HDR_MSB(hdr->prim_y1));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 10, HDR_LSB(hdr->prim_x2));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 11, HDR_MSB(hdr->prim_x2));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 12, HDR_LSB(hdr->prim_y2));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 13, HDR_MSB(hdr->prim_y2));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 14, HDR_LSB(hdr->white_px));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 15, HDR_MSB(hdr->white_px));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 16, HDR_LSB(hdr->white_py));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 17, HDR_MSB(hdr->white_py));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 18, HDR_LSB(hdr->max_dml));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 19, HDR_MSB(hdr->max_dml));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 20, HDR_LSB(hdr->min_dml));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 21, HDR_MSB(hdr->min_dml));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 22, HDR_LSB(hdr->max_cll));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 23, HDR_MSB(hdr->max_cll));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 24, HDR_LSB(hdr->max_fall));
		hdmi_writel(hdmi_dev, FC_DRM_PB + 25, HDR_MSB(hdr->max_fall));
	} else {
		hdmi_writel(hdmi_dev, FC_DRM_PB + 1, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 2, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 3, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 4, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 5, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 6, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 7, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 8, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 9, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 10, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 11, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 12, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 13, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 14, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 15, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 16, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 17, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 18, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 19, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 20, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 21, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 22, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 23, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 24, 0);
		hdmi_writel(hdmi_dev, FC_DRM_PB + 25, 0);
	}
	if (eotf) {
		hdmi_msk_reg(hdmi_dev, FC_PACK_TXE, m_DRM_TXEN, v_DRM_TXEN(1));
		hdmi_msk_reg(hdmi_dev, FC_MASK2, m_DRM_MASK, v_DRM_MASK(0));
		hdmi_msk_reg(hdmi_dev, FC_DRM_UP, m_DRM_PUPD, v_DRM_PUPD(1));
	} else {
		hdmi_msk_reg(hdmi_dev, FC_PACK_TXE, m_DRM_TXEN, v_DRM_TXEN(0));
		hdmi_msk_reg(hdmi_dev, FC_MASK2, m_DRM_MASK, v_DRM_MASK(1));
		hdmi_msk_reg(hdmi_dev, FC_DRM_UP, m_DRM_PUPD, v_DRM_PUPD(1));
	}
}

static int hdmi_dev_config_spd(struct hdmi *hdmi, const char *vendor,
			       const char *product, char deviceinfo)
{
	struct hdmi_dev *hdmi_dev;
	int i, len;

	if (!hdmi || !vendor || !product)
		return -1;
	hdmi_dev = hdmi->property->priv;

	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_SPD_AUTO, v_SPD_AUTO(0));
	len = strlen(vendor);
	for (i = 0; i < 8; i++) {
		if (i < len)
			hdmi_writel(hdmi_dev, FC_SPDVENDORNAME0 + i,
				    vendor[i]);
		else
			hdmi_writel(hdmi_dev, FC_SPDVENDORNAME0 + i,
				    0);
	}
	len = strlen(product);
	for (i = 0; i < 16; i++) {
		if (i < len)
			hdmi_writel(hdmi_dev, FC_SPDPRODUCTNAME0 + i,
				    product[i]);
		else
			hdmi_writel(hdmi_dev, FC_SPDPRODUCTNAME0 + i,
				    0);
	}
	hdmi_writel(hdmi_dev, FC_SPDDEVICEINF, deviceinfo);
	hdmi_msk_reg(hdmi_dev, FC_DATAUTO0, m_SPD_AUTO, v_SPD_AUTO(1));
	return 0;
}

static int hdmi_dev_config_video(struct hdmi *hdmi, struct hdmi_video *vpara)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(2, "%s vic %d 3dformat %d color mode %d color depth %d\n",
		__func__, vpara->vic, vpara->format_3d,
		vpara->color_output, vpara->color_output_depth);

	if (!hdmi->uboot) {
		/* before configure video, we power off phy */
		if (hdmi_dev->soctype != HDMI_SOC_RK322X &&
		    hdmi_dev->soctype != HDMI_SOC_RK322XH &&
		    hdmi_dev->soctype != HDMI_SOC_RV1108) {
			hdmi_msk_reg(hdmi_dev, PHY_CONF0,
				     m_PDDQ_SIG | m_TXPWRON_SIG,
				     v_PDDQ_SIG(1) | v_TXPWRON_SIG(0));
		} else {
			hdmi_msk_reg(hdmi_dev, PHY_CONF0,
				     m_ENHPD_RXSENSE_SIG,
				     v_ENHPD_RXSENSE_SIG(1));
			if (hdmi->soctype == HDMI_SOC_RK322X)
				regmap_write(hdmi_dev->grf_base,
					     RK322X_GRF_SOC_CON2,
					     RK322X_PLL_POWER_DOWN);
			else if (hdmi->soctype == HDMI_SOC_RK322XH)
				regmap_write(hdmi_dev->grf_base,
					     RK322XH_GRF_SOC_CON3,
					     RK322XH_PLL_POWER_DOWN);
			else if (hdmi->soctype == HDMI_SOC_RV1108)
				regmap_write(hdmi_dev->grf_base,
					     RV1108_GRF_SOC_CON4,
					     RV1108_PLL_POWER_DOWN);
		}
		/* force output blue */
		if (vpara->color_output == HDMI_COLOR_RGB_0_255) {
			hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x00);	/*R*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x00);	/*G*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x00);	/*B*/
		} else if (vpara->color_output == HDMI_COLOR_RGB_16_235) {
			hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x10);	/*R*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x10);	/*G*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x10);	/*B*/
		} else {
			hdmi_writel(hdmi_dev, FC_DBGTMDS2, 0x80);	/*Cr*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS1, 0x10);	/*Y*/
			hdmi_writel(hdmi_dev, FC_DBGTMDS0, 0x80);	/*Cb*/
		}
		hdmi_msk_reg(hdmi_dev, FC_DBGFORCE,
			     m_FC_FORCEVIDEO, v_FC_FORCEVIDEO(1));
		hdmi_writel(hdmi_dev, MC_CLKDIS, m_HDCPCLK_DISABLE);
	}

	/*pclk source must be vop on mode YCBCR420*/
	if (hdmi_dev->soctype == HDMI_SOC_RK3288 &&
	    hdmi_readl(hdmi_dev, REVISION_ID) == 0x1a) {
		if (vpara->color_input == HDMI_COLOR_YCBCR420)
			writel_relaxed(((1 << (2 + hdmi->property->videosrc)) |
				       (1 << (18 + hdmi->property->videosrc))),
				       RK_GRF_VIRT + RK3288_GRF_SOC_CON16);
		else
			writel_relaxed(((0 << (2 + hdmi->property->videosrc)) |
				       (1 << (18 + hdmi->property->videosrc))),
				       RK_GRF_VIRT + RK3288_GRF_SOC_CON16);
	}

	if (rockchip_hdmiv2_video_framecomposer(hdmi, vpara) < 0)
		return -1;

	if (rockchip_hdmiv2_video_packetizer(hdmi_dev, vpara) < 0)
		return -1;
	/* Color space convert */
	if (rockchip_hdmiv2_video_csc(hdmi_dev, vpara) < 0)
		return -1;
	if (rockchip_hdmiv2_video_sampler(hdmi_dev, vpara) < 0)
		return -1;

	if (vpara->sink_hdmi == OUTPUT_HDMI) {
		hdmi_dev_config_avi(hdmi, vpara);
		hdmi_dev_config_spd(hdmi, hdmi_dev->vendor_name,
				    hdmi_dev->product_name,
				    hdmi_dev->deviceinfo);
		if (vpara->format_3d != HDMI_3D_NONE) {
			hdmi_dev_config_vsi(hdmi,
					    vpara->format_3d,
					    HDMI_VIDEO_FORMAT_3D);
		} else if ((vpara->vic > 92 && vpara->vic < 96) ||
			 (vpara->vic == 98)) {
			vpara->vic = (vpara->vic == 98) ?
				     4 : (96 - vpara->vic);
			hdmi_dev_config_vsi(hdmi,
					    vpara->vic,
					    HDMI_VIDEO_FORMAT_4KX2K);
		} else {
			hdmi_dev_config_vsi(hdmi,
					    vpara->vic,
					    HDMI_VIDEO_FORMAT_NORMAL);
		}
		hdmi_dev_config_hdr(hdmi, vpara->eotf, &hdmi->hdr);
		dev_info(hdmi->dev, "[%s] success output HDMI.\n", __func__);
	} else {
		dev_info(hdmi->dev, "[%s] success output DVI.\n", __func__);
	}

	if (!hdmi->uboot)
		rockchip_hdmiv2_config_phy(hdmi_dev);
	else
		hdmi_msk_reg(hdmi_dev, PHY_MASK, m_PHY_LOCK, v_PHY_LOCK(0));
	return 0;
}

static void hdmi_dev_config_aai(struct hdmi_dev *hdmi_dev,
				struct hdmi_audio *audio)
{
	int ca;

	/* Refer to CEA861-E Audio infoFrame
	 * Set both Audio Channel Count and Audio Coding
	 * Type Refer to Stream Head for HDMI
	 */
	hdmi_msk_reg(hdmi_dev, FC_AUDICONF0,
		     m_FC_CHN_CNT | m_FC_CODING_TYPE,
		     v_FC_CHN_CNT(audio->channel - 1) | v_FC_CODING_TYPE(0));

	/* Set both Audio Sample Size and Sample Frequency
	 * Refer to Stream Head for HDMI
	 */
	hdmi_msk_reg(hdmi_dev, FC_AUDICONF1,
		     m_FC_SAMPLE_SIZE | m_FC_SAMPLE_FREQ,
		     v_FC_SAMPLE_SIZE(0) | v_FC_SAMPLE_FREQ(0));

	/* Set Channel Allocation */
	switch (audio->channel) {
	case 4:
		ca = 0x3;
		break;
	case 6:
		ca = 0xb;
		break;
	case 8:
		ca = 0x13;
		break;
	default:
		ca = 0x0;
	}

	hdmi_writel(hdmi_dev, FC_AUDICONF2, ca);

	/* Set LFEPBLDOWN-MIX INH and LSV */
	hdmi_writel(hdmi_dev, FC_AUDICONF3, 0x00);
}

struct hdmi_audio_tmds_n_cts {
	unsigned long tmds;
	unsigned int n_32k;
	unsigned int n_44k1;
	unsigned int n_48k;
};

static const struct hdmi_audio_tmds_n_cts common_tmds_n_table[] = {
	{ .tmds = 27000000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 28320000, .n_32k = 4096, .n_44k1 = 5586, .n_48k = 6144, },
	{ .tmds = 30240000, .n_32k = 4096, .n_44k1 = 5642, .n_48k = 6144, },
	{ .tmds = 31500000, .n_32k = 4096, .n_44k1 = 5600, .n_48k = 6144, },
	{ .tmds = 32000000, .n_32k = 4096, .n_44k1 = 5733, .n_48k = 6144, },
	{ .tmds = 33750000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 36000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 40000000, .n_32k = 4096, .n_44k1 = 5733, .n_48k = 6144, },
	{ .tmds = 49500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 50000000, .n_32k = 4096, .n_44k1 = 5292, .n_48k = 6144, },
	{ .tmds = 54000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 65000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 68250000, .n_32k = 4096, .n_44k1 = 5376, .n_48k = 6144, },
	{ .tmds = 71000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 72000000, .n_32k = 4096, .n_44k1 = 5635, .n_48k = 6144, },
	{ .tmds = 73250000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 74176000, .n_32k = 11648, .n_44k1 = 17836, .n_48k = 11648, },
	{ .tmds = 74250000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 75000000, .n_32k = 4096, .n_44k1 = 5880, .n_48k = 6144, },
	{ .tmds = 78750000, .n_32k = 4096, .n_44k1 = 5600, .n_48k = 6144, },
	{ .tmds = 78800000, .n_32k = 4096, .n_44k1 = 5292, .n_48k = 6144, },
	{ .tmds = 79500000, .n_32k = 4096, .n_44k1 = 4704, .n_48k = 6144, },
	{ .tmds = 83500000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 85500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 88750000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 97750000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 101000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 106500000, .n_32k = 4096, .n_44k1 = 4704, .n_48k = 6144, },
	{ .tmds = 108000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 115500000, .n_32k = 4096, .n_44k1 = 5712, .n_48k = 6144, },
	{ .tmds = 119000000, .n_32k = 4096, .n_44k1 = 5544, .n_48k = 6144, },
	{ .tmds = 135000000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 146250000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 148352000, .n_32k = 11648, .n_44k1 = 8918, .n_48k = 5824, },
	{ .tmds = 148500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 154000000, .n_32k = 4096, .n_44k1 = 5544, .n_48k = 6144, },
	{ .tmds = 162000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },

	/* For 297 MHz+ HDMI spec have some other rule for setting N */
	{ .tmds = 296703000, .n_32k = 5824, .n_44k1 = 4459, .n_48k = 5824, },
	{ .tmds = 297000000, .n_32k = 3073, .n_44k1 = 4704, .n_48k = 5120, },
	{ .tmds = 593407000, .n_32k = 5824, .n_44k1 = 8918, .n_48k = 5824, },
	{ .tmds = 594000000, .n_32k = 3073, .n_44k1 = 9408, .n_48k = 10240, },

	/* End of table */
	{ .tmds = 0,         .n_32k = 0,    .n_44k1 = 0,    .n_48k = 0, },
};

static u64 hdmi_audio_math_diff(unsigned int freq, unsigned int n,
				unsigned int pixel_clk)
{
	u64 final, diff;
	u64 cts;

	final = (u64)pixel_clk * n;

	cts = final;
	do_div(cts, 128 * freq);

	diff = final - (u64)cts * (128 * freq);

	return diff;
}

static unsigned int hdmi_compute_n(struct hdmi_dev *hdmi,
				   unsigned long pixel_clk,
				   unsigned long freq)
{
	unsigned int min_n = DIV_ROUND_UP((128 * freq), 1500);
	unsigned int max_n = (128 * freq) / 300;
	unsigned int ideal_n = (128 * freq) / 1000;
	unsigned int best_n_distance = ideal_n;
	unsigned int best_n = 0;
	u64 best_diff = U64_MAX;
	int n;

	/* If the ideal N could satisfy the audio math, then just take it */
	if (hdmi_audio_math_diff(freq, ideal_n, pixel_clk) == 0)
		return ideal_n;

	for (n = min_n; n <= max_n; n++) {
		u64 diff = hdmi_audio_math_diff(freq, n, pixel_clk);

		if (diff < best_diff ||
		    (diff == best_diff &&
		     abs(n - ideal_n) < best_n_distance)) {
			best_n = n;
			best_diff = diff;
			best_n_distance = abs(best_n - ideal_n);
		}

		/*
		 * The best N already satisfy the audio math, and also be
		 * the closest value to ideal N, so just cut the loop.
		 */
		if (best_diff == 0 && abs(n - ideal_n) > best_n_distance)
			break;
	}

	return best_n;
}

static int hdmi_match_tmds_n_table(struct hdmi_dev *hdmi,
				   unsigned long pixel_clk,
				   unsigned long freq)
{
	const struct hdmi_audio_tmds_n_cts *tmds_n = NULL;
	int i;

	if (!tmds_n) {
		for (i = 0; common_tmds_n_table[i].tmds != 0; i++) {
			if (pixel_clk == common_tmds_n_table[i].tmds) {
				tmds_n = &common_tmds_n_table[i];
				break;
			}
		}
	}

	if (!tmds_n)
		return -ENOENT;

	switch (freq) {
	case 32000:
		return tmds_n->n_32k;
	case 44100:
	case 88200:
	case 176400:
		return (freq / 44100) * tmds_n->n_44k1;
	case 48000:
	case 96000:
	case 192000:
		return (freq / 48000) * tmds_n->n_48k;
	default:
		return -ENOENT;
	}
}

static unsigned int hdmi_find_n(struct hdmi_dev *hdmi,
				unsigned long pixel_clk,
				unsigned long sample_rate)
{
	int n;

	n = hdmi_match_tmds_n_table(hdmi, pixel_clk, sample_rate);
	if (n > 0)
		return n;

	dev_warn(hdmi->dev, "Rate %lu missing; compute N dynamically\n",
		 pixel_clk);

	return hdmi_compute_n(hdmi, pixel_clk, sample_rate);
}

static int hdmi_dev_config_audio(struct hdmi *hdmi, struct hdmi_audio *audio)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	int word_length = 0, channel = 0, mclk_fs, sample_rate;
	unsigned int N = 0, CTS = 0, rate;
	unsigned long ftdms = hdmi_dev->tmdsclk;
	u64 tmp;

	HDMIDBG(2, "%s\n", __func__);

	if (audio->channel < 3)
		channel = I2S_CHANNEL_1_2;
	else if (audio->channel < 5)
		channel = I2S_CHANNEL_3_4;
	else if (audio->channel < 7)
		channel = I2S_CHANNEL_5_6;
	else
		channel = I2S_CHANNEL_7_8;

	mclk_fs = FS_128;
	switch (audio->rate) {
	case HDMI_AUDIO_FS_32000:
		sample_rate = 32000;
		rate = AUDIO_32K;
		break;
	case HDMI_AUDIO_FS_44100:
		sample_rate = 44100;
		rate = AUDIO_441K;
		break;
	case HDMI_AUDIO_FS_48000:
		sample_rate = 48000;
		rate = AUDIO_48K;
		break;
	case HDMI_AUDIO_FS_88200:
		sample_rate = 88200;
		rate = AUDIO_882K;
		break;
	case HDMI_AUDIO_FS_96000:
		sample_rate = 96000;
		rate = AUDIO_96K;
		break;
	case HDMI_AUDIO_FS_176400:
		sample_rate = 176400;
		rate = AUDIO_1764K;
		break;
	case HDMI_AUDIO_FS_192000:
		sample_rate = 192000;
		rate = AUDIO_192K;
		break;
	default:
		dev_err(hdmi_dev->hdmi->dev,
			"[%s] not support such sample rate %d\n",
			__func__, audio->rate);
		return -ENOENT;
	}

	if (hdmi_dev->frac_en)
		ftdms = DIV_ROUND_CLOSEST(hdmi_dev->tmdsclk, 1001) * 1000;

	N = hdmi_find_n(hdmi_dev, ftdms, sample_rate);
	tmp = (u64)ftdms * N;
	do_div(tmp, 128 * sample_rate);
	CTS = tmp;

	switch (audio->word_length) {
	case HDMI_AUDIO_WORD_LENGTH_16bit:
		word_length = I2S_16BIT_SAMPLE;
		break;
	case HDMI_AUDIO_WORD_LENGTH_20bit:
		word_length = I2S_20BIT_SAMPLE;
		break;
	case HDMI_AUDIO_WORD_LENGTH_24bit:
		word_length = I2S_24BIT_SAMPLE;
		break;
	default:
		word_length = I2S_16BIT_SAMPLE;
	}

	HDMIDBG(2, "rate = %d, tmdsclk = %lu, N = %d, CTS = %d\n",
		sample_rate, ftdms, N, CTS);
	/* more than 2 channels => layout 1 else layout 0 */
	hdmi_msk_reg(hdmi_dev, FC_AUDSCONF,
		     m_AUD_PACK_LAYOUT,
		     v_AUD_PACK_LAYOUT((audio->channel > 2) ? 1 : 0));

	if (hdmi_dev->audiosrc == HDMI_AUDIO_SRC_SPDIF) {
		mclk_fs = FS_128;
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_I2S_SEL, v_I2S_SEL(AUDIO_SPDIF_GPA));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF1,
			     m_SET_NLPCM | m_SPDIF_WIDTH,
			     v_SET_NLPCM(PCM_LINEAR) |
			     v_SPDIF_WIDTH(word_length));
		/*Mask fifo empty and full int and reset fifo*/
		hdmi_msk_reg(hdmi_dev, AUD_SPDIFINT,
			     m_FIFO_EMPTY_MASK | m_FIFO_FULL_MASK,
			     v_FIFO_EMPTY_MASK(1) | v_FIFO_FULL_MASK(1));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF0,
			     m_SW_SAUD_FIFO_RST, v_SW_SAUD_FIFO_RST(1));
	} else {
		/*Mask fifo empty and full int and reset fifo*/
		hdmi_msk_reg(hdmi_dev, AUD_INT,
			     m_FIFO_EMPTY_MASK | m_FIFO_FULL_MASK,
			     v_FIFO_EMPTY_MASK(1) | v_FIFO_FULL_MASK(1));
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_SW_AUD_FIFO_RST, v_SW_AUD_FIFO_RST(1));
		hdmi_writel(hdmi_dev, MC_SWRSTZREQ, 0xF7);
		usleep_range(90, 100);
		/*
		 * when we try to use hdmi nlpcm mode
		 * we should use set AUD_CONF2 to open this route and set
		 * word_length to 24bit for b.p.c.u.v with 16bit raw data
		 * when the bitstream data  up to 8 channel, we should use
		 * the hdmi hbr mode
		 * HBR Mode : Dolby TrueHD
		 *            Dolby Atmos
		 *            DTS-HDMA
		 * NLPCM Mode :
		 * FS_32000 FS_44100 FS_48000 : Dolby Digital &  DTS
		 * FS_176400 FS_192000        : Dolby Digital Plus
		 */
		if (audio->type == HDMI_AUDIO_NLPCM) {
			if (channel == I2S_CHANNEL_7_8) {
				HDMIDBG(2, "hbr mode.\n");
				hdmi_writel(hdmi_dev, AUD_CONF2, 0x1);
				word_length = I2S_24BIT_SAMPLE;
			} else if ((audio->rate == HDMI_AUDIO_FS_32000) ||
				   (audio->rate == HDMI_AUDIO_FS_44100) ||
				   (audio->rate == HDMI_AUDIO_FS_48000) ||
				   (audio->rate == HDMI_AUDIO_FS_176400) ||
				   (audio->rate == HDMI_AUDIO_FS_192000)) {
				HDMIDBG(2, "nlpcm mode.\n");
				hdmi_writel(hdmi_dev, AUD_CONF2, 0x2);
				word_length = I2S_24BIT_SAMPLE;
			} else {
				hdmi_writel(hdmi_dev, AUD_CONF2, 0x0);
			}
		} else if (hdmi_readl(hdmi_dev, DESIGN_ID) >= 0x21) {
			hdmi_writel(hdmi_dev, AUD_CONF2, 0x4);
		} else {
			hdmi_writel(hdmi_dev, AUD_CONF2, 0x0);
		}
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_I2S_SEL | m_I2S_IN_EN,
			     v_I2S_SEL(AUDIO_I2S) | v_I2S_IN_EN(channel));
		hdmi_writel(hdmi_dev, AUD_CONF1,
			    v_I2S_MODE(I2S_STANDARD_MODE) |
			    v_I2S_WIDTH(word_length));
	}

	hdmi_msk_reg(hdmi_dev, AUD_INPUTCLKFS,
		     m_LFS_FACTOR, v_LFS_FACTOR(mclk_fs));

	/*Set N value*/
	hdmi_msk_reg(hdmi_dev, AUD_N3, m_NCTS_ATOMIC_WR, v_NCTS_ATOMIC_WR(1));
	/*Set CTS by manual*/
	hdmi_msk_reg(hdmi_dev, AUD_CTS3,
		     m_N_SHIFT | m_CTS_MANUAL | m_AUD_CTS3,
		     v_N_SHIFT(N_SHIFT_1) |
		     v_CTS_MANUAL(1) |
		     v_AUD_CTS3(CTS >> 16));
	hdmi_writel(hdmi_dev, AUD_CTS2, (CTS >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_CTS1, CTS & 0xff);

	hdmi_msk_reg(hdmi_dev, AUD_N3, m_AUD_N3, v_AUD_N3(N >> 16));
	hdmi_writel(hdmi_dev, AUD_N2, (N >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_N1, N & 0xff);

	/* set channel status register */
	hdmi_msk_reg(hdmi_dev, FC_AUDSCHNLS7,
		     m_AUDIO_SAMPLE_RATE, v_AUDIO_SAMPLE_RATE(rate));
	hdmi_writel(hdmi_dev, FC_AUDSCHNLS8, ((~rate) << 4) | 0x2);

	hdmi_msk_reg(hdmi_dev, AUD_CONF0,
		     m_SW_AUD_FIFO_RST, v_SW_AUD_FIFO_RST(1));

	hdmi_dev_config_aai(hdmi_dev, audio);

	return 0;
}

static int hdmi_dev_control_output(struct hdmi *hdmi, int enable)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	struct hdmi_video vpara;

	HDMIDBG(2, "[%s] %d\n", __func__, enable);
	if (enable == HDMI_AV_UNMUTE) {
		hdmi_writel(hdmi_dev, FC_DBGFORCE, 0x00);
		if (hdmi->edid.sink_hdmi == OUTPUT_HDMI)
			hdmi_msk_reg(hdmi_dev, FC_GCP,
				     m_FC_SET_AVMUTE | m_FC_CLR_AVMUTE,
				     v_FC_SET_AVMUTE(0) | v_FC_CLR_AVMUTE(1));
	} else {
		if (enable & HDMI_VIDEO_MUTE) {
			if (hdmi->edid.sink_hdmi == OUTPUT_HDMI) {
				hdmi_msk_reg(hdmi_dev, FC_GCP,
					     m_FC_SET_AVMUTE |
					     m_FC_CLR_AVMUTE,
					     v_FC_SET_AVMUTE(1) |
					     v_FC_CLR_AVMUTE(0));
				vpara.vic = hdmi->vic;
				vpara.color_output = HDMI_COLOR_RGB_0_255;
				hdmi_dev_config_avi(hdmi, &vpara);
				while ((!hdmi_readl(hdmi_dev, IH_FC_STAT1)) &
				       m_AVI_INFOFRAME) {
					usleep_range(900, 1000);
				}
			}
		}
/*		if (enable & HDMI_AUDIO_MUTE) {
 *			hdmi_msk_reg(hdmi_dev, FC_AUDSCONF,
 *				     m_AUD_PACK_SAMPFIT,
 *				     v_AUD_PACK_SAMPFIT(0x0F));
 *		}
 */
		if (enable == (HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE)) {
			if (hdmi->ops->hdcp_power_off_cb)
				hdmi->ops->hdcp_power_off_cb(hdmi);
			rockchip_hdmiv2_powerdown(hdmi_dev);
		}
	}
	return 0;
}

static int hdmi_dev_insert(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(2, "%s\n", __func__);
	if (!hdmi->uboot)
		hdmi_writel(hdmi_dev, MC_CLKDIS, m_HDCPCLK_DISABLE);
	if (gpio_is_valid(hdmi_dev->io_pullup))
		gpio_set_value(hdmi_dev->io_pullup, 1);
	if (hdmi_dev->soctype == HDMI_SOC_RK322XH) {
		regmap_write(hdmi_dev->grf_base,
			     RK322XH_GRF_SOC_CON4,
			     RK322XH_IO_5V_DOMAIN);
	}
	return HDMI_ERROR_SUCCESS;
}

static int hdmi_dev_remove(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(2, "%s\n", __func__);
	if (hdmi->ops->hdcp_power_off_cb)
		hdmi->ops->hdcp_power_off_cb(hdmi);
	rockchip_hdmiv2_powerdown(hdmi_dev);
	if (gpio_is_valid(hdmi_dev->io_pullup))
		gpio_set_value(hdmi_dev->io_pullup, 0);
	if (hdmi_dev->soctype == HDMI_SOC_RK322XH) {
		regmap_write(hdmi_dev->grf_base,
			     RK322XH_GRF_SOC_CON4,
			     RK322XH_IO_3V_DOMAIN);
	}
	hdmi_dev->tmdsclk = 0;
	return HDMI_ERROR_SUCCESS;
}

static int hdmi_dev_enable(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(2, "%s\n", __func__);
	if (!hdmi_dev->enable) {
		if (hdmi->property->feature & SUPPORT_CEC_WAKEUP) {
			hdmi_writel(hdmi_dev, IH_MUTE, 0x02);
			hdmi_msk_reg(hdmi_dev, CEC_CTRL,
				     m_CEC_STANBY, v_CEC_STANBY(0));
			hdmi_writel(hdmi_dev, CEC_WKUPCTRL, 0x00);
		} else {
			hdmi_writel(hdmi_dev, IH_MUTE, 0);
		}
		hdmi_dev->enable = 1;
	}
	hdmi_submit_work(hdmi, HDMI_HPD_CHANGE, 10, 0);
	return 0;
}

static int hdmi_dev_disable(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(2, "%s\n", __func__);
	if (hdmi_dev->enable) {
		hdmi_dev->enable = 0;
		if (hdmi->property->feature & SUPPORT_CEC_WAKEUP) {
			hdmi_writel(hdmi_dev, CEC_WKUPCTRL, 0xff);
			hdmi_msk_reg(hdmi_dev, CEC_CTRL,
				     m_CEC_STANBY, v_CEC_STANBY(1));
		}
		hdmi_writel(hdmi_dev, IH_MUTE, 0x1);
	}
	return 0;
}

void rockchip_hdmiv2_dev_init_ops(struct hdmi_ops *ops)
{
	if (ops) {
		ops->enable	= hdmi_dev_enable;
		ops->disable	= hdmi_dev_disable;
		ops->getstatus	= hdmi_dev_detect_hotplug;
		ops->insert	= hdmi_dev_insert;
		ops->remove	= hdmi_dev_remove;
		ops->getedid	= hdmi_dev_read_edid;
		ops->setvideo	= hdmi_dev_config_video;
		ops->setaudio	= hdmi_dev_config_audio;
		ops->setmute	= hdmi_dev_control_output;
		ops->setavi	= hdmi_dev_config_avi;
		ops->setvsi	= hdmi_dev_config_vsi;
		ops->sethdr	= hdmi_dev_config_hdr;
	}
}

void rockchip_hdmiv2_dev_initial(struct hdmi_dev *hdmi_dev)
{
	struct hdmi *hdmi = hdmi_dev->hdmi;

	if (!hdmi->uboot) {
		pr_info("reset hdmi\n");
		if (hdmi_dev->soctype == HDMI_SOC_RK3288) {
			rk3288_cru_set_soft_reset(RK3288_SOFT_RST_HDMI, true);
			usleep_range(10, 20);
			rk3288_cru_set_soft_reset(RK3288_SOFT_RST_HDMI, false);
		} else {
			if (hdmi_dev->soctype == HDMI_SOC_RK322X) {
				regmap_write(hdmi_dev->grf_base,
					     RK322X_GRF_SOC_CON2,
					     RK322X_DDC_MASK_EN);
				regmap_write(hdmi_dev->grf_base,
					     RK322X_GRF_SOC_CON6,
					     RK322X_IO_3V_DOMAIN);
			} else if (hdmi_dev->soctype == HDMI_SOC_RK322XH) {
				regmap_write(hdmi_dev->grf_base,
					     RK322XH_GRF_SOC_CON4,
					     RK322XH_IO_3V_DOMAIN |
					     RK322XH_HPD_3V);
				regmap_write(hdmi_dev->grf_base,
					     RK322XH_GRF_SOC_CON3,
					     RK322XH_IO_CTRL_BY_HDMI);
				regmap_write(hdmi_dev->grf_base,
					     RK322XH_GRF_SOC_CON2,
					     RK322XH_DDC_MASK_EN |
					     BIT(2 + 16));
			} else if (hdmi_dev->soctype == HDMI_SOC_RV1108) {
				regmap_write(hdmi_dev->grf_base,
					     RV1108_GRF_SOC_CON4,
					     RV1108_DDC_MASK_EN);
				regmap_write(hdmi_dev->grf_base,
					     RV1108_GRF_SOC_CON2,
					     RV1108_SDA_READ_EN);
			}
			reset_control_assert(hdmi_dev->reset);
			usleep_range(10, 20);
			reset_control_deassert(hdmi_dev->reset);
		}
		rockchip_hdmiv2_powerdown(hdmi_dev);
	} else {
		hdmi->hotplug = hdmi_dev_detect_hotplug(hdmi);
		if (hdmi->hotplug != HDMI_HPD_ACTIVATED) {
			if (hdmi_dev->soctype == HDMI_SOC_RK322XH)
				regmap_write(hdmi_dev->grf_base,
					     RK322XH_GRF_SOC_CON4,
					     RK322XH_IO_3V_DOMAIN);
			hdmi->uboot = 0;
		} else if (hdmi_dev->soctype == HDMI_SOC_RV1108 ||
			   hdmi_dev->soctype == HDMI_SOC_RK322XH) {
			/* Enable PHY IRQ */
			rockchip_hdmiv2_write_phy(hdmi_dev, 0x05, 0x22);
			rockchip_hdmiv2_write_phy(hdmi_dev, 0x07, 0x22);
		}
	}
	/*mute unnecessary interrupt, only enable hpd*/
	hdmi_writel(hdmi_dev, IH_MUTE_FC_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_FC_STAT1, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_FC_STAT2, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_AS_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_PHY_STAT0, 0xfc);
	hdmi_writel(hdmi_dev, IH_MUTE_I2CM_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_CEC_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_VP_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_I2CMPHY_STAT0, 0xff);
	hdmi_writel(hdmi_dev, IH_MUTE_AHBDMAAUD_STAT0, 0xff);

	/* disable hdcp interrupt */
	hdmi_writel(hdmi_dev, A_APIINTMSK, 0xff);
	hdmi_writel(hdmi_dev, PHY_MASK, 0xf1);

	if (hdmi->property->feature & SUPPORT_CEC)
		rockchip_hdmiv2_cec_init(hdmi);
	if (hdmi->property->feature & SUPPORT_HDCP)
		rockchip_hdmiv2_hdcp_init(hdmi);
}

irqreturn_t rockchip_hdmiv2_dev_irq(int irq, void *priv)
{
	struct hdmi_dev *hdmi_dev = priv;
	struct hdmi *hdmi = hdmi_dev->hdmi;
	char phy_pol = hdmi_readl(hdmi_dev, PHY_POL0);
	char phy_status = hdmi_readl(hdmi_dev, PHY_STAT0);
	char phy_int0 = hdmi_readl(hdmi_dev, PHY_INI0);
	/*read interrupt*/
	char fc_stat0 = hdmi_readl(hdmi_dev, IH_FC_STAT0);
	char fc_stat1 = hdmi_readl(hdmi_dev, IH_FC_STAT1);
	char fc_stat2 = hdmi_readl(hdmi_dev, IH_FC_STAT2);
	char aud_int = hdmi_readl(hdmi_dev, IH_AS_SATA0);
	char phy_int = hdmi_readl(hdmi_dev, IH_PHY_STAT0);
	char vp_stat0 = hdmi_readl(hdmi_dev, IH_VP_STAT0);
	char cec_int = hdmi_readl(hdmi_dev, IH_CEC_STAT0);
	char hdcp_int = hdmi_readl(hdmi_dev, A_APIINTSTAT);
	u8 hdcp2_int = hdmi_readl(hdmi_dev, HDCP2REG_STAT);

	/*clear interrupt*/
	hdmi_writel(hdmi_dev, IH_FC_STAT0, fc_stat0);
	hdmi_writel(hdmi_dev, IH_FC_STAT1, fc_stat1);
	hdmi_writel(hdmi_dev, IH_FC_STAT2, fc_stat2);
	hdmi_writel(hdmi_dev, IH_VP_STAT0, vp_stat0);

	if (phy_int0 || phy_int) {
		if ((phy_int0 & m_PHY_LOCK) &&
		    (phy_pol & m_PHY_LOCK) == 0) {
			pr_info("hdmi phy pll unlock\n");
			hdmi_submit_work(hdmi, HDMI_SET_VIDEO, 0, 0);
		}
		phy_pol = (phy_int0 & (~phy_status)) | ((~phy_int0) & phy_pol);
		hdmi_writel(hdmi_dev, PHY_POL0, phy_pol);
		hdmi_writel(hdmi_dev, IH_PHY_STAT0, phy_int);
		if ((phy_int & m_HPD) || ((phy_int & 0x3c) == 0x3c))
			hdmi_submit_work(hdmi, HDMI_HPD_CHANGE, 20, 0);
	}

	/* Audio error */
	if (aud_int) {
		hdmi_writel(hdmi_dev, IH_AS_SATA0, aud_int);
		hdmi_msk_reg(hdmi_dev, AUD_CONF0,
			     m_SW_AUD_FIFO_RST, v_SW_AUD_FIFO_RST(1));
		hdmi_writel(hdmi_dev, MC_SWRSTZREQ, 0xF7);
	}
	/* CEC */
	if (cec_int) {
		hdmi_writel(hdmi_dev, IH_CEC_STAT0, cec_int);
		if (hdmi_dev->hdmi->property->feature & SUPPORT_CEC)
			rockchip_hdmiv2_cec_isr(hdmi_dev, cec_int);
	}
	/* HDCP */
	if (hdcp_int) {
		hdmi_writel(hdmi_dev, A_APIINTCLR, hdcp_int);
		rockchip_hdmiv2_hdcp_isr(hdmi_dev, hdcp_int);
	}

	/* HDCP2 */
	if (hdcp2_int) {
		hdmi_writel(hdmi_dev, HDCP2REG_STAT, hdcp2_int);
		pr_info("hdcp2_int is 0x%02x\n", hdcp2_int);
		if ((hdcp2_int & m_HDCP2_AUTH_FAIL ||
		     hdcp2_int & m_HDCP2_AUTH_LOST) &&
		    hdmi_dev->hdcp2_start) {
			pr_info("hdcp2 failed or lost\n");
			hdmi_dev->hdcp2_start();
		}
	}
	return IRQ_HANDLED;
}

irqreturn_t rockchip_hdmiv2_dev_cec_irq(int irq, void *priv)
{
	struct hdmi_dev *hdmi_dev = priv;
	char wakeup, cec_int;

	pr_info("%s\n", __func__);
	cec_int = hdmi_readl(hdmi_dev, IH_CEC_STAT0);
	if (cec_int)
		hdmi_writel(hdmi_dev, IH_CEC_STAT0, cec_int);
	hdmi_msk_reg(hdmi_dev, CEC_CTRL,
		     m_CEC_STANBY, v_CEC_STANBY(0));
	wakeup = hdmi_readl(hdmi_dev, CEC_WKUPCTRL);
	hdmi_writel(hdmi_dev, CEC_WKUPCTRL, 0x00);
	rockchip_hdmi_cec_submit_work(EVENT_RX_WAKEUP, 100, NULL);
	return IRQ_HANDLED;
}
