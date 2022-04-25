// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/pci.h>
// #include <linux/preempt.h>
#include <linux/delay.h>

#include "obo_spi.h"
#include "fpga_sw.h"

u32 timeout = 300000;
u32 sleeptime = 5000;


u32 wait_till_done(void __iomem *base_addr, u32 reg, u32 bitmask, u32 *done,
			u32 timeout_us, u32 sleeptime_us)
{
	u32 i;
	u32 timeout_count = timeout_us / sleeptime_us;


	*done = 0;

	for (i = 0; i < timeout_count; i++) {
		*done = ioread32(base_addr + reg);
		if (*done & bitmask)
			break;

		usleep_range(sleeptime_us, sleeptime_us+1);
	}

	if (i >= timeout_count) {
		PRINTK(KERN_INFO, "timed out on reading reg 0x%x after %d us\n",
			reg, timeout_us);
		return -1;
	}

	return 0;
}

u32 wait_spi_busy(void __iomem *base_addr, u32 reg, u32 bitmask, u32 *done,
			u32 timeout_us, u32 sleeptime_us)
{
	u32 i;
	u32 timeout_count = timeout_us / sleeptime_us;


	*done = 0;

	for (i = 0; i < timeout_count; i++) {
		*done = ioread32(base_addr + reg);
		if ((*done & bitmask) == 0x00)
			break;

		usleep_range(sleeptime_us, sleeptime_us+1);
	}

	if (i >= timeout_count) {
		PRINTK(KERN_INFO,
			"%d us timed out in waiting spi_busy (reg 0x%x)\n",
			timeout_us, reg);
		return -1;
	}

	return 0;
}

void spi_reset_bus(void __iomem *base_addr, u8 pim, u8 rtc_inx)
{
	u32 base = PIM_BASE_ADDR + ((pim - 1) * PIM_REG_SIZE) +
		   SPI_MASTER_CSR_BASE_ADDR + (SPI_CFG_SIZE * rtc_inx);


	// Write 1 to reset the SPI controller. Auto-clear to 0.
	iowrite32(0x01, base_addr + base + SPI_RST_OFFSET_ADDR);
	udelay(1); // Need to check with DOM FPGA specs
	// iowrite32();
}

uint32_t obo_spi_read_mock(void __iomem *base_addr, u32 pim, u32 rtc_idx,
		uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
	*buf = start;

	return 0;
}

uint32_t mrvl_obo_spi_read(void __iomem *base_addr, u32 obo_idx, u32 bank,
		uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
	u32 i, done, ret;
	u32 spi_ctrl_reg, spi_stat_reg, spi_xfer_info_reg;
	u32 spi_flow_ctrl_reg, data_w_reg, data_r_reg;
	u32 u32_len = byte_len;
	uint32_t flow_ctrl_reg_val;
	u32 spi_xfer_info_reg_val;
	uint8_t total_read_loop;
	uint32_t buf32[32];
	uint32_t data_reg_32;
	u32 spi_ctrl_reg_val;


	spi_ctrl_reg = MRVL_SPI_CTRL_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);
	spi_stat_reg = MRVL_SPI_STAT_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);
	spi_xfer_info_reg = MRVL_SPI_XFER_INFO_REG +
				(obo_idx * MRVL_SPI_CFG_REG_SIZE);
	spi_flow_ctrl_reg = MRVL_SPI_FLOW_CTRL_REG +
				(obo_idx * MRVL_SPI_CFG_REG_SIZE);
	data_w_reg = MRVL_SPI_W_DATA_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);
	data_r_reg = MRVL_SPI_R_DATA_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);

	PRINTK(KERN_INFO, "%s line#%d obo_idx: %d bank: %d page: %d offset: %d len: %d\n         \
		\rbase_addr: 0x%lx spi_ctrl_reg: 0x%lx spi_stat_reg: 0x%lx\n \
		\rspi_xfer_info_reg: 0x%lx spi_flow_ctrl_reg: 0x%lx\n          \
		\rdata_w_reg: 0x%lx data_r_reg: 0x%lx\n",
		__func__, __LINE__, obo_idx, bank, page, start, byte_len,
		(unsigned long)base_addr, (unsigned long)spi_ctrl_reg,
		(unsigned long)spi_stat_reg,
		(unsigned long)spi_xfer_info_reg,
		(unsigned long)spi_flow_ctrl_reg,
		(unsigned long)data_w_reg, (unsigned long)data_r_reg);

	ret = wait_spi_busy(base_addr, spi_stat_reg,
				0x2, &done, timeout, sleeptime);
	if (ret != 0) {
		PRINTK(KERN_INFO, "%s line#%d SPI Busy wait timed out\n",
			__func__, __LINE__);
		return -1;
	}

	// SPI Transfer Addr Reg
	spi_xfer_info_reg_val = 0;
	spi_xfer_info_reg_val |= (byte_len - 1) << 24;
	spi_xfer_info_reg_val |= (bank & 0x3) << 16;
	spi_xfer_info_reg_val |= (page & 0xff) << 8;
	spi_xfer_info_reg_val |= (start & 0xff);
	PRINTK(KERN_INFO, "1. writing 0x%08x to 0x%04x", spi_xfer_info_reg_val,
			spi_xfer_info_reg);
	iowrite32(spi_xfer_info_reg_val, base_addr + spi_xfer_info_reg);
	// usleep_range(5000, 5001);

	// SPI Control Reg
	spi_ctrl_reg_val = 0;
	PRINTK(KERN_INFO, "2. writing 0x%08x to 0x%04x", spi_ctrl_reg_val,
			spi_ctrl_reg);
	iowrite32(spi_ctrl_reg_val, base_addr + spi_ctrl_reg);
	// usleep_range(5000, 5001);

	// SPI Status Reg
	PRINTK(KERN_INFO, "3. writing 0x%08x to 0x%04x", 0x01, spi_stat_reg);
	iowrite32(0x01, base_addr + spi_stat_reg);
	// usleep_range(5000, 5001);

	// Polling Check SPI Status Reg
	PRINTK(KERN_INFO, "4. polling check 0x%08x", spi_stat_reg);
	ret = wait_till_done(base_addr, spi_stat_reg,
				0x4, &done, timeout, sleeptime);
	if (ret != 0) {
		PRINTK(KERN_INFO, "%s line#%d SPI Transfer timed out\n",
			__func__, __LINE__);
		return -1;
	}

	usleep_range(5000, 5001);

	// Data Retreiving
	total_read_loop = byte_len / 4;
	memset(buf32, 0, sizeof(buf32));
	data_reg_32 = data_r_reg;

	if (byte_len % 4 != 0)
		total_read_loop += 1;

	PRINTK(KERN_INFO, "total read bytes: %d total read loops: %d\n",
			byte_len, total_read_loop);

	for (i = 0; i < total_read_loop; i++) {
		buf32[i] = ioread32(base_addr + data_reg_32);
		// buf32[i] = ioread32(base_addr + data_reg_32); //double read

		// printk(KERN_INFO "%d %lx %08x\n", i, data_reg_32, buf32[i]);
		PRINTK(KERN_INFO, "0x%04x: 0x%08x", data_reg_32, buf32[i]);
		data_reg_32 += 4;
		// usleep_range(5000, 5001);
	}
	memcpy(buf, ((uint8_t *)buf32), u32_len);

	// for returning the 2nd byte (Write Ready status) to spi_check_status
	flow_ctrl_reg_val = ioread32(base_addr + spi_flow_ctrl_reg);
	PRINTK(KERN_INFO, "flow_ctrl 0x%04x: 0x%08x\n", spi_flow_ctrl_reg,
			flow_ctrl_reg_val);
	// usleep_range(5000, 5001);

	if ((flow_ctrl_reg_val & 0x00ff0000) == 0x00)
		ret = 0;
	else {
		PRINTK(KERN_INFO, "%s#%d write_ready_byte: %02x\n",
			__func__, __LINE__, flow_ctrl_reg_val);
		ret = 1;
	}

	return ret;
}

uint32_t mrvl_obo_spi_write(void __iomem *base_addr, u32 obo_idx, u32 bank,
		uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
	u32 i, j, done, ret;
	u32 spi_ctrl_reg, spi_stat_reg, spi_xfer_info_reg;
	u32 spi_flow_ctrl_reg, data_w_reg, data_r_reg;
	u32 spi_xfer_info_reg_val;
	u32 spi_ctrl_reg_val;
	uint8_t total_write_loop;
	uint32_t data_reg_32;


	spi_ctrl_reg = MRVL_SPI_CTRL_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);
	spi_stat_reg = MRVL_SPI_STAT_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);
	spi_xfer_info_reg = MRVL_SPI_XFER_INFO_REG +
				(obo_idx * MRVL_SPI_CFG_REG_SIZE);
	spi_flow_ctrl_reg = MRVL_SPI_FLOW_CTRL_REG +
				(obo_idx * MRVL_SPI_CFG_REG_SIZE);
	data_w_reg = MRVL_SPI_W_DATA_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);
	data_r_reg = MRVL_SPI_R_DATA_REG + (obo_idx * MRVL_SPI_CFG_REG_SIZE);

	PRINTK(KERN_INFO, "%s line#%d obo_idx: %d bank: %d page: %d offset: %d len: %d\n         \
		\rbase_addr: 0x%lx spi_ctrl_reg: 0x%lx spi_stat_reg: 0x%lx\n \
		\rspi_xfer_info_reg: 0x%lx spi_flow_ctrl_reg: 0x%lx\n          \
		\rdata_w_reg: 0x%lx data_r_reg: 0x%lx\n",
		__func__, __LINE__, obo_idx, bank, page, start, byte_len,
		(unsigned long)base_addr, (unsigned long)spi_ctrl_reg,
		(unsigned long)spi_stat_reg,
		(unsigned long)spi_xfer_info_reg,
		(unsigned long)spi_flow_ctrl_reg,
		(unsigned long)data_w_reg, (unsigned long)data_r_reg);

	ret = wait_spi_busy(base_addr, spi_stat_reg,
						0x2, &done, timeout, sleeptime);
	if (ret != 0) {
		PRINTK(KERN_INFO, "%s line#%d SPI Busy wait timed out\n",
			__func__, __LINE__);
		return -1;
	}

	// SPI Transfer Addr Reg
	spi_xfer_info_reg_val = 0;
	spi_xfer_info_reg_val |= (byte_len - 1) << 24;
	spi_xfer_info_reg_val |= (bank & 0x3) << 16;
	spi_xfer_info_reg_val |= (page & 0xff) << 8;
	spi_xfer_info_reg_val |= (start & 0xff);
	iowrite32(spi_xfer_info_reg_val, base_addr + spi_xfer_info_reg);

	// SPI Control Reg
	spi_ctrl_reg_val = 1;
	iowrite32(spi_ctrl_reg_val, base_addr + spi_ctrl_reg);

	// SPI Write Data
	total_write_loop = byte_len / 4;
	data_reg_32 = data_w_reg;

	if (byte_len % 4 != 0)
		total_write_loop += 1;

	PRINTK(KERN_INFO, "total write bytes: %d total write loops: %d\n",
		byte_len, total_write_loop);

	for (i = 0; i < total_write_loop; i++) {
		u32 regis_data = 0;

		for (j = 0; j < 4; j++)
			regis_data |= *(buf++) << (8 * j);

		//printk(KERN_INFO "%d %lx %08x\n", i, data_reg_32, regis_data);
		iowrite32(regis_data, base_addr + data_reg_32);
		data_reg_32 += 4;
	}

	// SPI Status Reg
	iowrite32(0x01, base_addr + spi_stat_reg);

	// Polling Check SPI Status Reg
	ret = wait_till_done(base_addr, spi_stat_reg,
			     0x4, &done, timeout, sleeptime);
	if (ret != 0) {
		PRINTK(KERN_INFO, "%s line#%d SPI Transfer timed out\n",
			__func__, __LINE__);
		return -1;
	}

	return 0;
}

uint32_t obo_spi_read(void __iomem *base_addr, u32 pim, u32 rtc_idx,
		uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
	u32 i, done, done_bit, ret;
	u32 desc_h_reg, desc_l_reg, data_w_reg, data_r_reg, desc_l, desc_h;
	u32 pim_base = PIM_BASE_ADDR + (pim - 1) * PIM_REG_SIZE;
	u32 u32_len = byte_len;
	uint8_t write_ready_byte;
	uint8_t total_bytes;
	uint8_t total_read_loop;
	uint32_t buf32[140];
	uint32_t data_reg_32;


	desc_l_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR +
			SPI_DSC_L_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
	desc_h_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR +
			SPI_DSC_H_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
	data_w_reg = pim_base + SPI_W_DATA_REG_OFFSET_ADDR +
			(rtc_idx * SPI_DATA_SIZE);
	data_r_reg = pim_base + SPI_R_DATA_REG_OFFSET_ADDR +
			(rtc_idx * SPI_DATA_SIZE);

	PRINTK(KERN_INFO, "%s line#%d pim: %d rtc: %d page: %d offset: %d len: %d base_addr: 0x%lx pim_base: 0x%lx desc_l_reg: 0x%lx desc_h_reg: 0x%lx data_w_reg: 0x%lx data_r_reg: 0x%lx\n",
		__func__, __LINE__, pim, rtc_idx, page, start, byte_len,
		(unsigned long)base_addr, (unsigned long)pim_base,
		(unsigned long)desc_l_reg, (unsigned long)desc_h_reg,
		(unsigned long)data_w_reg, (unsigned long)data_r_reg);

	// Check the SPI controller status
	done_bit = 0x00000001;
	ret = wait_till_done(base_addr, desc_h_reg,
			     done_bit, &done, timeout, sleeptime);
	// printk(KERN_INFO "%s %d %d", __func__, __LINE__, ret);
	if (ret == -1) {
		// write 1 to clear done.
		PRINTK(KERN_INFO, "%s line#%d Write 1 to clear bit done\n",
			__func__, __LINE__);
		iowrite32(0x01, base_addr + desc_h_reg);
	}

	desc_h = 0x03;
	{ // Read transaction
		u32 data_out = 0;

		data_out |= (byte_len - 1) << 8;
		data_out |= (page & 0xff) << 16;
		data_out |= (start & 0xff) << 24;
		iowrite32(data_out, base_addr + data_w_reg);

		desc_l = 0;
		desc_l = 0x80000000
			 | 0
			 | ((byte_len + 6) << 8)
			 | 0x2
			 | 0x1;

		iowrite32(desc_h, base_addr + desc_h_reg);

		iowrite32(desc_l, base_addr + desc_l_reg);

		// Check the OBO done register.
		done_bit = 0x00000001;
		ret = wait_till_done(base_addr, desc_h_reg,
				     done_bit, &done, timeout, sleeptime);

		if (ret != 0) {
			PRINTK(KERN_INFO, "%s line#%d wait failed\n",
				__func__, __LINE__);
			return -1;
		}
	}

	// 32bits access
	total_bytes = u32_len + 6;
	total_read_loop = total_bytes / 4;
	memset(buf32, 0, sizeof(buf32));
	data_reg_32 = data_r_reg;

	if (total_bytes % 4 != 0)
		total_read_loop += 1;

	PRINTK(KERN_INFO, "total read bytes: %d total read loops: %d\n",
		total_bytes, total_read_loop);

	for (i = 0; i < total_read_loop; i++) {
		buf32[i] = ioread32(base_addr + data_reg_32);

		// printk(KERN_INFO "%d %lx %08x\n", i, data_reg_32, buf32[i]);
		data_reg_32 += 4;
	}
	memcpy(buf, ((uint8_t *)buf32)+6, u32_len);


	// for returning the 5th byte (Write Ready status) to spi_check_status
	write_ready_byte = ioread8(base_addr + data_r_reg + 5);

	if (write_ready_byte == 0x00) {
		ret = 0;
	} else {
		PRINTK(KERN_INFO, "%s#%d write_ready_byte: %02x\n",
			__func__, __LINE__, write_ready_byte);
		ret = 1;
	}

	return ret;
}

uint32_t obo_spi_write(void __iomem *base_addr, u32 pim, u32 rtc_idx,
		uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
	u32 i, j, done, done_bit, ret;
	u32 desc_h_reg, desc_l_reg, data_w_reg, data_r_reg, desc_l, desc_h;
	u32 pim_base = PIM_BASE_ADDR + (pim - 1) * PIM_REG_SIZE;
	u32 u32_len = byte_len;
	u32 data_out;


	desc_l_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR +
			SPI_DSC_L_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
	desc_h_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR +
			SPI_DSC_H_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
	data_w_reg = pim_base + SPI_W_DATA_REG_OFFSET_ADDR +
			(rtc_idx * SPI_DATA_SIZE);
	data_r_reg = pim_base + SPI_R_DATA_REG_OFFSET_ADDR +
			(rtc_idx * SPI_DATA_SIZE);

	PRINTK(KERN_INFO, "%s line#%d pim: %d rtc: %d page: %d offset: %d len: %d base_addr: 0x%lx pim_base: 0x%lx desc_l_reg: 0x%lx desc_h_reg: 0x%lx data_w_reg: 0x%lx data_r_reg: 0x%lx\n",
		__func__, __LINE__, pim, rtc_idx, page, start, byte_len,
		(unsigned long)base_addr, (unsigned long)pim_base,
		(unsigned long)desc_l_reg, (unsigned long)desc_h_reg,
		(unsigned long)data_w_reg, (unsigned long)data_r_reg);

	// Check the SPI controller status
	done_bit = 0x00000001;
	ret = wait_till_done(base_addr, desc_h_reg,
				done_bit, &done, timeout, sleeptime);
	// printk(KERN_INFO "%s %d %d", __func__, __LINE__, ret);
	if (ret == -1) {
		// write 1 to clear done.
		PRINTK(KERN_INFO, "%s line#%d Write 1 to clear bit done\n",
			__func__, __LINE__);
		iowrite32(0x01, base_addr + desc_h_reg);
	}

	desc_h = 0x03;
	{ // write transaction

		// Reset SPI bus
		spi_reset_bus(base_addr, pim, rtc_idx);

		desc_l = 0;
		desc_l = 0x80000000
			 | (1 << 30)
			 | ((u32_len + 6) << 8)
			 | 0x2
			 | 0x1;

		// Preliminary SPI Access Protocol
		data_out = 0x80;
		data_out |= (byte_len - 1) << 8;
		data_out |= (page & 0xff) << 16;
		data_out |= (start & 0xff) << 24;
		iowrite32(data_out, base_addr + data_w_reg);

		for (i = 0; i < (byte_len / 4) + 1; data_w_reg += 4, i++) {
			u32 regis_data = 0;

			for (j = 0; j < 4; j++) {
				if ((i == 0) && (j == 0)) {
					regis_data = 0;
					j = 1;
				} else {
					regis_data |= *(buf++) << (8 * j);
				}
			}
			iowrite32(regis_data, base_addr + data_w_reg + 4);
		}

		iowrite32(desc_h, base_addr + desc_h_reg);
		iowrite32(desc_l, base_addr + desc_l_reg);

		// Check the OBO done register.
		done_bit = 0x00000001;
		ret = wait_till_done(base_addr, desc_h_reg,
					done_bit, &done, timeout, sleeptime);

		if (ret != 0) {
			PRINTK(KERN_INFO, "%s line#%d wait failed\n",
				__func__, __LINE__);
			return -1;
		}
	}

	return 0;
}

uint32_t mrvl_spi_check_status(void __iomem *base_addr,
				uint32_t obo_idx, u32 bank)
{
	uint32_t offset = 0x80; // Byte 128
	uint32_t byte_len = 1;
	uint32_t page = 0xA0;
	uint8_t buf[20];
	int ret = 0;
	int i;


	for (i = 0; i < 5; i++) {
		ret = mrvl_obo_spi_read(base_addr, obo_idx, bank, page, offset,
					byte_len, buf);
		if (ret == 0) {
			ret = 0;
			break;
		} else {
			PRINTK(KERN_INFO, "%s line#%d flow_ctrl_failed\n",
					__func__, __LINE__);
		}
		usleep_range(5, 6);
	}

	return ret;
}

uint32_t spi_check_status(void __iomem *base_addr, uint32_t pim,
				uint32_t rtc_idx)
{
	uint32_t offset = 0x80; // Byte 128
	uint32_t byte_len = 1;
	uint32_t page = 0xA0;
	uint8_t buf[20];
	int ret = 0;
	int i;


	for (i = 0; i < 5; i++) {
		ret = obo_spi_read(base_addr, pim, rtc_idx, page, offset,
				   byte_len, buf);
		if (ret == 0) {
			ret = 0;
			break;
		}
		usleep_range(5, 6);
	}

	return ret;
}
