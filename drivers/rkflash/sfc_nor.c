/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "sfc.h"
#include "sfc_nor.h"
#include "typedef.h"

#ifdef CONFIG_RK_SFC_NOR_MTD
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include "rkflash_blk.h"
#endif

#define PRINT_SPI_CHIP_INFO		0
#define SNOR_4BIT_DATA_DETECT_EN	0
#define SNOR_STRESS_TEST_EN	0
#define NOR_PAGE_SIZE		256
#define NOR_BLOCK_SIZE		(64 * 1024)
#define NOR_SECS_BLK		(NOR_BLOCK_SIZE / 512)
#define NOR_SECS_PAGE		4

#define FEA_READ_STATUE_MASK	(0x3 << 0)
#define FEA_STATUE_MODE1	0
#define FEA_STATUE_MODE2	1
#define FEA_4BIT_READ		BIT(2)
#define FEA_4BIT_PROG		BIT(3)
#define FEA_4BYTE_ADDR		BIT(4)
#define FEA_4BYTE_ADDR_MODE	BIT(5)

struct flash_info {
	u32 id;

	u8 block_size;
	u8 sector_size;
	u8 read_cmd;
	u8 prog_cmd;

	u8 read_cmd_4;
	u8 prog_cmd_4;
	u8 sector_erase_cmd;
	u8 block_erase_cmd;

	u8 feature;
	u8 density;  /* (1 << density) sectors*/
	u8 QE_bits;
	u8 reserved2;
};

static struct flash_info spi_flash_tbl[] = {
	/* GD25Q32B */
	{0xc84016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 13, 9, 0},
	/* GD25Q64B */
	{0xc84017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0},
	/* GD25Q127C and GD25Q128C*/
	{0xc84018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0},
	/* GD25Q256B */
	{0xc84019, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1C, 16, 6, 0},
	/* GD25Q512MC */
	{0xc84020, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1C, 17, 6, 0},
	/* 25Q128FV */
	{0xef4018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0},
	/* 25Q256FV */
	{0xef4019, 128, 8, 0x13, 0x02, 0x6C, 0x32, 0x20, 0xD8, 0x3C, 16, 9, 0},
	/* XT25F128A */
	{0x207018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x00, 15, 0, 0},
	/* MX25L25635E/F */
	{0xc22019, 128, 8, 0x03, 0x02, 0x6B, 0x38, 0x20, 0xD8, 0x30, 16, 6, 0},
};

static struct flash_info *g_spi_flash_info;
typedef int (*SNOR_WRITE_STATUS)(u32 reg_index, u8 status);
struct SFNOR_DEV {
	u32	capacity;
	u8	manufacturer;
	u8	mem_type;
	u16	page_size;
	u32	blk_size;

	u8	read_cmd;
	u8	prog_cmd;
	u8	sec_erase_cmd;
	u8	blk_erase_cmd;
	u8	QE_bits;

	enum SNOR_READ_MODE  read_mode;
	enum SNOR_ADDR_MODE  addr_mode;
	enum SNOR_IO_MODE    io_mode;

	enum SFC_DATA_LINES read_lines;
	enum SFC_DATA_LINES prog_lines;

	SNOR_WRITE_STATUS write_status;
	struct mutex	lock; /* to lock this object */
#ifdef CONFIG_RK_SFC_NOR_MTD
	struct mtd_info	mtd;
	u8 *dma_buf;
#endif
};

static int snor_wait_busy(int timeout);
static struct SFNOR_DEV sfnor_dev;

static const u8 sfnor_dev_code[] = {
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x16,
	0x17,
	0x18,
	0x19
};

static const u32 sfnor_capacity[] = {
	0x20000,        /* 128k-byte */
	0x40000,        /* 256k-byte */
	0x80000,        /* 512k-byte */
	0x100000,       /* 1M-byte */
	0x200000,       /* 2M-byte */
	0x400000,       /* 4M-byte */
	0x800000,       /* 8M-byte */
	0x1000000,      /* 16M-byte */
	0x2000000       /* 32M-byte */
};

#ifdef CONFIG_RK_SFC_NOR_MTD
static struct mtd_partition nor_parts[MAX_PART_COUNT];
static inline struct SFNOR_DEV *mtd_to_sfc(struct mtd_info *mtd)
{
	return container_of(mtd, struct SFNOR_DEV, mtd);
}
#endif

static int snor_write_en(void)
{
	int ret;
	union SFCCMD_DATA     sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_WRITE_EN;

	ret = sfc_request(sfcmd.d32, 0, 0, NULL);

	return ret;
}

static int snor_reset_device(void)
{
	int ret;
	union SFCCMD_DATA sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_ENABLE_RESER;
	sfc_request(sfcmd.d32, 0, 0, NULL);

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_RESET_DEVICE;
	ret = sfc_request(sfcmd.d32, 0, 0, NULL);
	/* tRST=30us , delay 1ms here */
	mdelay(1);
	return ret;
}

static int snor_enter_4byte_mode(void)
{
	int ret;
	union SFCCMD_DATA sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_ENTER_4BYTE_MODE;

	ret = sfc_request(sfcmd.d32, 0, 0, NULL);
	return ret;
}

static int snor_read_status(u32 reg_index, u8 *status)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	u8 read_stat_cmd[] = {CMD_READ_STATUS,
				CMD_READ_STATUS2, CMD_READ_STATUS3};
	sfcmd.d32 = 0;
	sfcmd.b.cmd = read_stat_cmd[reg_index];
	sfcmd.b.datasize = 1;

	ret = sfc_request(sfcmd.d32, 0, 0, status);

	return ret;
}

static int snor_write_status2(u32 reg_index, u8 status)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	u8 status2[2];
	u8 read_index;

	status2[reg_index] = status;
	read_index = (reg_index == 0) ? 1 : 0;
	ret = snor_read_status(read_index, &status2[read_index]);
	if (ret != SFC_OK)
		return ret;

	snor_write_en();

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_WRITE_STATUS;
	sfcmd.b.datasize = 2;
	sfcmd.b.rw = SFC_WRITE;

	ret = sfc_request(sfcmd.d32, 0, 0, &status2[0]);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);    /* 10ms */

	return ret;
}

static int snor_write_status(u32 reg_index, u8 status)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	u8 write_stat_cmd[] = {CMD_WRITE_STATUS,
			       CMD_WRITE_STATUS2, CMD_WRITE_STATUS3};
	snor_write_en();
	sfcmd.d32 = 0;
	sfcmd.b.cmd = write_stat_cmd[reg_index];
	sfcmd.b.datasize = 1;
	sfcmd.b.rw = SFC_WRITE;

	ret = sfc_request(sfcmd.d32, 0, 0, &status);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);    /* 10ms */

	return ret;
}

static int snor_wait_busy(int timeout)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	u32 i, status;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_READ_STATUS;
	sfcmd.b.datasize = 1;

	for (i = 0; i < timeout; i++) {
		ret = sfc_request(sfcmd.d32, 0, 0, &status);
		if (ret != SFC_OK)
			return ret;

		if ((status & 0x01) == 0)
			return SFC_OK;

		sfc_delay(1);
	}
	PRINT_E("snor_wait_busy  error %x\n", timeout);
	return SFC_BUSY_TIMEOUT;
}

static int snor_erase(u32 addr, enum NOR_ERASE_TYPE erase_type)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	int timeout[] = {400, 2000, 40000};   /* ms */
	struct SFNOR_DEV *p_dev = &sfnor_dev;

	if (erase_type > ERASE_CHIP)
		return SFC_PARAM_ERR;

	sfcmd.d32 = 0;
	if (erase_type == ERASE_BLOCK64K)
		sfcmd.b.cmd = p_dev->blk_erase_cmd;
	else if (erase_type == ERASE_SECTOR)
		sfcmd.b.cmd = p_dev->sec_erase_cmd;
	else
		sfcmd.b.cmd = CMD_CHIP_ERASE;

	sfcmd.b.addrbits = (erase_type != ERASE_CHIP) ?
				SFC_ADDR_24BITS : SFC_ADDR_0BITS;
	if ((p_dev->addr_mode == ADDR_MODE_4BYTE) && (erase_type != ERASE_CHIP))
		sfcmd.b.addrbits = SFC_ADDR_32BITS;

	snor_write_en();

	ret = sfc_request(sfcmd.d32, 0, addr, NULL);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(timeout[erase_type] * 1000);
	return ret;
}

static int snor_prog_page(u32 addr, void *p_data, u32 size)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	union SFCCTRL_DATA sfctrl;
	struct SFNOR_DEV *p_dev = &sfnor_dev;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = p_dev->prog_cmd;
	sfcmd.b.addrbits = SFC_ADDR_24BITS;
	sfcmd.b.datasize = size;
	sfcmd.b.rw = SFC_WRITE;

	sfctrl.d32 = 0;
	sfctrl.b.datalines = p_dev->prog_lines;
	sfctrl.b.enbledma = 0;
	if (p_dev->prog_cmd == CMD_PAGE_PROG_A4)
		sfctrl.b.addrlines = SFC_4BITS_LINE;

	if (p_dev->addr_mode == ADDR_MODE_4BYTE)
		sfcmd.b.addrbits = SFC_ADDR_32BITS;

	snor_write_en();

	ret = sfc_request(sfcmd.d32, sfctrl.d32, addr, p_data);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);

	return ret;
}

static int snor_prog(u32 addr, void *p_data, u32 size)
{
	int ret = SFC_OK;
	u32 page_size, len;
	u8 *p_buf =  (u8 *)p_data;

	page_size = NOR_PAGE_SIZE;
	while (size) {
		len = page_size < size ? page_size : size;
		ret = snor_prog_page(addr, p_buf, len);
		if (ret != SFC_OK)
			return ret;

		size -= len;
		addr += len;
		p_buf += len;
	}

	return ret;
}

static int snor_enable_QE(void)
{
	int ret = SFC_OK;
	int reg_index;
	int bit_offset;
	u8 status;
	struct SFNOR_DEV *p_dev = &sfnor_dev;

	if (p_dev->manufacturer == MID_GIGADEV ||
	    p_dev->manufacturer == MID_WINBOND) {
		reg_index = p_dev->QE_bits >> 3;
		bit_offset = p_dev->QE_bits & 0x7;
		ret = snor_read_status(reg_index, &status);
		if (ret != SFC_OK)
			return ret;

		if (status & (1 << bit_offset))   /* is QE bit set */
			return SFC_OK;

		status |= (1 << bit_offset);
		return p_dev->write_status(reg_index, status);
	}

	return ret;
}

static int snor_disable_QE(void)
{
	int ret = SFC_OK;
	int reg_index;
	int bit_offset;
	u8 status;
	struct SFNOR_DEV *p_dev = &sfnor_dev;

	if (p_dev->manufacturer == MID_GIGADEV ||
	    p_dev->manufacturer == MID_WINBOND) {
		reg_index = p_dev->QE_bits >> 3;
		bit_offset = p_dev->QE_bits & 0x7;
		ret = snor_read_status(reg_index, &status);
		if (ret != SFC_OK)
			return ret;

		if (!(status & (1 << bit_offset)))
			return SFC_OK;

		status &= ~(1 << bit_offset);
		return p_dev->write_status(reg_index, status);
	}

	return ret;
}

#if (SNOR_4BIT_DATA_DETECT_EN)
static int snor_set_dlines(enum SFC_DATA_LINES lines)
{
	int ret;
	struct SFNOR_DEV *p_dev = &sfnor_dev;
	u8 read_cmd[] = {CMD_FAST_READ_X1, CMD_FAST_READ_X2, CMD_FAST_READ_X4};

	if (lines == DATA_LINES_X4) {
		ret = snor_enable_QE();
		if (ret != SFC_OK)
			return ret;
	}

	p_dev->read_lines = lines;
	p_dev->read_cmd = read_cmd[lines];

	if (p_dev->manufacturer == MID_GIGADEV ||
	    p_dev->manufacturer == MID_WINBOND ||
	    p_dev->manufacturer == MID_MACRONIX) {
		p_dev->prog_lines = (lines != DATA_LINES_X2) ?
				     lines : DATA_LINES_X1;
		if (lines == DATA_LINES_X1) {
			p_dev->prog_cmd = CMD_PAGE_PROG;
		} else {
			if (p_dev->manufacturer == MID_GIGADEV ||
			    p_dev->manufacturer == MID_WINBOND)
				p_dev->prog_cmd = CMD_PAGE_PROG_X4;
			else
				p_dev->prog_cmd = CMD_PAGE_PROG_A4;
		}
	}

	return SFC_OK;
}
#endif

static int snor_read_data(u32 addr, void *p_data, u32 size)
{
	int ret;
	union SFCCMD_DATA sfcmd;
	union SFCCTRL_DATA sfctrl;
	struct SFNOR_DEV *p_dev = &sfnor_dev;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = p_dev->read_cmd;
	sfcmd.b.datasize = size;
	sfcmd.b.addrbits = SFC_ADDR_24BITS;

	sfctrl.d32 = 0;
	sfctrl.b.datalines = p_dev->read_lines;
	if (!(size & 0x3) && size >= 4)
		sfctrl.b.enbledma = 0;

	if (p_dev->read_cmd == CMD_FAST_READ_X1 ||
	    p_dev->read_cmd == CMD_FAST_READ_X4 ||
	    p_dev->read_cmd == CMD_FAST_READ_X2 ||
	    p_dev->read_cmd == CMD_FAST_4READ_X4) {
		sfcmd.b.dummybits = 8;
	} else if (p_dev->read_cmd == CMD_FAST_READ_A4) {
		sfcmd.b.addrbits = SFC_ADDR_32BITS;
		addr = (addr << 8) | 0xFF;	/* Set M[7:0] = 0xFF */
		sfcmd.b.dummybits = 4;
		sfctrl.b.addrlines = SFC_4BITS_LINE;
	}

	if (p_dev->addr_mode == ADDR_MODE_4BYTE)
		sfcmd.b.addrbits = SFC_ADDR_32BITS;

	ret = sfc_request(sfcmd.d32, sfctrl.d32, addr, p_data);
	return ret;
}

int snor_read(u32 sec, u32 n_sec, void *p_data)
{
	int ret = SFC_OK;
	u32 addr, size, len;
	struct SFNOR_DEV *p_dev = &sfnor_dev;
	u8 *p_buf =  (u8 *)p_data;

	if ((sec + n_sec) > p_dev->capacity)
		return SFC_PARAM_ERR;

	mutex_lock(&p_dev->lock);
	addr = sec << 9;
	size = n_sec << 9;
	while (size) {
		len = size < SFC_MAX_IOSIZE ? size : SFC_MAX_IOSIZE;
		ret = snor_read_data(addr, p_buf, len);
		if (ret != SFC_OK) {
			PRINT_E("snor_read_data %x ret= %x\n", addr >> 9, ret);
			goto out;
		}

		size -= len;
		addr += len;
		p_buf += len;
	}
out:
	mutex_unlock(&p_dev->lock);

	return ret;
}

int snor_write(u32 sec, u32 n_sec, void *p_data)
{
	int ret = SFC_OK;
	u32 len, blk_size, offset;
	struct SFNOR_DEV *p_dev = &sfnor_dev;
	u8 *p_buf =  (u8 *)p_data;

	if ((sec + n_sec) > p_dev->capacity)
		return SFC_PARAM_ERR;

	mutex_lock(&p_dev->lock);
	while (n_sec) {
		if (sec < 512 || sec >= p_dev->capacity  - 512)
			blk_size = 8;
		else
			blk_size = p_dev->blk_size;

		offset = (sec & (blk_size - 1));
		if (!offset) {
			ret = snor_erase(sec << 9, (blk_size == 8) ?
				ERASE_SECTOR : ERASE_BLOCK64K);
			if (ret != SFC_OK) {
				PRINT_E("snor_erase %x ret= %x\n", sec, ret);
				goto out;
			}
		}
		len = (blk_size - offset) < n_sec ?
		      (blk_size - offset) : n_sec;
		ret = snor_prog(sec << 9, p_buf, len << 9);
		if (ret != SFC_OK) {
			PRINT_E("snor_prog %x ret= %x\n", sec, ret);
			goto out;
		}
		n_sec -= len;
		sec += len;
		p_buf += len << 9;
	}
out:
	mutex_unlock(&p_dev->lock);

	return ret;
}

int snor_read_id(u8 *data)
{
	int ret;
	union SFCCMD_DATA     sfcmd;

	sfcmd.d32 = 0;
	sfcmd.b.cmd = CMD_READ_JEDECID;
	sfcmd.b.datasize = 3;

	ret = sfc_request(sfcmd.d32, 0, 0, data);

	return ret;
}

u32 snor_get_capacity(void)
{
	struct SFNOR_DEV *p_dev = &sfnor_dev;

	return p_dev->capacity;
}

#ifdef CONFIG_RK_SFC_NOR_MTD
static int sfc_erase_mtd(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;
	struct SFNOR_DEV *p_dev = mtd_to_sfc(mtd);
	u32 addr, len;
	u32 rem;

	if ((instr->addr + instr->len) > p_dev->capacity << 9)
		return -EINVAL;

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	mutex_lock(&p_dev->lock);

	addr = instr->addr;
	len = instr->len;

	if (len == p_dev->mtd.size) {
		ret = snor_erase(0, CMD_CHIP_ERASE);
		if (ret) {
			PRINT_E("snor_erase CHIP 0x%x ret=%d\n", addr, ret);
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&p_dev->lock);
			return -EIO;
		}
	} else {
		while (len > 0) {
			ret = snor_erase(addr, ERASE_BLOCK64K);
			if (ret) {
				PRINT_E("snor_erase 0x%x ret=%d\n", addr, ret);
				instr->state = MTD_ERASE_FAILED;
				mutex_unlock(&p_dev->lock);
				return -EIO;
			}
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&p_dev->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

static int sfc_write_mtd(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	int status;
	u32 addr, size, chunk, padding;
	u32 page_align;
	struct SFNOR_DEV *p_dev = mtd_to_sfc(mtd);

	if ((to + len) > p_dev->capacity << 9)
		return -EINVAL;

	mutex_lock(&p_dev->lock);

	addr = to;
	size = len;

	while (size > 0) {
		page_align = addr & (NOR_PAGE_SIZE - 1);
		chunk = size;
		if (chunk > (NOR_PAGE_SIZE - page_align))
			chunk = NOR_PAGE_SIZE - page_align;
		memcpy(p_dev->dma_buf, buf, chunk);
		padding = 0;
		if (chunk < NOR_PAGE_SIZE) {
			/* 4 bytes algin */
			padding = ((chunk + 3) & 0xFFFC) - chunk;
			memset(p_dev->dma_buf + chunk, 0xFF, padding);
		}
		status = snor_prog_page(addr, p_dev->dma_buf, chunk + padding);
		if (status != SFC_OK) {
			PRINT_E("snor_prog_page %x ret= %d\n", addr, status);
			*retlen = len - size;
			mutex_unlock(&p_dev->lock);
			return status;
		}

		size -= chunk;
		addr += chunk;
		buf += chunk;
	}
	*retlen = len;
	mutex_unlock(&p_dev->lock);

	return 0;
}

static int sfc_read_mtd(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	u32 addr, size, chunk;
	u8 *p_buf =  (u8 *)buf;
	int ret = SFC_OK;

	struct SFNOR_DEV *p_dev = mtd_to_sfc(mtd);

	if ((from + len) > p_dev->capacity << 9)
		return -EINVAL;

	mutex_lock(&p_dev->lock);

	addr = from;
	size = len;

	while (size > 0) {
		chunk = (size < NOR_PAGE_SIZE) ? size : NOR_PAGE_SIZE;
		ret = snor_read_data(addr, p_dev->dma_buf, chunk);
		if (ret != SFC_OK) {
			PRINT_E("snor_read_data %x ret=%d\n", addr, ret);
			*retlen = len - size;
			mutex_unlock(&p_dev->lock);
			return ret;
		}
		memcpy(p_buf, p_dev->dma_buf, chunk);
		size -= chunk;
		addr += chunk;
		p_buf += chunk;
	}

	*retlen = len;
	mutex_unlock(&p_dev->lock);
	return 0;
}

static int sfc_nor_mtd_init(struct SFNOR_DEV *p_dev)
{
	int ret, i, part_num = 0;
	int capacity;
	struct STRUCT_PART_INFO *g_part;  /* size 2KB */

	capacity = p_dev->capacity;
	p_dev->mtd.name = "sfc_nor";
	p_dev->mtd.type = MTD_NORFLASH;
	p_dev->mtd.writesize = 1;
	p_dev->mtd.flags = MTD_CAP_NORFLASH;
	/* see snor_write */
	p_dev->mtd.size = capacity << 9;
	p_dev->mtd._erase = sfc_erase_mtd;
	p_dev->mtd._read = sfc_read_mtd;
	p_dev->mtd._write = sfc_write_mtd;
	p_dev->mtd.erasesize = g_spi_flash_info->block_size << 9;
	p_dev->mtd.writebufsize = NOR_PAGE_SIZE;

	p_dev->dma_buf = kmalloc(NOR_PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	if (!p_dev->dma_buf) {
		PRINT_E("kmalloc size=0x%x failed\n", NOR_PAGE_SIZE);
		ret = -ENOMEM;
		goto out;
	}

	g_part = kmalloc(sizeof(*g_part), GFP_KERNEL | GFP_DMA);
	if (!g_part) {
		ret = -ENOMEM;
		goto free_dma_buf;
	}
	part_num = 0;
	if (snor_read(0, 4, g_part) == 0) {
		if (g_part->hdr.ui_fw_tag == RK_PARTITION_TAG) {
			part_num = g_part->hdr.ui_part_entry_count;
			for (i = 0; i < part_num; i++) {
				nor_parts[i].name =
					kstrdup(g_part->part[i].sz_name,
						GFP_KERNEL);
				if (g_part->part[i].ui_pt_sz == 0xFFFFFFFF)
					g_part->part[i].ui_pt_sz = capacity -
						g_part->part[i].ui_pt_off;
				nor_parts[i].offset =
					g_part->part[i].ui_pt_off << 9;
				nor_parts[i].size =
					g_part->part[i].ui_pt_sz << 9;
				nor_parts[i].mask_flags = 0;
			}
		}
	}
	kfree(g_part);
	if (part_num == 0) {
		ret = -1;
		goto free_dma_buf;
	}
	ret = mtd_device_register(&p_dev->mtd, nor_parts, part_num);
	if (ret != 0)
		goto free_dma_buf;
	return ret;

free_dma_buf:
	kfree(p_dev->dma_buf);
out:
	return ret;
}

#endif /* CONFIG_RK_SFC_NOR_MTD */

#if (SNOR_STRESS_TEST_EN)
#define max_test_sector 64
u8 pwrite[max_test_sector * 512];
u8 pread[max_test_sector * 512];
u32 *pwrite32;
void snor_test(void)
{
	u16 i, j, loop = 0;
	u32 test_end_lba;
	u32 test_lba = 0;
	u16 test_sec_count = 1;
	u16 print_flag;

	test_end_lba = snor_get_capacity();
	pwrite32 = (u32 *)pwrite;
	for (i = 0; i < (max_test_sector * 512); i++)
		pwrite[i] = i;
	for (loop = 0; loop < 10; loop++) {
		PRINT_E("---------Test loop = %d---------\n", loop);
		PRINT_E("---------Test ftl write---------\n");
		test_sec_count = 1;
		PRINT_E("test_end_lba = %x\n", test_end_lba);
		PRINT_E("test_lba = %x\n", test_lba);
		for (test_lba = 0 + loop;
			(test_lba + test_sec_count) < test_end_lba;) {
			pwrite32[0] = test_lba;
			if (test_lba == loop)
				snor_erase((test_lba & 0xFFFFFF80) << 9,
					   ERASE_BLOCK64K);
			snor_write(test_lba, test_sec_count, pwrite);
			snor_read(test_lba, test_sec_count, pread);
			for (j = 0; j < test_sec_count * 512; j++) {
				if (pwrite[j] != pread[j]) {
					rknand_print_hex("w:",
							 pwrite,
							 4,
							 test_sec_count * 128);
					rknand_print_hex("r:",
							 pread,
							 4,
							 test_sec_count * 128);
					PRINT_E("e:r=%x, n=%x, w=%x, r=%x\n",
						test_lba,
						j,
						pwrite[j],
						pread[j]);
					while (1)
						;
					break;
				}
			}
			print_flag = test_lba & 0x1FF;
			if (print_flag < test_sec_count)
				PRINT_E("test_lba = %x\n", test_lba);
			test_lba += test_sec_count;
			test_sec_count++;
			if (test_sec_count > max_test_sector)
				test_sec_count = 1;
		}
		PRINT_E("---------Test ftl check---------\n");

		test_sec_count = 1;
		for (test_lba = 0 + loop;
			(test_lba + test_sec_count) < test_end_lba;) {
			pwrite32[0] = test_lba;
			snor_read(test_lba, test_sec_count, pread);
			print_flag = test_lba & 0x7FF;
			if (print_flag < test_sec_count)
				PRINT_E("test_lba = %x\n", test_lba);

			for (j = 0; j < test_sec_count * 512; j++) {
				if (pwrite[j] != pread[j]) {
					PRINT_E("e:r=%x, n=%x, w=%x, r=%x\n",
						test_lba,
						j,
						pwrite[j],
						pread[j]);
					/* while(1); */
					break;
				}
			}
			test_lba += test_sec_count;
			test_sec_count++;
			if (test_sec_count > max_test_sector)
				test_sec_count = 1;
		}
	}
	PRINT_E("---------Test end---------\n");
	/* while(1); */
}
#endif

#if (PRINT_SPI_CHIP_INFO)
static void snor_print_spi_chip_info(struct SFNOR_DEV *p_dev)
{
	PRINT_E("addr_mode: %x\n", p_dev->addr_mode);
	PRINT_E("read_lines: %x\n", p_dev->read_lines);
	PRINT_E("prog_lines: %x\n", p_dev->prog_lines);
	PRINT_E("read_cmd: %x\n", p_dev->read_cmd);
	PRINT_E("prog_cmd: %x\n", p_dev->prog_cmd);
	PRINT_E("blk_erase_cmd: %x\n", p_dev->blk_erase_cmd);
	PRINT_E("sec_erase_cmd: %x\n", p_dev->sec_erase_cmd);
}
#endif

static struct flash_info *snor_get_flash_info(u8 *flash_id)
{
	u32 i;
	u32 id = (flash_id[0] << 16) | (flash_id[1] << 8) | (flash_id[2] << 0);

	for (i = 0;
		i < (sizeof(spi_flash_tbl) / sizeof(struct flash_info));
		i++) {
		if (spi_flash_tbl[i].id == id)
			return &spi_flash_tbl[i];
	}
	return NULL;
}

int snor_init(void)
{
	int i;
	struct SFNOR_DEV *p_dev = &sfnor_dev;
	u8 id_byte[5];
	int err;

	memset(p_dev, 0, sizeof(struct SFNOR_DEV));
	snor_read_id(id_byte);
	PRINT_E("sfc nor id: %x %x %x\n", id_byte[0], id_byte[1], id_byte[2]);
	if (0xFF == id_byte[0] || 0x00 == id_byte[0]) {
		err = SFC_ERROR;
		goto err_out;
	}

	p_dev->manufacturer = id_byte[0];
	p_dev->mem_type = id_byte[1];

	mutex_init(&p_dev->lock);
	g_spi_flash_info = snor_get_flash_info(id_byte);
	if (g_spi_flash_info) {
		p_dev->capacity = 1 << g_spi_flash_info->density;
		p_dev->blk_size = g_spi_flash_info->block_size;
		p_dev->page_size = NOR_SECS_PAGE;
		p_dev->read_cmd = g_spi_flash_info->read_cmd;
		p_dev->prog_cmd = g_spi_flash_info->prog_cmd;
		p_dev->sec_erase_cmd = g_spi_flash_info->sector_erase_cmd;
		p_dev->blk_erase_cmd = g_spi_flash_info->block_erase_cmd;
		p_dev->prog_lines = DATA_LINES_X1;
		p_dev->read_lines = DATA_LINES_X1;
		p_dev->QE_bits = g_spi_flash_info->QE_bits;

		i = g_spi_flash_info->feature & FEA_READ_STATUE_MASK;
		if (i == 0)
			p_dev->write_status = snor_write_status;
		else
			p_dev->write_status = snor_write_status2;
		if (g_spi_flash_info->feature & FEA_4BIT_READ) {
			if (snor_enable_QE() == SFC_OK) {
				p_dev->read_lines = DATA_LINES_X4;
				p_dev->read_cmd = g_spi_flash_info->read_cmd_4;
			}
		}
		if ((g_spi_flash_info->feature & FEA_4BIT_PROG) &&
		    (p_dev->read_lines == DATA_LINES_X4)) {
			p_dev->prog_lines = DATA_LINES_X4;
			p_dev->prog_cmd = g_spi_flash_info->prog_cmd_4;
		}

		if (g_spi_flash_info->feature & FEA_4BYTE_ADDR)
			p_dev->addr_mode = ADDR_MODE_4BYTE;

		if ((g_spi_flash_info->feature & FEA_4BYTE_ADDR_MODE))
			snor_enter_4byte_mode();
#ifdef CONFIG_RK_SFC_NOR_MTD
		err = sfc_nor_mtd_init(p_dev);
		if (err)
			goto err_out;
#endif
		goto normal_out;
	}

	for (i = 0; i < sizeof(sfnor_dev_code); i++) {
		if (id_byte[2] == sfnor_dev_code[i]) {
			p_dev->capacity = sfnor_capacity[i] >> 9;
			break;
		}
	}

	if (i >= sizeof(sfnor_dev_code)) {
		err = SFC_ERROR;
		goto err_out;
	}

	p_dev->QE_bits = 9;
	p_dev->blk_size = NOR_SECS_BLK;
	p_dev->page_size = NOR_SECS_PAGE;
	p_dev->read_cmd = CMD_READ_DATA;
	p_dev->prog_cmd = CMD_PAGE_PROG;
	p_dev->sec_erase_cmd = CMD_SECTOR_ERASE;
	p_dev->blk_erase_cmd = CMD_BLOCK_ERASE;
	p_dev->write_status = snor_write_status2;
	#if (SNOR_4BIT_DATA_DETECT_EN)
	snor_set_dlines(DATA_LINES_X4);
	#endif

normal_out:
#if (PRINT_SPI_CHIP_INFO)
	snor_print_spi_chip_info(p_dev);
#endif
#if (SNOR_STRESS_TEST_EN)
	snor_test();
#endif
	return SFC_OK;

err_out:
	return err;
}

void snor_deinit(void)
{
	snor_disable_QE();
	snor_reset_device();
}

int snor_resume(void __iomem *reg_addr)
{
	return snor_init();
}
