// SPDX-License-Identifier: GPL-2.0

/* ref sysfs from seastone2 with obo_spi_sysfs
 *   \--sys
 *       \--devices
 *          \--platform
 *             \--Marvell_Switch
 *                |--FPGA
 *                \--OBO
 *			\
 *			OBO1
 *			.   \_lopwr
 *			.   |_rst_l
 *			.   |_presence
 *			.   |_int_l
 *			.   |_spi_read_data
 *			.   |_spi_write_data
 *			.   |_obo_id
 *			.   |_bank
 *			.   |_page
 *			.   |_offset
 *			.   \_len
 *			.
 *			.
 *			.
 *			OBO16
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

#define VERSION "0.6.3"

#define TOTAL_OBO 16

#define CLASS_NAME "mrvl_fpga"
#define DRIVER_NAME "Marvell_Switch_FPGA"
#define FPGA_PCI_NAME "Marvell_Switch_FPGA_PCI"
#define DEVICE_NAME "mrvl_fpga_device"

#define FPGA_PCI_BAR_NUM 0

/* MISC       */

/* FPGA FRONT PANEL PORT MGMT */
#define SFF_PORT_CTRL_BASE          0x4000

#define PORT_XCVR_REGISTER_SIZE     0x1000

#define SPI_MAX_RETRY_BUSY 5

static int fpga_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void fpga_pci_remove(struct pci_dev *pdev);

static int fpga_i2c_access(struct i2c_adapter *adapter, u16 addr,
			  unsigned short flags, char rw, u8 cmd,
			  int size, union i2c_smbus_data *data);

static int marvell_switch_drv_probe(struct platform_device *pdev);
static int marvell_switch_drv_remove(struct platform_device *pdev);

static int  majorNumber;
static struct kobject *fpga;

static struct device *sff_dev = NULL;

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
	uint8_t current_page_sel_byte_for_i2cif;
};

struct marvell_switch_fpga_data {
	struct device *sff_devices[TOTAL_OBO];
	struct i2c_adapter *i2c_adapter[TOTAL_OBO];
	struct mutex fpga_lock;         // For FPGA internal lock
	unsigned long fpga_read_addr;
	uint8_t cpld1_read_addr;
	uint8_t cpld2_read_addr;
	struct obo_spi_t_cfg_t obo_spi_t_cfg[TOTAL_OBO];
};

enum PORT_TYPE {
	NONE,
	QSFP,
	SFP
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

struct each_obo_device_data {
	int portid;
	enum PORT_TYPE port_type;
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
	char clone[count+1];
	char *pclone = clone;
	char *last;

	memset(clone, 0x00, count+1);
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
	return sprintf(buf, "0x%8.8x\n", data);
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
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;

	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg[portid].obo_id);
}

static ssize_t bank_show(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;

	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg[portid].bank);
}

static ssize_t page_show(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;

	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg[portid].page);
}

static ssize_t offset_show(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;

	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg[portid].offset);
}

static ssize_t len_show(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;

	return sprintf(buf, "%d\n", fpga_data->obo_spi_t_cfg[portid].len);
}

static ssize_t obo_id_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg[portid].obo_id = val;

	return count;
}

static ssize_t bank_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg[portid].bank = val;

	return count;
}

static ssize_t page_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg[portid].page = val;

	return count;
}

static ssize_t offset_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg[portid].offset = val;

	return count;
}

static ssize_t len_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;
	long val = 0;
	int ret;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	fpga_data->obo_spi_t_cfg[portid].len = val;

	return count;
}

static ssize_t spi_read_data_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;
	uint8_t data[140] = {0};
	int output_len;
	int remaining = fpga_data->obo_spi_t_cfg[portid].len;
	int count = 0;
	uint8_t *p = data;
	uint8_t *pch, ch;
	uint32_t ret;
	int round = 1;



	mutex_lock(&fpga_data->fpga_lock);
	while (round <= SPI_MAX_RETRY_BUSY) {
		ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
					fpga_data->obo_spi_t_cfg[portid].obo_id,
					fpga_data->obo_spi_t_cfg[portid].bank);
		if (ret == 0) {
			mrvl_obo_spi_read(fpga_dev.data_base_addr,
					fpga_data->obo_spi_t_cfg[portid].obo_id,
					fpga_data->obo_spi_t_cfg[portid].bank,
					fpga_data->obo_spi_t_cfg[portid].page,
					fpga_data->obo_spi_t_cfg[portid].offset,
					fpga_data->obo_spi_t_cfg[portid].len,
					data);

			// memcpy(data, data+6, fpga_data->obo_spi_t_cfg.len);
			break;
		} else {
			PRINTK(KERN_INFO,
			"%s line#%d Cannot read value SPI .. Retry - %d\n",
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
					fpga_data->obo_spi_t_cfg[portid].offset
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
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	memset(buf_dup, 0x00, sizeof(buf_dup));
	strncpy(buf_dup, buf, size);


	PRINTK(KERN_INFO, "%s line#%d %lu spi_write_data: %s\n",
			__func__, __LINE__, size, buf_dup);

	while ((token = strsep(&p_buf_dup, " ")) != NULL) {
		byte_count++;

		if (byte_count > fpga_data->obo_spi_t_cfg[portid].len) {
			pr_err("%s line#%d total data exceed desired spi len [%d]\n",
			__func__, __LINE__, fpga_data->obo_spi_t_cfg[portid].len);
			return -1;
		}

		ret = kstrtol(token, 0, &data);
		if (ret != 0) {
			pr_err("%s line#%d invalid data [%s]\n",
					__func__, __LINE__, token);
			return -1;
		}

		// printk(KERN_INFO "%d %02x\n", byte_count, data);
		fpga_data->obo_spi_t_cfg[portid].spi_w_data[byte_count-1] = data & 0xff;
		spi_w_data[byte_count-1] = data & 0xff;
	}

	if (byte_count != fpga_data->obo_spi_t_cfg[portid].len) {
		pr_err("%s line#%d amount of data not equal to desired spi len [%d]\n",
			   __func__, __LINE__,
			   fpga_data->obo_spi_t_cfg[portid].len);
		return -1;
	}

	//
	while (round <= SPI_MAX_RETRY_BUSY) {
		ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
					fpga_data->obo_spi_t_cfg[portid].obo_id,
					fpga_data->obo_spi_t_cfg[portid].bank);
		if (ret == 0) {
			mrvl_obo_spi_write(fpga_dev.data_base_addr,
					fpga_data->obo_spi_t_cfg[portid].obo_id,
					fpga_data->obo_spi_t_cfg[portid].bank,
					fpga_data->obo_spi_t_cfg[portid].page,
					fpga_data->obo_spi_t_cfg[portid].offset,
					fpga_data->obo_spi_t_cfg[portid].len,
					spi_w_data);
			break;
		} else {
			PRINTK(KERN_INFO,
			"%s line#%d Cannot write value SPI .. Retry - %d\n",
			__func__, __LINE__, round);
		}
		usleep_range(3000, 3001);
		round++;
	}
	if (round >= SPI_MAX_RETRY_BUSY)
		return -1;

	return size;
}

static ssize_t rst_l_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_TXDIS_RST_L_CTRL_REG);

	return sprintf(buf, "%d\n", (temp32 & (0x1 << portid)) ? 1:0);
}

static ssize_t lopwr_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_LOPWR_CTRL_REG);

	return sprintf(buf, "%d\n", (temp32 & (0x1 << portid)) ? 1:0);
}

static ssize_t tx_dis_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_TXDIS_RST_L_CTRL_REG);

	return sprintf(buf, "%d\n", (temp32 & (0x1 << (portid + 16))) ? 1:0);
}

static ssize_t presence_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_INT_L_DC7A_STAT_REG);

	return sprintf(buf, "%d\n", (temp32 & (0x1 << portid)) ? 1:0);
}

static ssize_t int_l_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	u32 temp32 = 0;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_INT_L_DC7A_STAT_REG);

	return sprintf(buf, "%d\n", (temp32 & (0x1 << (portid + 16))) ? 1:0);
}

static ssize_t rst_l_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	u32 writing_val = 0;
	int ret;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_TXDIS_RST_L_CTRL_REG);

	if (val == 1)
		writing_val = temp32 | (0x1 << portid);
	else if (val == 0)
		writing_val = temp32 & ~(0x1 << portid);
	else
		return -1;

	iowrite32(writing_val,
		fpga_dev.data_base_addr + MRVL_OBO_TXDIS_RST_L_CTRL_REG);

	return count;
}

static ssize_t lopwr_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	u32 writing_val = 0;
	int ret;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_LOPWR_CTRL_REG);

	if (val == 1)
		writing_val = temp32 | (0x1 << portid);
	else if (val == 0)
		writing_val = temp32 & ~(0x1 << portid);
	else
		return -1;

	iowrite32(writing_val,
		fpga_dev.data_base_addr + MRVL_OBO_LOPWR_CTRL_REG);

	return count;
}

static ssize_t tx_dis_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	long val = 0;
	u32 temp32 = 0;
	int ret;
	u32 writing_val = 0;
	struct each_obo_device_data *dev_data = dev_get_drvdata(dev);
	unsigned int portid = dev_data->portid;


	ret = kstrtol(buf, 0, &val);
	if (ret != 0)
		return -1;

	temp32 = ioread32(fpga_dev.data_base_addr +
					MRVL_OBO_TXDIS_RST_L_CTRL_REG);

	if (val == 1)
		writing_val = temp32 | (0x1 << (portid + 16));
	else if (val == 0)
		writing_val = temp32 & ~(0x1 << (portid + 16));
	else
		return -1;

	iowrite32(writing_val,
		fpga_dev.data_base_addr + MRVL_OBO_TXDIS_RST_L_CTRL_REG);

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
static DEVICE_ATTR_RW(rst_l);
static DEVICE_ATTR_RW(lopwr);
static DEVICE_ATTR_RW(tx_dis);
static DEVICE_ATTR_RO(presence);
static DEVICE_ATTR_RO(int_l);

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
		&dev_attr_rst_l.attr,
		&dev_attr_lopwr.attr,
		&dev_attr_tx_dis.attr,
		&dev_attr_presence.attr,
		&dev_attr_int_l.attr,
		NULL,
};

static struct attribute_group obo_spi_attr_grp = {
		.attrs = obo_spi_attrs,
};

static const struct attribute_group *obo_spi_attr_grps[] = {
	&obo_spi_attr_grp,
	NULL
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
	return I2C_FUNC_I2C |
		I2C_FUNC_SMBUS_PROC_CALL |
		I2C_FUNC_SMBUS_QUICK |
		I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK;
}

struct obo_i2c_data_t {
	char calling_name[20];
};

struct i2c_dev_data {
	int portid;
	struct obo_i2c_data_t obo_i2c_data;
};

static int vi2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct i2c_dev_data *dev_data;
	unsigned int obo_id;
	// int i = 0;
	// int j = 0;
	int ret;
	uint8_t offset, req_len, write_val, current_page_sel_byte;


	dev_data = i2c_get_adapdata(adap);
	obo_id = dev_data->portid;

	PRINTK(KERN_INFO, "%s line#%d num: %d obo_id: %d\n",
				   __func__, __LINE__, num, obo_id);

	mutex_lock(&fpga_data->fpga_lock);
	if (num == 2) { /* read */
		offset = msgs[0].buf[0];
		req_len = msgs[1].len;

		PRINTK(KERN_INFO, "%s line#%d offset: 0x%02x req_len: %d\n",
			   __func__, __LINE__, offset, req_len);

		/* dummy return */
		// if (offset == 2 && req_len == 1) {
		// 	msgs[1].buf[0] = 0x08; // force return CMIS 0x0002 to 0x08
		// 	mutex_unlock(&fpga_data->fpga_lock);
		// 	return num;
		// }

		// for (i = 0; i < req_len; i++) {
		// 	PRINTK(KERN_INFO, "%s line#%d i:%d [%02x]\n",
		//        __func__, __LINE__, i, offset + i);
		// 	msgs[1].buf[i] = offset + i;
		// } /* end of dummy return */

		ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
							obo_id, 0);
		if (ret == 0) {
			if (offset < 0x80) { /* lower page */
				mrvl_obo_spi_read(fpga_dev.data_base_addr,
						obo_id,
						0,
						0,
						offset,
						req_len,
						msgs[1].buf);
			} else {  /* upper page */

				mrvl_obo_spi_read(fpga_dev.data_base_addr,
						obo_id,
						0,
						0,
						0x7f,
						1,
						&current_page_sel_byte);

				mrvl_obo_spi_read(fpga_dev.data_base_addr,
						obo_id,
						0,
						current_page_sel_byte,
						offset,
						req_len,
						msgs[1].buf);
			}
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot read value SPI\n",
				__func__, __LINE__);
			mutex_unlock(&fpga_data->fpga_lock);
			return -2;
		}
	} else if (num == 1) { /* write */
		req_len = msgs[0].len;
		offset = msgs[0].buf[0];
		write_val = msgs[0].buf[1];

		PRINTK(KERN_INFO,
		"%s line#%d: write req_len: %d offset: 0x%02x write_val: 0x%02x\n",
		__func__, __LINE__, req_len, offset, write_val);

		ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
							obo_id, 0);
		if (ret == 0) {
			if (offset < 0x80) { /* lower page */
				mrvl_obo_spi_write(fpga_dev.data_base_addr,
							obo_id,
							0,
							0,
							offset,
							1,
							&write_val);
			} else {  /* upper page */
				mrvl_obo_spi_read(fpga_dev.data_base_addr,
						obo_id,
						0,
						0,
						0x7f,
						1,
						&current_page_sel_byte);

				mrvl_obo_spi_write(fpga_dev.data_base_addr,
							obo_id,
							0,
							current_page_sel_byte,
							offset,
							1,
							&write_val);
			}
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot read value SPI\n",
				__func__, __LINE__);
			mutex_unlock(&fpga_data->fpga_lock);
			return -2;
		}



	}
	mutex_unlock(&fpga_data->fpga_lock);

	return num;
}

static const struct i2c_algorithm marvell_switch_i2c_algorithm = {
	.master_xfer = vi2c_master_xfer,
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
	unsigned int portid;


	dev_data = i2c_get_adapdata(adapter);
	ret = 0;
	error = 0;
	portid = dev_data->portid;

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
		if (size != I2C_SMBUS_BYTE_DATA &&
			size == I2C_SMBUS_BYTE &&
			size == I2C_SMBUS_I2C_BLOCK_DATA)
			return -1;

		// i2cdetect, i2cget (no data-addr specified)
		if (size == I2C_SMBUS_BYTE)
			return 0;

		// i2cget (with data-addr specified)
		if (size == I2C_SMBUS_BYTE_DATA) {
			mutex_lock(&fpga_data->fpga_lock);

			ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
							portid, 0);
			if (ret == 0) {
				if (cmd < 0x80) { /* lower page */
					mrvl_obo_spi_read(fpga_dev.data_base_addr,
							portid,
							0,
							0,
							cmd,
							1,
							&(data->byte));

					if (cmd == 0x7f) { /* page_sel byte */
						// current page_sel_byte
						fpga_data->obo_spi_t_cfg[dev_data->portid].current_page_sel_byte_for_i2cif = data->byte;
					}
				} else {  /* upper page */
					mrvl_obo_spi_read(fpga_dev.data_base_addr,
							portid,
							0,
							fpga_data->obo_spi_t_cfg[dev_data->portid].current_page_sel_byte_for_i2cif,
							cmd,
							1,
							&(data->byte));
				}
			} else {
				PRINTK(KERN_INFO,
					"%s line#%d Cannot read value SPI\n",
					__func__, __LINE__);
				mutex_unlock(&fpga_data->fpga_lock);
				return -2;
			}
			mutex_unlock(&fpga_data->fpga_lock);
		} else if (size == I2C_SMBUS_I2C_BLOCK_DATA) { // i2cdump mode=i
			data->block[0] = 32;

			mutex_lock(&fpga_data->fpga_lock);
			ret = mrvl_spi_check_status(fpga_dev.data_base_addr,
							portid, 0);
			if (ret == 0) {
				if (cmd < 0x80) { /* lower page */
					mrvl_obo_spi_read(fpga_dev.data_base_addr,
							portid,
							0,
							0,
							cmd,
							32,
							&(data->block[1]));

					if (cmd == 0x60) { /* page_sel byte */
						// current page_sel_byte
						fpga_data->obo_spi_t_cfg[dev_data->portid].current_page_sel_byte_for_i2cif = data->block[32];
					}
				} else {  /* upper page */
					mrvl_obo_spi_read(fpga_dev.data_base_addr,
							portid,
							0,
							fpga_data->obo_spi_t_cfg[dev_data->portid].current_page_sel_byte_for_i2cif,
							cmd,
							32,
							&(data->block[1]));
				}
			} else {
				PRINTK(KERN_INFO, "%s line#%d Cannot read value SPI\n", __func__, __LINE__);
				mutex_unlock(&fpga_data->fpga_lock);
				return -2;
			}
			mutex_unlock(&fpga_data->fpga_lock);

		}
	} else if (rw == I2C_SMBUS_WRITE) {
		PRINTK(KERN_INFO, "%s line#%d data[%02x]\n",
				__func__,
				__LINE__,
				data->block[0]);

		if (size != I2C_SMBUS_BYTE_DATA)
			return -1;

		ret = mrvl_spi_check_status(fpga_dev.data_base_addr, portid, 0);
		if (ret == 0) {
			if (cmd < 0x80) {
				mrvl_obo_spi_write(fpga_dev.data_base_addr,
							portid,
							0,
							0,
							cmd,
							1,
							&(data->byte));
			} else {
				mrvl_obo_spi_read(fpga_dev.data_base_addr,
							portid,
							0,
							0,
							0x7f,
							1,
							&(fpga_data->obo_spi_t_cfg[dev_data->portid].current_page_sel_byte_for_i2cif));

				mrvl_obo_spi_write(fpga_dev.data_base_addr,
							portid,
							0,
							fpga_data->obo_spi_t_cfg[dev_data->portid].current_page_sel_byte_for_i2cif,
							cmd,
							1,
							&(data->byte));
			}
		} else {
			PRINTK(KERN_INFO, "%s line#%d Cannot write value SPI\n",
				   __func__, __LINE__);
		}
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

static struct device *seastone2_sff_init(int portid)
{
	struct each_obo_device_data *new_data;
	struct device *new_device;

	new_data = kzalloc(sizeof(*new_data), GFP_KERNEL);
	if (!new_data) {
		printk(KERN_ALERT "Cannot alloc sff device data @port%d",
			portid);
		return NULL;
	}

	/* The QSFP port ID start from 1 */
	new_data->portid = portid;
	new_device = device_create_with_groups(fpgafwclass, sff_dev,
			MKDEV(0, 0), new_data, obo_spi_attr_grps,
			"OBO%d", portid+1);
	if (IS_ERR(new_device)) {
		printk(KERN_ALERT "Cannot create sff device @port%d", portid);
		kfree(new_data);
		return NULL;
	}

	return new_device;
}
enum{
	READREG,
	WRITEREG
};

struct fpga_reg_data {
	uint32_t addr;
	uint32_t value;
};

static long fpgafw_unlocked_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;
	struct fpga_reg_data data;


	mutex_lock(&fpga_data->fpga_lock);

#ifdef TEST_MODE
	static  uint32_t status_reg;
#endif
	// Switch function to read and write.
	switch (cmd) {
	case READREG:
		if (copy_from_user(&data, (void __user *)arg, sizeof(data))
		!= 0) {
			mutex_unlock(&fpga_data->fpga_lock);
			return -EFAULT;
		}
		data.value = ioread32(fpga_dev.data_base_addr+data.addr);
		if (copy_to_user((void __user *)arg, &data, sizeof(data))
		!= 0) {
			mutex_unlock(&fpga_data->fpga_lock);
			return -EFAULT;
		}
#ifdef TEST_MODE
		if (data.addr == 0x1210) {
			switch (status_reg) {
			case 0x0000:
				status_reg = 0x8000;
				break;
			case 0x8080:
				status_reg = 0x80C0;
				break;
			case 0x80C0:
				status_reg = 0x80F0;
				break;
			case 0x80F0:
				status_reg = 0x80F8;
				break;
			}
			iowrite32(status_reg, fpga_dev.data_base_addr+0x1210);
		}
#endif
		break;
	case WRITEREG:
		if (copy_from_user(&data, (void __user *)arg, sizeof(data))
		!= 0) {
			mutex_unlock(&fpga_data->fpga_lock);
			return -EFAULT;
		}
		iowrite32(data.value, fpga_dev.data_base_addr+data.addr);

#ifdef TEST_MODE
		if (data.addr == 0x1204) {
			status_reg = 0x8080;
			iowrite32(status_reg, fpga_dev.data_base_addr+0x1210);
		}
#endif

		break;
	default:
		mutex_unlock(&fpga_data->fpga_lock);
		return -EINVAL;
	}
	mutex_unlock(&fpga_data->fpga_lock);
	return ret;
}

const struct file_operations fpgafw_fops = {
	.owner      = THIS_MODULE,
	.unlocked_ioctl = fpgafw_unlocked_ioctl,
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
	majorNumber = register_chrdev(0, DEVICE_NAME, &fpgafw_fops);
	if (majorNumber < 0) {
		printk(KERN_ALERT "Failed to register a major number\n");
		return majorNumber;
	}
	printk(KERN_INFO "Device registered correctly with major number %d\n",
		majorNumber);

	fpgafwclass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(fpgafwclass)) {   // Check for error and clean up if there is
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");

		// Correct way to return an error on a pointer
		return PTR_ERR(fpgafwclass);
	}
	printk(KERN_INFO "Device class registered correctly\n");

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

	for (i = 0; i < TOTAL_OBO; i++) {
		fpga_data->obo_spi_t_cfg[i].pim = 1;
		fpga_data->obo_spi_t_cfg[i].rtc = 0;
		fpga_data->obo_spi_t_cfg[i].obo_id = 0;
		fpga_data->obo_spi_t_cfg[i].bank = 0;
		fpga_data->obo_spi_t_cfg[i].page = 0;
		fpga_data->obo_spi_t_cfg[i].offset = 0;
		fpga_data->obo_spi_t_cfg[i].len = 1;
		fpga_data->obo_spi_t_cfg[i].current_page_sel_byte_for_i2cif = 0;
		memset(fpga_data->obo_spi_t_cfg[i].spi_w_data, 0x00, 128);
	}

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

	// each OBO sysfs
	sff_dev = device_create(fpgafwclass, NULL, MKDEV(0, 0), NULL, "OBO");
	if (IS_ERR(sff_dev)) {
		printk(KERN_ERR "Failed to create sff device\n");
		sysfs_remove_group(fpga, &fpga_attr_grp);
		kobject_put(fpga);
		kzfree(fpga_data);
		return PTR_ERR(sff_dev);
	}

	ret = sysfs_create_link(&pdev->dev.kobj, &sff_dev->kobj, "OBO");
	if (ret != 0) {
		device_destroy(fpgafwclass, MKDEV(0, 0));
		sysfs_remove_group(fpga, &fpga_attr_grp);
		kobject_put(fpga);
		kzfree(fpga_data);
		return ret;
	}

	for (i = 0; i < 16; i++)
		fpga_data->sff_devices[i] = seastone2_sff_init(i);

	// virtual i2c bus create
	for (i = 0; i < TOTAL_OBO; i++)
		fpga_data->i2c_adapter[i] = i2c_master_init(pdev, i, -1);

	dev_info(&pdev->dev, "Virtual I2C buses created\n");

	return 0;
}

static void fpga_pci_remove(struct pci_dev *pdev)
{
	// fpgafw_exit();

	// remove the device
	device_destroy(fpgafwclass, MKDEV(majorNumber, 0));

	// unregister the device class
	class_unregister(fpgafwclass);

	// remove the device class
	class_destroy(fpgafwclass);

	// unregister the major number
	unregister_chrdev(majorNumber, DEVICE_NAME);
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
	struct each_obo_device_data *rem_data;
	uint8_t i;

	for (i = 0 ; i < TOTAL_OBO ; i++) {
		if (fpga_data->i2c_adapter[i] != NULL) {
			PRINTK(KERN_INFO, "i2c_adapter#%d deleted.\n", i);
			i2c_del_adapter(fpga_data->i2c_adapter[i]);
		}
	}

	for (i = 0 ; i < TOTAL_OBO ; i++) {
		if (fpga_data->sff_devices[i] != NULL) {
			rem_data = dev_get_drvdata(fpga_data->sff_devices[i]);
			device_unregister(fpga_data->sff_devices[i]);
			put_device(fpga_data->sff_devices[i]);
			kfree(rem_data);
		}
	}

	sysfs_remove_group(fpga, &fpga_attr_grp);

	kobject_put(fpga);

	device_destroy(fpgafwclass, MKDEV(0, 0));
	devm_kfree(&pdev->dev, fpga_data);
	return 0;
}

void cleanup_module(void)
{
	pr_info("%s::%s::line#%d\n", __FILE__, __func__, __LINE__);
	platform_driver_unregister(&marvell_switch_drv);
	platform_device_unregister(&marvell_switch_dev);
	pci_unregister_driver(&pci_dev_ops);
}

MODULE_AUTHOR("Raywat P. rpolpa@celestica.com");
MODULE_VERSION(VERSION);
MODULE_DESCRIPTION("Celestica mrvl sw_fpga driver");
MODULE_LICENSE("GPL");
