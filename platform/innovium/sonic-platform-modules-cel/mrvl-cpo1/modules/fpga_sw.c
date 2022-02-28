// SPDX-License-Identifier: GPL-2.0

/* ref sysfs from seastone2 with obo_spi_sysfs
 *   \--sys
 *       \--devices
 *            \--platform
 *                \--Marvell_Switch
 *                    |--FPGA
 *                    |--CPLD1
 *                    |--CPLD2
 *                    \--SFF
 *                    |   |--QSFP[1..32]
 *                    |   \--SFP[1..2]
 *                    \--OBO
 *                         |_obo_id
 *                         |_bank
 *                         |_page
 *                         |_offset
 *                         |_len
 *                         |_spi_clk_cfg
 *                         |_spi_read_data
 *                         |_spi_write_data
 *                         \_fpga_spi [ io_op pim rtc page offset len ]
 */


#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <uapi/linux/stat.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
// #include <linux/preempt.h>

#include "fpga_sw.h"
#include "obo_spi.h"

#define VERSION "0.4.1"

#define TOTAL_OBO 16

#define DRIVER_NAME "Marvell_Switch_FPGA"
#define FPGA_PCI_NAME "Marvell_Switch_FPGA_PCI"

#define FPGA_PCI_BAR_NUM 0

/* MISC       */

/* FPGA FRONT PANEL PORT MGMT */
#define SFF_PORT_CTRL_BASE          0x4000

#define PORT_XCVR_REGISTER_SIZE     0x1000

#define SPI_MAX_RETRY_BUSY 10

static int fpga_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void fpga_pci_remove(struct pci_dev *pdev);

static int fpga_i2c_access(struct i2c_adapter *adapter, u16 addr,
			  unsigned short flags, char rw, u8 cmd,
			  int size, union i2c_smbus_data *data);

static int marvell_switch_drv_probe(struct platform_device *pdev);
static int marvell_switch_drv_remove(struct platform_device *pdev);

static struct kobject *fpga;

static struct kobject *p_kobj_obo_spi = {NULL};

static struct class *fpgafwclass;

struct fpga_device {
	/* data mmio region */
	void __iomem *data_base_addr;
	resource_size_t data_mmio_start;
	resource_size_t data_mmio_len;
	uint8_t board_type_valid;
};

// OBO SPI Transaction Configuration
struct obo_spi_t_cfg_t {
	uint8_t pim;
	uint8_t rtc;
	uint8_t obo_id;
	uint8_t bank;
	uint8_t page;
	uint8_t offset;
	uint8_t len;
	uint8_t spi_w_data[128];
	uint8_t current_page_sel_byte_for_i2cif[TOTAL_OBO];
};

struct marvell_switch_fpga_data {
	struct i2c_adapter *i2c_adapter[TOTAL_OBO];
	struct mutex fpga_lock;         // For FPGA internal lock
	unsigned long fpga_read_addr;
	uint8_t cpld1_read_addr;
	uint8_t cpld2_read_addr;
	struct obo_spi_t_cfg_t obo_spi_t_cfg;
};

enum PORT_TYPE {
	NONE,
	QSFP,
	SFP
};

struct sff_device_data {
	int portid;
	enum PORT_TYPE port_type;
};

struct marvell_switch_fpga_data *fpga_data;

static struct fpga_device fpga_dev = {
	.data_base_addr = NULL,
	.data_mmio_start = 0,
	.data_mmio_len = 0,
	.board_type_valid = 0,
};

static const struct pci_device_id fpga_id_table[] = {
	// {  PCI_DEVICE(0x1d9b, 0x0011) },    /* minipack2 */
	{  PCI_DEVICE(0x10EE, 0x7021) },    /* mrvl */
	{0, }
};

static struct pci_driver pci_dev_ops = {
	.name       = FPGA_PCI_NAME,
	.probe      = fpga_pci_probe,
	.remove     = fpga_pci_remove,
	.id_table   = fpga_id_table,
};

static struct platform_driver marvell_switch_drv = {
	.probe  = marvell_switch_drv_probe,
	.remove = __exit_p(marvell_switch_drv_remove),
	.driver = {
		.name   = DRIVER_NAME,
	},
};

// I/O resource need.
static struct resource marvell_switch_resources[] = {
	{
		.start  = 0x10000000,
		.end    = 0x10001000,
		.flags  = IORESOURCE_MEM,
	},
};

static void marvell_switch_dev_release(struct device *dev)
{
	return;
}

static struct platform_device marvell_switch_dev = {
	.name           = DRIVER_NAME,
	.id             = -1,
	.num_resources  = ARRAY_SIZE(marvell_switch_resources),
	.resource       = marvell_switch_resources,
	.dev = {
					.release = marvell_switch_dev_release,
	}
};

static ssize_t getreg_show(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
{
	// read data from the address
	uint32_t data;


	data = ioread32((void __iomem *)(fpga_data->fpga_read_addr));
	return sprintf(buf, "0x%8.8x\n", data);
}

static ssize_t getreg_store(struct device *dev,
				    struct device_attribute *devattr,
				    const char *buf, size_t count)
{
	uint32_t addr;
	char *last;

	addr = (uint32_t)strtoul(buf, &last, 16);
	if (addr == 0 && buf == last)
		return -EINVAL;

	fpga_data->fpga_read_addr = (unsigned long)fpga_dev.data_base_addr+addr;
	return count;
}

static ssize_t scratch_show(struct device *dev,
				struct device_attribute *devattr,
				char *buf)
{
	return sprintf(buf, "0x%8.8x\n", ioread32(fpga_dev.data_base_addr
						  + MRVL_PCIE_SCRTCH_REG)
						  & 0xffffffff);
}

static ssize_t scratch_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	uint32_t data;
	char *last;


	data = (uint32_t)strtoul(buf, &last, 16);

	if (data == 0 && buf == last)
		return -EINVAL;

	iowrite32(data, fpga_dev.data_base_addr + MRVL_PCIE_SCRTCH_REG);
	return count;
}

static ssize_t setreg_store(struct device *dev,
				  struct device_attribute *devattr,
				  const char *buf, size_t count)
{
	//register is 4 bytes
	uint32_t addr;
	uint32_t value;
	uint32_t mode = 8;
	char *tok;
	char clone[count];
	char *pclone = clone;
	char *last;

	strcpy(clone, buf);

	mutex_lock(&fpga_data->fpga_lock);
	tok = strsep((char **)&pclone, " ");
	if (tok == NULL) {
		mutex_unlock(&fpga_data->fpga_lock);
		return -EINVAL;
	}
	addr = (uint32_t)strtoul(tok, &last, 16);
	if (addr == 0 && tok == last) {
		mutex_unlock(&fpga_data->fpga_lock);
		return -EINVAL;
	}
	tok = strsep((char **)&pclone, " ");
	if (tok == NULL) {
		mutex_unlock(&fpga_data->fpga_lock);
		return -EINVAL;
	}
	value = (uint32_t)strtoul(tok, &last, 16);
	if (value == 0 && tok == last) {
		mutex_unlock(&fpga_data->fpga_lock);
		return -EINVAL;
	}
	tok = strsep((char **)&pclone, " ");
	if (tok == NULL) {
		mode = 32;
	} else {
		mode = (uint32_t)strtoul(tok, &last, 10);
		if (mode == 0 && tok == last) {
			mutex_unlock(&fpga_data->fpga_lock);
			return -EINVAL;
		}
	}
	if (mode == 32) {
		iowrite32(value, fpga_dev.data_base_addr+addr);
	} else if (mode == 8) {
		iowrite8(value, fpga_dev.data_base_addr+addr);
	} else {
		mutex_unlock(&fpga_data->fpga_lock);
		return -EINVAL;
	}
	mutex_unlock(&fpga_data->fpga_lock);
	return count;
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	u32 data;


	mutex_lock(&fpga_data->fpga_lock);
	data = ioread32(fpga_dev.data_base_addr+IOB_REV_REG_OFFSET_ADDR);
	mutex_unlock(&fpga_data->fpga_lock);
	return sprintf(buf, "%x\n", data);
}

static ssize_t dump_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr, char *buf,
				loff_t off, size_t count)
{
	unsigned long i = 0;
	ssize_t status;
	u8 read_reg;

	if (off + count > PORT_XCVR_REGISTER_SIZE)
		return -EINVAL;

	mutex_lock(&fpga_data->fpga_lock);
	while (i < count) {
		read_reg = ioread8(fpga_dev.data_base_addr + off + i);
		buf[i++] = read_reg;
	}
	status = count;
	mutex_unlock(&fpga_data->fpga_lock);
	return status;
}

/* FPGA attributes */
static DEVICE_ATTR_RW(getreg);
static DEVICE_ATTR_RW(scratch);
static DEVICE_ATTR_WO(setreg);
static DEVICE_ATTR_RO(version);
static BIN_ATTR_RO(dump, PORT_XCVR_REGISTER_SIZE);

static struct bin_attribute *fpga_bin_attrs[] = {
		&bin_attr_dump,
		NULL,
};

static struct attribute *fpga_attrs[] = {
		&dev_attr_getreg.attr,
		&dev_attr_scratch.attr,
		&dev_attr_setreg.attr,
		&dev_attr_version.attr,
		NULL,
};

static struct attribute_group fpga_attr_grp = {
		.attrs = fpga_attrs,
		.bin_attrs = fpga_bin_attrs,
};

static ssize_t obo_id_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg.obo_id);
}

static ssize_t bank_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg.bank);
}

static ssize_t page_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg.page);
}

static ssize_t offset_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg.offset);
}

static ssize_t len_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg.len);
}

static ssize_t obo_id_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg.obo_id = val;

	return count;
}

static ssize_t bank_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg.bank = val;

	return count;
}

static ssize_t page_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg.page = val;

	return count;
}

static ssize_t offset_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg.offset = val;

	return count;
}

static ssize_t len_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg.len = val;

	return count;
}

static ssize_t mrvl_spi_read_data_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	uint8_t data[140] = {0};
	int output_len;
	int remaining = fpga_data->obo_spi_t_cfg.len;
	int count = 0;
	uint8_t *p = data;
	uint8_t *pch, ch;
	uint32_t ret;
	int round = 1;


	mutex_lock(&fpga_data->fpga_lock);
	while (round <= SPI_MAX_RETRY_BUSY) {
		ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
						fpga_data->obo_spi_t_cfg.obo_id,
						fpga_data->obo_spi_t_cfg.bank);
		if (ret == 0) {
			mrvl_obo_spi_read(fpga_dev.data_base_addr,
					fpga_data->obo_spi_t_cfg.obo_id,
					fpga_data->obo_spi_t_cfg.bank,
					fpga_data->obo_spi_t_cfg.page,
					fpga_data->obo_spi_t_cfg.offset,
					fpga_data->obo_spi_t_cfg.len,
					data);

			// memcpy(data, data+6, fpga_data->obo_spi_t_cfg.len);
			break;
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot read value SPI .. Retry - %d\n", __func__, __LINE__, round);
		}
		usleep_range(3000, 3001);
		round++;
	}
	if (round >= SPI_MAX_RETRY_BUSY) {
		mutex_unlock(&fpga_data->fpga_lock);
		return -1;
	}
	mutex_unlock(&fpga_data->fpga_lock);

	// output display
	output_len = sprintf(buf, "SPI Data:\n");

	while (remaining > 0) {
		if (!(count & 0xf)) {
			output_len += sprintf(buf + output_len, "%06x:  ",
						fpga_data->obo_spi_t_cfg.offset
						+ count);
			pch = p;
		} else if (!(count & 0x7)) {
			output_len += sprintf(buf + output_len, " ");
		}

		output_len += sprintf(buf + output_len,
					"%02x ", *(data + count));

		count++;
		p++;

		if (!(count & 0xf)) {
			output_len += sprintf(buf + output_len, " | ");

			while (pch != p - 1) {
				ch = *pch;
				output_len += sprintf(buf + output_len, "%c",
						(ch >= 32 && ch <= 126) ?
						ch : '.');
				pch++;
			}
			output_len += sprintf(buf + output_len, "\n");

		}
		remaining--;
	}
	output_len += sprintf(buf + output_len, "\n");

	return output_len;
}

static ssize_t mrvl_spi_write_data_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	char buf_dup[size+1];
	char *token = buf_dup;
	char *p_buf_dup = buf_dup;
	uint8_t byte_count = 0;
	long data;
	int ret;
	uint8_t spi_w_data[128] = {0};
	int round = 1;


	memset(buf_dup, 0x00, sizeof(buf_dup));
	strncpy(buf_dup, buf, size);


	PRINTK(KERN_INFO, "%s line#%d %lu spi_write_data: %s\n",
			__func__, __LINE__, size, buf_dup);

	while ((token = strsep(&p_buf_dup, " ")) != NULL) {
		byte_count++;

		if (byte_count > fpga_data->obo_spi_t_cfg.len) {
			pr_err("%s line#%d total data exceed desired spi len [%d]\n", __func__, __LINE__, fpga_data->obo_spi_t_cfg.len);
			return -1;
		}

		ret = kstrtol(token, 0, &data);
		if (ret != 0) {
			pr_err("%s line#%d invalid data [%s]\n",
					__func__, __LINE__, token);
			return -1;
		}

		// printk(KERN_INFO "%d %02x\n", byte_count, data);
		fpga_data->obo_spi_t_cfg.spi_w_data[byte_count-1] = data & 0xff;
		spi_w_data[byte_count-1] = data & 0xff;
	}

	if (byte_count != fpga_data->obo_spi_t_cfg.len) {
		pr_err("%s line#%d amount of data not equal to desired spi len [%d]\n",
		       __func__, __LINE__, fpga_data->obo_spi_t_cfg.len);
		return -1;
	}

	//
	while (round <= SPI_MAX_RETRY_BUSY) {
		ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
					    fpga_data->obo_spi_t_cfg.obo_id,
					    fpga_data->obo_spi_t_cfg.bank);
		if (ret == 0) {
			mrvl_obo_spi_write(fpga_dev.data_base_addr,
					   fpga_data->obo_spi_t_cfg.obo_id,
					   fpga_data->obo_spi_t_cfg.bank,
					   fpga_data->obo_spi_t_cfg.page,
					   fpga_data->obo_spi_t_cfg.offset,
					   fpga_data->obo_spi_t_cfg.len,
					   spi_w_data);
			break;
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot write value SPI .. Retry - %d\n",
			       __func__, __LINE__, round);
		}
		usleep_range(3000, 3001);
		round++;
	}
	if (round >= SPI_MAX_RETRY_BUSY)
		return -1;

	return size;
}

static ssize_t spi_read_data_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	uint8_t data[140] = {0};
	int output_len;
	int remaining = fpga_data->obo_spi_t_cfg.len;
	int count = 0;
	uint8_t *p = data;
	uint8_t *pch, ch;
	uint32_t ret;
	int round = 1;


	mutex_lock(&fpga_data->fpga_lock);
	while (round <= SPI_MAX_RETRY_BUSY) {
		ret = spi_check_status(fpga_dev.data_base_addr,
					1,
					fpga_data->obo_spi_t_cfg.obo_id);
		if (ret == 0) {
			obo_spi_read(fpga_dev.data_base_addr,
				     1,
				     fpga_data->obo_spi_t_cfg.obo_id,
				     fpga_data->obo_spi_t_cfg.page,
				     fpga_data->obo_spi_t_cfg.offset,
				     fpga_data->obo_spi_t_cfg.len,
				     data);
			// memcpy(data, data+6, fpga_data->obo_spi_t_cfg.len);
			break;
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot read value SPI .. Retry - %d\n",
			       __func__, __LINE__, round);
		}
		usleep_range(3000, 3001);
		round++;
	}
	if (round >= SPI_MAX_RETRY_BUSY) {
		mutex_unlock(&fpga_data->fpga_lock);
		return -1;
	}
	mutex_unlock(&fpga_data->fpga_lock);

	// output display
	output_len = sprintf(buf, "SPI Data:\n");

	while (remaining > 0) {
		if (!(count & 0xf)) {
			output_len += sprintf(buf + output_len, "%06x:  ",
						fpga_data->obo_spi_t_cfg.offset
						+ count);
			pch = p;
		} else if (!(count & 0x7)) {
			output_len += sprintf(buf + output_len, " ");
		}

		output_len += sprintf(buf + output_len, "%02x ",
					*(data + count));

		count++;
		p++;

		if (!(count & 0xf)) {
			output_len += sprintf(buf + output_len, " | ");

			while (pch != p - 1) {
				ch = *pch;
				output_len += sprintf(buf + output_len,
						      "%c",
						      (ch >= 32 && ch <= 126) ?
						      ch : '.');
				pch++;
			}
			output_len += sprintf(buf + output_len, "\n");

		}
		remaining--;
	}
	output_len += sprintf(buf + output_len, "\n");

	return output_len;
}

static ssize_t spi_write_data_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	char buf_dup[size+1];
	char *token = buf_dup;
	char *p_buf_dup = buf_dup;
	uint8_t byte_count = 0;
	long data;
	int ret;
	uint8_t spi_w_data[128] = {0};
	int round = 1;


	memset(buf_dup, 0x00, sizeof(buf_dup));
	strncpy(buf_dup, buf, size);


	PRINTK(KERN_INFO, "%s line#%d %lu spi_write_data: %s\n",
			__func__, __LINE__, size, buf_dup);

	while ((token = strsep(&p_buf_dup, " ")) != NULL) {
		byte_count++;

		if (byte_count > fpga_data->obo_spi_t_cfg.len) {
			pr_err("%s line#%d total data exceed desired spi len [%d]\n", __func__, __LINE__, fpga_data->obo_spi_t_cfg.len);
			return -1;
		}

		ret = kstrtol(token, 0, &data);
		if (ret != 0) {
			pr_err("%s line#%d invalid data [%s]\n",
					__func__, __LINE__, token);
			return -1;
		}

		// printk(KERN_INFO "%d %02x\n", byte_count, data);
		fpga_data->obo_spi_t_cfg.spi_w_data[byte_count-1] = data & 0xff;
		spi_w_data[byte_count-1] = data & 0xff;
	}

	if (byte_count != fpga_data->obo_spi_t_cfg.len) {
		pr_err("%s line#%d amount of data not equal to desired spi len [%d]\n", __func__, __LINE__, fpga_data->obo_spi_t_cfg.len);
		return -1;
	}

	//
	while (round <= SPI_MAX_RETRY_BUSY) {
		ret = spi_check_status(fpga_dev.data_base_addr,
					1,
					fpga_data->obo_spi_t_cfg.obo_id);
		if (ret == 0) {
			obo_spi_write(fpga_dev.data_base_addr,
					1,
					fpga_data->obo_spi_t_cfg.obo_id,
					fpga_data->obo_spi_t_cfg.page,
					fpga_data->obo_spi_t_cfg.offset,
					fpga_data->obo_spi_t_cfg.len,
					spi_w_data);
			break;
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot write value SPI .. Retry - %d\n", __func__, __LINE__, round);
		}
		usleep_range(3000, 3001);
		round++;
	}
	if (round >= SPI_MAX_RETRY_BUSY)
		return -1;


	return size;
}

static ssize_t reset_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_LPWR_RST_CTRL_REG);

	return sprintf(buf, "0x%04x\n", temp32 >> 16);
}

static ssize_t lpmod_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_LPWR_RST_CTRL_REG);

	return sprintf(buf, "0x%04x\n", temp32 & 0xffff);
}

static ssize_t tx_dis_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_TXDIS_CTRL_REG);

	return sprintf(buf, "0x%04x\n", temp32 & 0xffff);
}

static ssize_t connect_check_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_DC7A_INT_STAT_REG);

	return sprintf(buf, "0x%04x\n", temp32 & 0xffff);
}

static ssize_t interrupt_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_DC7A_INT_STAT_REG);

	return sprintf(buf, "0x%04x\n", temp32 >> 16);
}

static ssize_t reset_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_LPWR_RST_CTRL_REG);

	iowrite32((temp32 & 0x0000ffff) | ((val & 0xffff) << 16),
		fpga_dev.data_base_addr + MRVL_OBO_LPWR_RST_CTRL_REG);

	return count;
}

static ssize_t lpmod_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_LPWR_RST_CTRL_REG);

	iowrite32((temp32 & 0xffff0000) | (val & 0xffff),
		fpga_dev.data_base_addr + MRVL_OBO_LPWR_RST_CTRL_REG);

	return count;
}

static ssize_t tx_dis_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	iowrite32((val & 0xffff), fpga_dev.data_base_addr +
				MRVL_OBO_TXDIS_CTRL_REG);

	return count;
}

static ssize_t connect_check_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_DC7A_INT_STAT_REG);

	iowrite32((temp32 & 0xffff0000) | (val & 0xffff),
		fpga_dev.data_base_addr + MRVL_OBO_DC7A_INT_STAT_REG);

	return count;
}

static ssize_t interrupt_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_DC7A_INT_STAT_REG);

	iowrite32((temp32 & 0x0000ffff) | ((val & 0xffff) << 16),
		fpga_dev.data_base_addr + MRVL_OBO_DC7A_INT_STAT_REG);

	return count;
}

/* obo_spi attributes */
static DEVICE_ATTR_RW(obo_id);
static DEVICE_ATTR_RW(bank);
static DEVICE_ATTR_RW(page);
static DEVICE_ATTR_RW(offset);
static DEVICE_ATTR_RW(len);
static DEVICE_ATTR_RO(spi_read_data);
static DEVICE_ATTR_WO(spi_write_data);
static DEVICE_ATTR_RO(mrvl_spi_read_data);
static DEVICE_ATTR_WO(mrvl_spi_write_data);
static DEVICE_ATTR_RW(reset);
static DEVICE_ATTR_RW(lpmod);
static DEVICE_ATTR_RW(tx_dis);
static DEVICE_ATTR_RW(connect_check);
static DEVICE_ATTR_RW(interrupt);

static struct attribute *obo_spi_attrs[] = {
		//&dev_attr_pim.attr,
		//&dev_attr_rtc.attr,
		&dev_attr_obo_id.attr,
		&dev_attr_bank.attr,
		&dev_attr_page.attr,
		&dev_attr_offset.attr,
		&dev_attr_len.attr,
		&dev_attr_spi_read_data.attr,
		&dev_attr_spi_write_data.attr,
		&dev_attr_mrvl_spi_read_data.attr,
		&dev_attr_mrvl_spi_write_data.attr,
		&dev_attr_reset.attr,
		&dev_attr_lpmod.attr,
		&dev_attr_tx_dis.attr,
		&dev_attr_connect_check.attr,
		&dev_attr_interrupt.attr,
		NULL,
};

static struct attribute_group obo_spi_attr_grp = {
		.attrs = obo_spi_attrs,
};



/* I2C_MASTER BASE ADDR */
#define I2C_MASTER_FREQ_1           0x0100
#define I2C_MASTER_CTRL_1           0x0104
#define I2C_MASTER_STATUS_1         0x0108
#define I2C_MASTER_DATA_1           0x010c
#define I2C_MASTER_PORT_ID_1        0x0110
#define I2C_MASTER_CH_1             1
#define I2C_MASTER_CH_2             2
#define I2C_MASTER_CH_3             3
#define I2C_MASTER_CH_4             4
#define I2C_MASTER_CH_5             5
#define I2C_MASTER_CH_6             6
#define I2C_MASTER_CH_7             7
#define I2C_MASTER_CH_8             8
#define I2C_MASTER_CH_9             9
#define I2C_MASTER_CH_10            10
#define I2C_MASTER_CH_TOTAL I2C_MASTER_CH_10

static u32 fpga_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_SMBUS_QUICK  |
		I2C_FUNC_SMBUS_BYTE      |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA|
		I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm marvell_switch_i2c_algorithm = {
	.smbus_xfer = fpga_i2c_access,
	.functionality  = fpga_i2c_func,
};

struct i2c_switch {
	unsigned char master_bus;	// I2C bus number
	unsigned char switch_addr;	// PCA9548 device address,
					// 0xFF if directly connect to a bus.
	unsigned char channel;		// PCA9548 channel number.
					// If the switch_addr is 0xFF,
					// this value is ignored.
	enum PORT_TYPE port_type;	// QSFP/SFP tranceiver port type.
	char calling_name[20];		// Calling name.
};

struct obo_i2c_data_t {
	char calling_name[20];
};

struct i2c_dev_data {
	int portid;
	struct obo_i2c_data_t obo_i2c_data;
};

static struct obo_i2c_data_t v_i2c_adapter_data[] = {
	{"OBO_1"},
	{"OBO_2"},
	{"OBO_3"},
	{"OBO_4"},
	{"OBO_5"},
	{"OBO_6"},
	{"OBO_7"},
	{"OBO_8"},
	{"OBO_9"},
	{"OBO_10"},
	{"OBO_11"},
	{"OBO_12"},
	{"OBO_13"},
	{"OBO_14"},
	{"OBO_15"},
	{"OBO_16"}
};

/**
 * Wrapper of smbus_access access with PCA9548 I2C switch management.
 * This function set PCA9548 switches to the proper slave channel.
 * Only one channel among switches chip is selected during communication time.
 *
 * Note: If the bus does not have any PCA9548 on it, the switch_addr must be
 * set to 0xFF, it will use normal smbus_access function.
 */
static int fpga_i2c_access(struct i2c_adapter *adapter, u16 addr,
			  unsigned short flags, char rw, u8 cmd,
			  int size, union i2c_smbus_data *data)
{
	int error;
	struct i2c_dev_data *dev_data;
	int ret;


	dev_data = i2c_get_adapdata(adapter);
	ret = 0;
	error = 0;

	if (addr != 0x50)
		return -1;


	PRINTK(KERN_INFO, "%s line#%d addr: 0x%02x flags: 0x%x rw: (%d)%s cmd: 0x%02x size: (%d)%s\n",
			 __func__,
			 __LINE__,
			addr,
			flags,
			rw, rw == 1 ? "READ ":"WRITE",
			cmd,
			size,
			size == 0 ? "QUICK" :
			size == 1 ? "BYTE" :
			size == 2 ? "BYTE_DATA" :
			size == 3 ? "WORD_DATA" :
			size == 4 ? "PROC_CALL" :
			size == 5 ? "BLOCK_DATA" :
			size == 8 ? "I2C_BLOCK_DATA" :  "ERROR");

	PRINTK(KERN_INFO, "%s line#%d portid: (%d) name: %s\n",
			__func__,
			__LINE__,
			dev_data->portid,
			dev_data->obo_i2c_data.calling_name);

	if (rw == I2C_SMBUS_READ) {
		if (size == I2C_SMBUS_BYTE_DATA) {
			// data->block[0] = 5;
			// data->block[1] = 0xaa;
			// data->block[2] = 0x01;
			// data->block[3] = 0x02;
			// data->block[4] = 0x03;
			mutex_lock(&fpga_data->fpga_lock);

			// ret = spi_check_status(fpga_dev.data_base_addr,
			//                        1,
			//                        fpga_data->obo_spi_t_cfg.obo_id);
			if (ret == 0) {
				if (cmd < 0x80) { /* lower page */
					obo_spi_read_mock(fpga_dev.data_base_addr,
							1,
							dev_data->portid,
							0,
							cmd,
							1,
							&(data->byte));

					if (cmd == 0x7f) { /* page_sel byte */
						// current page_sel_byte
						fpga_data->obo_spi_t_cfg.current_page_sel_byte_for_i2cif[dev_data->portid] = data->byte;
					}
				} else {  /* upper page */
					obo_spi_read_mock(fpga_dev.data_base_addr,                                                       /* base_addr */
							1,                                                                             /* pim, force to 1 for marvell project */
							dev_data->portid,                                                              /* obo_idx */
							fpga_data->obo_spi_t_cfg.current_page_sel_byte_for_i2cif[dev_data->portid],    /* page */
							cmd,                                                                           /* offset */
							1,                                                                             /* len */
							&(data->byte));                                                                   /* buf */
				}
			} else {
				PRINTK(KERN_INFO, "%s line#%d Cannot read value SPI\n", __func__, __LINE__);
				mutex_unlock(&fpga_data->fpga_lock);
				return -2;
			}
			mutex_unlock(&fpga_data->fpga_lock);

		} else if (size == I2C_SMBUS_BYTE) {
			return 0;
		} else {
			return -1;
		}


		// 0. which obo to read ? (from dev_data->portid)
		// 1. read lower memory page 0x00
		// 2. extract byte 127 (page)
		// 3. read upper memory from page got in step 2
		// 4. display:
		//     4.1 all 256 bytes
		//     4.2 byte
		//     4.3 word
		//     4.4 block
	} else if (rw == I2C_SMBUS_WRITE) {
		PRINTK(KERN_INFO, "%s line#%d data[%02x]\n",
				__func__,
				__LINE__,
				data->block[0]);
	}

	return 0;
}

/**
 * Create virtual I2C bus adapter for switch devices
 * @param  pdev             platform device pointer
 * @param  portid           virtual i2c port id for switch device mapping
 * @param  bus_number_offset bus offset for virtual i2c adapter in system
 * @return                  i2c adapter.
 *
 * When bus_number_offset is -1, created adapter with dynamic bus number.
 * Otherwise create adapter at i2c bus = bus_number_offset + portid.
 */
static struct i2c_adapter *i2c_master_init(struct platform_device *pdev,
					int portid, int bus_number_offset)
{
	int error;

	struct i2c_adapter *new_adapter;
	struct i2c_dev_data *new_data;

	new_adapter = kzalloc(sizeof(*new_adapter), GFP_KERNEL);
	if (!new_adapter) {
		pr_err("Cannot alloc i2c adapter for %s",
				v_i2c_adapter_data[portid].calling_name);
		return NULL;
	}

	new_adapter->owner = THIS_MODULE;
	new_adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	new_adapter->algo  = &marvell_switch_i2c_algorithm;
	/* If the bus offset is -1, use dynamic bus number */
	if (bus_number_offset == -1)
		new_adapter->nr = -1;
	else
		new_adapter->nr = bus_number_offset + portid;

	new_data = kzalloc(sizeof(*new_data), GFP_KERNEL);
	if (!new_data) {
		pr_err("Cannot alloc i2c data for %s",
				v_i2c_adapter_data[portid].calling_name);
		kzfree(new_adapter);
		return NULL;
	}

	new_data->portid = portid;
	strcpy(new_data->obo_i2c_data.calling_name,
		v_i2c_adapter_data[portid].calling_name);

	snprintf(new_adapter->name, sizeof(new_adapter->name),
		"SMBus I2C Adapter PortID: %s",
		new_data->obo_i2c_data.calling_name);

	i2c_set_adapdata(new_adapter, new_data);
	error = i2c_add_numbered_adapter(new_adapter);
	if (error < 0) {
		dev_err(&pdev->dev, "Cannot add i2c adapter %s",
			new_data->obo_i2c_data.calling_name);
		kzfree(new_adapter);
		kzfree(new_data);
		return NULL;
	}

	return new_adapter;
};

static int fpga_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err;
	struct device *dev = &pdev->dev;
	uint32_t buff = 0;


	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "pci_enable_device probe error %d for device %s\n",
		err, pci_name(pdev));
		return err;
	}

	err = pci_request_regions(pdev, FPGA_PCI_NAME);
	if (err < 0) {
		dev_err(dev, "pci_request_regions error %d\n", err);
		goto pci_disable;
	}

	/* bar0: data mmio region */
	fpga_dev.data_mmio_start = pci_resource_start(pdev, FPGA_PCI_BAR_NUM);
	fpga_dev.data_mmio_len = pci_resource_len(pdev, FPGA_PCI_BAR_NUM);
	fpga_dev.data_base_addr = pci_iomap(pdev, FPGA_PCI_BAR_NUM, 0);
	if (!fpga_dev.data_base_addr) {
		dev_err(dev, "cannot iomap region of size %lu\n",
			(unsigned long)fpga_dev.data_mmio_len);
		goto pci_release;
	}

	// checking whether it is our desired device or not ?
	buff = ioread32(fpga_dev.data_base_addr + MRVL_FPGA_TYPE_REG);
	dev_info(dev, "board type value: 0x%08x\n", buff);
	if (buff != 0x10) {
		dev_info(dev, "unknow board type value: 0x%08x\n", buff);
		fpga_dev.board_type_valid = 0;
		goto reg_release;
	} else {
		fpga_dev.board_type_valid = 1;
	}

	// fpga_dev.board_type_valid = 1;

	dev_info(dev, "data_mmio iomap base = 0x%lx\n",
		 (unsigned long)fpga_dev.data_base_addr);

	dev_info(dev, "data_mmio_start = 0x%lx data_mmio_len = %lu\n",
		 (unsigned long)fpga_dev.data_mmio_start,
		 (unsigned long)fpga_dev.data_mmio_len);

	dev_info(dev, "FPGA PCIe driver probe OK.\n");
	dev_info(dev, "FPGA ioremap registers of size %lu\n",
			(unsigned long)fpga_dev.data_mmio_len);
	dev_info(dev, "FPGA Virtual BAR %d at %8.8lx - %8.8lx\n",
		FPGA_PCI_BAR_NUM, (unsigned long)fpga_dev.data_base_addr,
		(unsigned long)fpga_dev.data_base_addr +
		(unsigned long)fpga_dev.data_mmio_len);

	buff = ioread32(fpga_dev.data_base_addr);
	dev_info(dev, "FPGA VERSION : %8.8x\n", buff);

	// fpgafw_init();

	// sysfs create

	return 0;

reg_release:
	pci_iounmap(pdev, fpga_dev.data_base_addr);
pci_release:
	pci_release_regions(pdev);
pci_disable:
	pci_disable_device(pdev);
	return -EBUSY;
}

static int marvell_switch_drv_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = 0;
	uint8_t i;


	fpga_data = devm_kzalloc(&pdev->dev,
				sizeof(struct marvell_switch_fpga_data),
				GFP_KERNEL);

	if (!fpga_data)
		return -ENOMEM;

	// Set default read address to VERSION
	fpga_data->fpga_read_addr = (unsigned long)fpga_dev.data_base_addr +
					IOB_REV_REG_OFFSET_ADDR;
	fpga_data->cpld1_read_addr = 0x00;
	fpga_data->cpld2_read_addr = 0x00;

	fpga_data->obo_spi_t_cfg.pim = 1;
	fpga_data->obo_spi_t_cfg.rtc = 0;
	fpga_data->obo_spi_t_cfg.obo_id = 0;
	fpga_data->obo_spi_t_cfg.bank = 0;
	fpga_data->obo_spi_t_cfg.page = 0;
	fpga_data->obo_spi_t_cfg.offset = 0;
	fpga_data->obo_spi_t_cfg.len = 1;

	memset(fpga_data->obo_spi_t_cfg.current_page_sel_byte_for_i2cif, 0x00,
		TOTAL_OBO);
	memset(fpga_data->obo_spi_t_cfg.spi_w_data, 0x00, 128);

	mutex_init(&fpga_data->fpga_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		pr_info("Specified Resource Not Available...\n");
		kzfree(fpga_data);
		return -1;
	}

	fpga = kobject_create_and_add("FPGA", &pdev->dev.kobj);
	if (!fpga) {
		kzfree(fpga);
		return -ENOMEM;
	}

	ret = sysfs_create_group(fpga, &fpga_attr_grp);
	if (ret != 0) {
		pr_info("Cannot create FPGA sysfs attributes\n");
		kobject_put(fpga);
		kzfree(fpga_data);
		return ret;
	}

	p_kobj_obo_spi = kobject_create_and_add("OBO", &pdev->dev.kobj);
	if (!p_kobj_obo_spi) {
		kzfree(p_kobj_obo_spi);
		return -ENOMEM;
	}

	ret = sysfs_create_group(p_kobj_obo_spi, &obo_spi_attr_grp);
	if (ret != 0) {
		pr_info("Cannot create FPGA sysfs attributes\n");
		kobject_put(p_kobj_obo_spi);
		kzfree(fpga_data);
		return ret;
	}

	// virtual i2c bus create
	for (i = 0; i < TOTAL_OBO; i++)
		fpga_data->i2c_adapter[i] = i2c_master_init(pdev, i, -1);

	dev_info(&pdev->dev, "Virtual I2C buses created\n");

	return 0;
}

static void fpga_pci_remove(struct pci_dev *pdev)
{
	// fpgafw_exit();
	pci_iounmap(pdev, fpga_dev.data_base_addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pr_info("FPGA PCIe driver remove OK.\n");
};

int init_module(void)
{
	int rc;


	rc = pci_register_driver(&pci_dev_ops);

	pr_info("[%s] rc=%d\n", __func__, rc);

	if (rc)
		return rc;

	if (fpga_dev.board_type_valid == 0) {
		pr_info("FPGA PCIe device not found!\n");
		pci_unregister_driver(&pci_dev_ops);
		return -ENODEV;
	}

	platform_device_register(&marvell_switch_dev);
	platform_driver_register(&marvell_switch_drv);

#ifdef ENABLE_DEBUG_MSG
	pr_info("%s %s initialized, k_dbg_msg enabled.\n",
		DRIVER_NAME, VERSION);
#else
	pr_info("%s %s initialized.\n", DRIVER_NAME, VERSION);
#endif

	/*
	 * A non 0 return means init_module failed; module can't be loaded.
	 */
	return 0;
}

static int marvell_switch_drv_remove(struct platform_device *pdev)
{
	// int portid_count;
	// struct sff_device_data *rem_data;
	uint8_t i;

	for (i = 0 ; i < TOTAL_OBO ; i++) {
		if (fpga_data->i2c_adapter[i] != NULL) {
			PRINTK(KERN_INFO, "i2c_adapter#%d deleted.\n", i);
			i2c_del_adapter(fpga_data->i2c_adapter[i]);
		}
	}

	sysfs_remove_group(fpga, &fpga_attr_grp);
	// sysfs_remove_group(cpld1, &cpld1_attr_grp);
	// sysfs_remove_group(cpld2, &cpld2_attr_grp);
	// sysfs_remove_group(&sff_dev->kobj, &sff_led_test_grp);
	sysfs_remove_group(p_kobj_obo_spi, &obo_spi_attr_grp);

	kobject_put(fpga);
	// kobject_put(cpld1);
	// kobject_put(cpld2);
	kobject_put(p_kobj_obo_spi);

	device_destroy(fpgafwclass, MKDEV(0, 0));
	devm_kfree(&pdev->dev, fpga_data);
	return 0;
}

#ifdef TEST_MODE
	#define FPGA_PCI_BAR_NUM 2
#else
	#define FPGA_PCI_BAR_NUM 0
#endif

void cleanup_module(void)
{
	pr_info("%s::%s::line#%d\n", __FILE__, __func__, __LINE__);
	platform_driver_unregister(&marvell_switch_drv);
	platform_device_unregister(&marvell_switch_dev);
	pci_unregister_driver(&pci_dev_ops);
}

MODULE_AUTHOR("Raywat P. rpolpa@celestica.com");
MODULE_LICENSE("GPL");
