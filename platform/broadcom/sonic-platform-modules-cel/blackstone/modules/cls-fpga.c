/*
 * cls-fpga.c - PCI device driver for Blackstone FPGA.
 *
 * Copyright (C) 2021 Celestica Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/platform_device.h>
#include <linux/stddef.h>

#include "cls-pca954x.h"
#include "xcvr-cls.h"

#define MOD_VERSION "1.0.1"
#define DRV_NAME "cls-fpga"

#define I2C_MUX_CHANNEL(_ch, _adap_id) [_ch] = {.adap_id = _adap_id}

#define FPGA_PCIE_DEVICE_ID 0x7021
#define MMIO_BAR 0
/* Reserve some bus numbers for CPU or FPGA */
#define I2C_BUS_OFS 15

/* I2C ocore configurations */
#define OCORE_REGSHIFT 2
#define OCORE_IP_CLK_khz 62500
#define OCORE_BUS_CLK_khz 100
#define OCORE_REG_IO_WIDTH 1

/* Optical port xcvr configuration */
#define XCVR_REG_SHIFT 2
#define XCVR_NUM_PORT 34
#define XCVR_PORT_REG_SIZE 0x10

#define SILVERSTONE2_BSP 1

/* i2c_bus_config - an i2c-core resource and platform data
 *  @id - I2C bus device ID, for identification.
 *  @res - resources for an i2c-core device.
 *  @num_res - size of the resources.
 *  @pdata - a platform data of an i2c-core device.
 */
struct i2c_bus_config {
    int id;
    struct resource *res;
    ssize_t num_res;
    struct ocores_i2c_platform_data pdata;
};

/* fpga_priv - fpga private data */
struct fpga_priv {
    unsigned long base;
    int num_i2c_bus;
    struct platform_device **i2cbuses_pdev;
    struct platform_device *regio_pdev;
    struct platform_device *spiflash_pdev;
    struct platform_device *xcvr_pdev;
};

/* Switchboard FPGA attributes */
static int fpga_pci_probe(struct pci_dev *pdev);
static void fpga_pci_remove(void);

/* MISC       */
#define FPGA_VERSION 0x0000
#define FPGA_VERSION_MJ_MSK 0xff00
#define FPGA_VERSION_MN_MSK 0x00ff
#define FPGA_SCRATCH 0x0004
#define FPGA_PORT_XCVR_READY 0x000c

/* FPGA FRONT PANEL PORT MGMT */
#define SFF_PORT_CTRL_BASE 0x4000

#define PORT_XCVR_REGISTER_SIZE 0x1000

static struct class *fpgafwclass =
    NULL;  // < The device-driver class struct pointer
static struct kobject *swfpga = NULL;

#define FPGA_PCI_DEVICE_ID 0x7021
#define FPGA_PCI_BAR_NUM 0

#define CLASS_NAME "cls_fpga"

struct fpga_device {
    /* data mmio region */
    void __iomem *data_base_addr;
    resource_size_t data_mmio_start;
    resource_size_t data_mmio_len;
};

static struct fpga_device fpga_dev = {
    .data_base_addr = 0,
    .data_mmio_start = 0,
    .data_mmio_len = 0,
};

struct silverstone_fpga_data {
    struct mutex fpga_lock;  // For FPGA internal lock
    void __iomem *fpga_read_addr;
};

struct silverstone_fpga_data *fpga_data;

/**
 * Show the value of the register set by 'set_fpga_reg_address'
 * If the address is not set by 'set_fpga_reg_address' first,
 * The version register is selected by default.
 * @param  buf     register value in hextring
 * @return         number of bytes read, or an error code
 */
static ssize_t get_fpga_reg_value(struct device *dev,
                                  struct device_attribute *devattr, char *buf) {
    // read data from the address
    uint32_t data;
    data = ioread32(fpga_data->fpga_read_addr);
    return sprintf(buf, "0x%8.8x\n", data);
}
/**
 * Store the register address
 * @param  buf     address wanted to be read value of
 * @return         number of bytes stored, or an error code
 */
static ssize_t set_fpga_reg_address(struct device *dev,
                                    struct device_attribute *devattr,
                                    const char *buf, size_t count) {
    uint32_t addr;
    char *last;

    addr = (uint32_t)strtoul(buf, &last, 16);
    if (addr == 0 && buf == last) {
        return -EINVAL;
    }
    fpga_data->fpga_read_addr = fpga_dev.data_base_addr + addr;
    return count;
}
/**
 * Show value of fpga scratch register
 * @param  buf     register value in hexstring
 * @return         number of bytes read, or an error code
 */
static ssize_t get_fpga_scratch(struct device *dev,
                                struct device_attribute *devattr, char *buf) {
    return sprintf(
        buf, "0x%8.8x\n",
        ioread32(fpga_dev.data_base_addr + FPGA_SCRATCH) & 0xffffffff);
}
/**
 * Store value of fpga scratch register
 * @param  buf     scratch register value passing from user space
 * @return         number of bytes stored, or an error code
 */
static ssize_t set_fpga_scratch(struct device *dev,
                                struct device_attribute *devattr,
                                const char *buf, size_t count) {
    uint32_t data;
    char *last;
    data = (uint32_t)strtoul(buf, &last, 16);
    if (data == 0 && buf == last) {
        return -EINVAL;
    }
    iowrite32(data, fpga_dev.data_base_addr + FPGA_SCRATCH);
    return count;
}
/**
 * Store a value in a specific register address
 * @param  buf     the value and address in format '0xhhhh 0xhhhhhhhh'
 * @return         number of bytes sent by user space, or an error code
 */
static ssize_t set_fpga_reg_value(struct device *dev,
                                  struct device_attribute *devattr,
                                  const char *buf, size_t count) {
    // register are 4 bytes
    uint32_t addr;
    uint32_t value;
    uint32_t mode = 8;
    char *tok;
    char clone[count + 1];
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
        iowrite32(value, fpga_dev.data_base_addr + addr);
    } else if (mode == 8) {
        iowrite8(value, fpga_dev.data_base_addr + addr);
    } else {
        mutex_unlock(&fpga_data->fpga_lock);
        return -EINVAL;
    }
    mutex_unlock(&fpga_data->fpga_lock);
    return count;
}

/**
 * Read all FPGA XCVR register in binary mode.
 * @param  buf   Raw transceivers port startus and control register values
 * @return       number of bytes read, or an error code
 */
static ssize_t dump_read(struct file *filp, struct kobject *kobj,
                         struct bin_attribute *attr, char *buf, loff_t off,
                         size_t count) {
    unsigned long i = 0;
    ssize_t status;
    u8 read_reg;

    if (off + count > PORT_XCVR_REGISTER_SIZE) {
        return -EINVAL;
    }
    mutex_lock(&fpga_data->fpga_lock);
    while (i < count) {
        read_reg =
            ioread8(fpga_dev.data_base_addr + SFF_PORT_CTRL_BASE + off + i);
        buf[i++] = read_reg;
    }
    status = count;
    mutex_unlock(&fpga_data->fpga_lock);
    return status;
}

/**
 * Show FPGA port XCVR ready status
 * @param  buf  1 if the functin is ready, 0 if not.
 * @return      number of bytes read, or an error code
 */
static ssize_t ready_show(struct device *dev, struct device_attribute *attr,
                          char *buf) {
    u32 data;
    unsigned int REGISTER = FPGA_PORT_XCVR_READY;

    mutex_lock(&fpga_data->fpga_lock);
    data = ioread32(fpga_dev.data_base_addr + REGISTER);
    mutex_unlock(&fpga_data->fpga_lock);
    return sprintf(buf, "%d\n", (data >> 0) & 1U);
}

static DEVICE_ATTR(getreg, 0600, get_fpga_reg_value, set_fpga_reg_address);
static DEVICE_ATTR(scratch, 0600, get_fpga_scratch, set_fpga_scratch);
static DEVICE_ATTR(setreg, 0200, NULL, set_fpga_reg_value);
static DEVICE_ATTR_RO(ready);
static BIN_ATTR_RO(dump, PORT_XCVR_REGISTER_SIZE);

static struct bin_attribute *fpga_bin_attrs[] = {
    &bin_attr_dump,
    NULL,
};

static struct attribute *fpga_attrs[] = {
    &dev_attr_getreg.attr,
    &dev_attr_scratch.attr,
    &dev_attr_setreg.attr,
    &dev_attr_ready.attr,
    NULL,
};

static struct attribute_group fpga_attr_grp = {
    .attrs = fpga_attrs,
    .bin_attrs = fpga_bin_attrs,
};

/* move this on top of platform_probe() */
static int fpga_pci_probe(struct pci_dev *pdev) {
    int err;
    struct device *dev = &pdev->dev;
    uint32_t fpga_version;

    /* Skip the reqions request and mmap the resource */
    /* bar0: data mmio region */
    fpga_dev.data_mmio_start = pci_resource_start(pdev, FPGA_PCI_BAR_NUM);
    fpga_dev.data_mmio_len = pci_resource_len(pdev, FPGA_PCI_BAR_NUM);
    fpga_dev.data_base_addr =
        ioremap_nocache(fpga_dev.data_mmio_start, fpga_dev.data_mmio_len);
    if (!fpga_dev.data_base_addr) {
        dev_err(dev, "cannot iomap region of size %lu\n",
                (unsigned long)fpga_dev.data_mmio_len);
        err = PTR_ERR(fpga_dev.data_base_addr);
        goto err_exit;
    }
    dev_info(dev, "data_mmio iomap base = 0x%lx \n",
             (unsigned long)fpga_dev.data_base_addr);
    dev_info(dev, "data_mmio_start = 0x%lx data_mmio_len = %lu\n",
             (unsigned long)fpga_dev.data_mmio_start,
             (unsigned long)fpga_dev.data_mmio_len);

    printk(KERN_INFO "FPGA PCIe driver probe OK.\n");
    printk(KERN_INFO "FPGA ioremap registers of size %lu\n",
           (unsigned long)fpga_dev.data_mmio_len);
    printk(KERN_INFO "FPGA Virtual BAR %d at %8.8lx - %8.8lx\n",
           FPGA_PCI_BAR_NUM, (unsigned long)fpga_dev.data_base_addr,
           (unsigned long)(fpga_dev.data_base_addr + fpga_dev.data_mmio_len));
    printk(KERN_INFO "");
    fpga_version = ioread32(fpga_dev.data_base_addr);
    printk(KERN_INFO "FPGA VERSION : %8.8x\n", fpga_version);

    fpgafwclass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(fpgafwclass)) {
        printk(KERN_ALERT "Failed to register device class\n");
        err = PTR_ERR(fpgafwclass);
        goto mem_unmap;
    }
    return 0;

mem_unmap:
    iounmap(fpga_dev.data_base_addr);
err_exit:
    return err;
}

static void fpga_pci_remove(void) {
    iounmap(fpga_dev.data_base_addr);
    //	class_unregister(fpgafwclass);
    class_destroy(fpgafwclass);
};
/* end FPGA */

/* I2C bus speed param */
static int bus_clock_master_1 = 100;
module_param(bus_clock_master_1, int, 0660);
MODULE_PARM_DESC(bus_clock_master_1,
                 "I2C master 1 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_2 = 100;
module_param(bus_clock_master_2, int, 0660);
MODULE_PARM_DESC(bus_clock_master_2,
                 "I2C master 2 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_3 = 100;
module_param(bus_clock_master_3, int, 0660);
MODULE_PARM_DESC(bus_clock_master_3,
                 "I2C master 3 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_4 = 100;
module_param(bus_clock_master_4, int, 0660);
MODULE_PARM_DESC(bus_clock_master_4,
                 "I2C master 4 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_5 = 100;
module_param(bus_clock_master_5, int, 0660);
MODULE_PARM_DESC(bus_clock_master_5,
                 "I2C master 5 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_6 = 100;
module_param(bus_clock_master_6, int, 0660);
MODULE_PARM_DESC(bus_clock_master_6,
                 "I2C master 6 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_7 = 100;
module_param(bus_clock_master_7, int, 0660);
MODULE_PARM_DESC(bus_clock_master_7,
                 "I2C master 7 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_8 = 100;
module_param(bus_clock_master_8, int, 0660);
MODULE_PARM_DESC(bus_clock_master_8,
                 "I2C master 8 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_9 = 100;
module_param(bus_clock_master_9, int, 0660);
MODULE_PARM_DESC(bus_clock_master_9,
                 "I2C master 9 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_10 = 100;
module_param(bus_clock_master_10, int, 0660);
MODULE_PARM_DESC(bus_clock_master_10,
                 "I2C master 10 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_11 = 100;
module_param(bus_clock_master_11, int, 0660);
MODULE_PARM_DESC(bus_clock_master_11,
                 "I2C master 11 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_12 = 100;
module_param(bus_clock_master_12, int, 0660);
MODULE_PARM_DESC(bus_clock_master_12,
                 "I2C master 12 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_13 = 100;
module_param(bus_clock_master_13, int, 0660);
MODULE_PARM_DESC(bus_clock_master_13,
                 "I2C master 13 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_14 = 100;
module_param(bus_clock_master_14, int, 0660);
MODULE_PARM_DESC(bus_clock_master_14,
                 "I2C master 14 bus speed in KHz 50/80/100/200/400");

/* PCA9548 channel config on MASTER BUS 6 */
static struct pca954x_platform_mode i2c_mux_70_modes[] = {
    I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 1), I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 0),
    I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 3), I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 2),
    I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 5), I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 4),
    I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 7), I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 6),
};

static struct pca954x_platform_mode i2c_mux_71_modes[] = {
    I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 9),  I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 8),
    I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 11), I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 10),
    I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 13), I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 12),
    I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 15), I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 14),
};

/* PCA9548 channel config on MASTER BUS 12 */
static struct pca954x_platform_mode i2c_mux_72_modes[] = {
    I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 17), I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 16),
    I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 19), I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 18),
    I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 21), I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 20),
    I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 23), I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 22),
};

static struct pca954x_platform_mode i2c_mux_73_modes[] = {
    I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 25), I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 24),
    I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 27), I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 26),
    I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 29), I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 28),
    I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 31), I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 30),
};

static struct pca954x_platform_data om_muxes[] = {
    {
        .modes = i2c_mux_70_modes,
        .num_modes = ARRAY_SIZE(i2c_mux_70_modes),
    },
    {
        .modes = i2c_mux_71_modes,
        .num_modes = ARRAY_SIZE(i2c_mux_71_modes),
    },
    {
        .modes = i2c_mux_72_modes,
        .num_modes = ARRAY_SIZE(i2c_mux_72_modes),
    },
    {
        .modes = i2c_mux_73_modes,
        .num_modes = ARRAY_SIZE(i2c_mux_73_modes),
    },
};

/* Optical Module bus 6 i2c muxes info */
static struct i2c_board_info i2c_info_6[] = {
    {
        I2C_BOARD_INFO("cls_pca9548", 0x70),
        .platform_data = &om_muxes[0],
    },
    {
        I2C_BOARD_INFO("cls_pca9548", 0x71),
        .platform_data = &om_muxes[1],
    },
};

/* Optical Module bus 12 i2c muxes info */
static struct i2c_board_info i2c_info_12[] = {
    {
        I2C_BOARD_INFO("cls_pca9548", 0x72),
        .platform_data = &om_muxes[2],
    },
    {
        I2C_BOARD_INFO("cls_pca9548", 0x73),
        .platform_data = &om_muxes[3],
    },
};

/* RESOURCE SEPERATES BY FUNCTION */
/* Resource IOMEM for i2c bus 1 for FPGA_BMC_I2C1*/
static struct resource cls_i2c_res_1[] = {
    {
        .start = 0x800,
        .end = 0x81F,
        .flags = IORESOURCE_MEM,
    },
};
/* Resource IOMEM for i2c bus 2 for FPGA_BMC_I2C2*/
static struct resource cls_i2c_res_2[] = {
    {
        .start = 0x820,
        .end = 0x83F,
        .flags = IORESOURCE_MEM,
    },
};
/* Resource IOMEM for i2c bus 3 for FPGA_BMC_I2C3*/
static struct resource cls_i2c_res_3[] = {
    {
        .start = 0x840,
        .end = 0x85F,
        .flags = IORESOURCE_MEM,
    },
};
/* Resource IOMEM for i2c bus 4 for FPGA_BMC_I2C4*/
static struct resource cls_i2c_res_4[] = {
    {
        .start = 0x860,
        .end = 0x87F,
        .flags = IORESOURCE_MEM,
    },
};
/* Resource IOMEM for i2c bus 5 for FPGA_BMC_I2C5*/
static struct resource cls_i2c_res_5[] = {
    {
        .start = 0x880,
        .end = 0x89F,
        .flags = IORESOURCE_MEM,
    },
};
/* Resource IOMEM for i2c bus 6 for FPGA_SW_PORT_I2C0*/
static struct resource cls_i2c_res_6[] = {
    {
        .start = 0x8A0,
        .end = 0x8BF,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 7 for FPGA_BMC_I2C7*/
static struct resource cls_i2c_res_7[] = {
    {
        .start = 0x8C0,
        .end = 0x8DF,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 8 for FPGA_BMC_I2C8*/
static struct resource cls_i2c_res_8[] = {
    {
        .start = 0x8E0,
        .end = 0x8FF,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 9 for FPGA_BMC_I2C9*/
static struct resource cls_i2c_res_9[] = {
    {
        .start = 0x900,
        .end = 0x91F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 10 for MISC_CPLD1_I2C*/
static struct resource cls_i2c_res_10[] = {
    {
        .start = 0x920,
        .end = 0x93F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 11 for MISC_CPLD2_I2C*/
static struct resource cls_i2c_res_11[] = {
    {
        .start = 0x940,
        .end = 0x95F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 12 for FPGA_SW_PORT_I2C1*/
static struct resource cls_i2c_res_12[] = {
    {
        .start = 0x960,
        .end = 0x97F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 13 for FPGA_SFPP_0_I2C*/
static struct resource cls_i2c_res_13[] = {
    {
        .start = 0x980,
        .end = 0x99F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 14 for FPGA_SFPP_1_I2C*/
static struct resource cls_i2c_res_14[] = {
    {
        .start = 0x9A0,
        .end = 0x9BF,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for reg access */
static struct resource reg_io_res[] = {
    {
        .start = 0x00,
        .end = 0xFF,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for spi flash firmware upgrade */
static struct resource spi_flash_res[] = {
    {
        .start = 0x1200,
        .end = 0x121F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for front panel XCVR */
static struct resource xcvr_res[] = {
    {
        .start = 0x4000,
        .end = 0x421F,
        .flags = IORESOURCE_MEM,
    },
};

/* if have i2c-mux pca9548, .num_devices=ARRAY_SIZE(i2c_info_X); .devices =
 * i2c_info_X. if not, .num_devices = 0; .devices = NULL
 *
 * Notes: Some FPGA_I2C_Master buses are shared with BMC,
 * 		these buses need to be uninitialized because it interfere the
 * BMC activity
 */
static struct i2c_bus_config i2c_bus_configs[] = {
    {
        .id = 1,
        .res = cls_i2c_res_1,
        .num_res = ARRAY_SIZE(cls_i2c_res_1),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 2,
        .res = cls_i2c_res_2,
        .num_res = ARRAY_SIZE(cls_i2c_res_2),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 3,
        .res = cls_i2c_res_3,
        .num_res = ARRAY_SIZE(cls_i2c_res_3),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 4,
        .res = cls_i2c_res_4,
        .num_res = ARRAY_SIZE(cls_i2c_res_4),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 5,
        .res = cls_i2c_res_5,
        .num_res = ARRAY_SIZE(cls_i2c_res_5),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 6,
        .res = cls_i2c_res_6,
        .num_res = ARRAY_SIZE(cls_i2c_res_6),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = ARRAY_SIZE(i2c_info_6),
                .devices = i2c_info_6,
            },
    },
    {
        .id = 7,
        .res = cls_i2c_res_7,
        .num_res = ARRAY_SIZE(cls_i2c_res_7),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 8,
        .res = cls_i2c_res_8,
        .num_res = ARRAY_SIZE(cls_i2c_res_8),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 9,
        .res = cls_i2c_res_9,
        .num_res = ARRAY_SIZE(cls_i2c_res_9),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 10,
        .res = cls_i2c_res_10,
        .num_res = ARRAY_SIZE(cls_i2c_res_10),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 11,
        .res = cls_i2c_res_11,
        .num_res = ARRAY_SIZE(cls_i2c_res_11),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 12,
        .res = cls_i2c_res_12,
        .num_res = ARRAY_SIZE(cls_i2c_res_12),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = ARRAY_SIZE(i2c_info_12),
                .devices = i2c_info_12,
            },
    },
    {
        .id = 13,
        .res = cls_i2c_res_13,
        .num_res = ARRAY_SIZE(cls_i2c_res_13),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
    {
        .id = 14,
        .res = cls_i2c_res_14,
        .num_res = ARRAY_SIZE(cls_i2c_res_14),
        .pdata =
            {
                .reg_shift = OCORE_REGSHIFT,
                .reg_io_width = OCORE_REG_IO_WIDTH,
                .clock_khz = OCORE_IP_CLK_khz,
                .bus_khz = OCORE_BUS_CLK_khz,
                .big_endian = false,
                .num_devices = 0,
                .devices = NULL,
            },
    },
};

/* xcvr front panel mapping */

static struct port_info front_panel_ports[] = {
    {"QSFPDD1", 1, QSFP},   {"QSFPDD2", 2, QSFP},   {"QSFPDD3", 3, QSFP},
    {"QSFPDD4", 4, QSFP},   {"QSFPDD5", 5, QSFP},   {"QSFPDD6", 6, QSFP},
    {"QSFPDD7", 7, QSFP},   {"QSFPDD8", 8, QSFP},   {"QSFPDD9", 9, QSFP},
    {"QSFPDD10", 10, QSFP}, {"QSFPDD11", 11, QSFP}, {"QSFPDD12", 12, QSFP},
    {"QSFPDD13", 13, QSFP}, {"QSFPDD14", 14, QSFP}, {"QSFPDD15", 15, QSFP},
    {"QSFPDD16", 16, QSFP}, {"QSFPDD17", 17, QSFP}, {"QSFPDD18", 18, QSFP},
    {"QSFPDD19", 19, QSFP}, {"QSFPDD20", 20, QSFP}, {"QSFPDD21", 21, QSFP},
    {"QSFPDD22", 22, QSFP}, {"QSFPDD23", 23, QSFP}, {"QSFPDD24", 24, QSFP},
    {"QSFPDD25", 25, QSFP}, {"QSFPDD26", 26, QSFP}, {"QSFPDD27", 27, QSFP},
    {"QSFPDD28", 28, QSFP}, {"QSFPDD29", 29, QSFP}, {"QSFPDD30", 30, QSFP},
    {"QSFPDD31", 31, QSFP}, {"QSFPDD32", 32, QSFP}, {"SFP+1", 33, SFP},
    {"SFP+2", 34, SFP},
    /* END OF LIST */
};
static struct cls_xcvr_platform_data xcvr_data = {
    .port_reg_size = 0x10,
    .num_ports = ARRAY_SIZE(front_panel_ports),
    .devices = front_panel_ports,
};

// TODO: Add a platform configuration struct, and use probe as a factory,
//	 so xcvr, fwupgrade device can configured as options.

static int cls_fpga_probe(struct pci_dev *dev, const struct pci_device_id *id) {
    int ret = 0;
    struct fpga_priv *priv;
    struct platform_device **i2cbuses_pdev;
    struct platform_device *regio_pdev;
    struct platform_device *xcvr_pdev;
    unsigned long rstart;
    int num_i2c_bus, i;
    int err;

    err = pci_enable_device(dev);
    if (err) {
        dev_err(&dev->dev, "Failed to enable PCI device\n");
        goto err_exit;
    }

    if (dev) {
        err = fpga_pci_probe(dev);
    } else {
        ret = -ENODEV;
        goto err_exit;
    }
    if (0 != err) {
        dev_err(&dev->dev, "Failed to do fpga pci probe\n");
        goto err_disable_device;
    }

    fpga_data = devm_kzalloc(&dev->dev, sizeof(struct silverstone_fpga_data),
                             GFP_KERNEL);
    if (!fpga_data) {
        ret = -ENOMEM;
        goto err_disable_device;
    }

    /* Check for valid MMIO address */
    rstart = pci_resource_start(dev, MMIO_BAR);
    if (!rstart) {
        dev_err(&dev->dev,
                "Switchboard base address uninitialized, "
                "check FPGA\n");
        err = -ENODEV;
        goto err_disable_device;
    }

    dev_dbg(&dev->dev, "BAR%d res: 0x%lx-0x%llx\n", MMIO_BAR, rstart,
            pci_resource_end(dev, MMIO_BAR));

    priv = devm_kzalloc(&dev->dev, sizeof(struct fpga_priv), GFP_KERNEL);
    if (!priv) {
        err = -ENOMEM;
        goto err_disable_device;
    }

    pci_set_drvdata(dev, priv);

    swfpga = kobject_create_and_add("FPGA4SW", &dev->dev.kobj);
    if (!swfpga) {
        ret = -ENOMEM;
        goto err_disable_device;
    }

    ret = sysfs_create_group(swfpga, &fpga_attr_grp);
    if (ret != 0) {
        printk(KERN_ERR "Cannot create FPGA sysfs attributes\n");
        goto err_remove_fpga;
    }

    num_i2c_bus = ARRAY_SIZE(i2c_bus_configs);
    i2cbuses_pdev = devm_kzalloc(
        &dev->dev, num_i2c_bus * sizeof(struct platform_device *), GFP_KERNEL);

    reg_io_res[0].start += rstart;
    reg_io_res[0].end += rstart;

    xcvr_res[0].start += rstart;
    xcvr_res[0].end += rstart;

    regio_pdev = platform_device_register_resndata(
        &dev->dev, "cls-swbrd-io", -1, reg_io_res, ARRAY_SIZE(reg_io_res), NULL,
        0);

    if (IS_ERR(regio_pdev)) {
        dev_err(&dev->dev, "Failed to register cls-swbrd-io\n");
        err = PTR_ERR(regio_pdev);
        goto err_remove_grp_fpga;
    }

    xcvr_pdev = platform_device_register_resndata(
        NULL, "cls-xcvr", -1, xcvr_res, ARRAY_SIZE(xcvr_res), &xcvr_data,
        sizeof(xcvr_data));

    if (IS_ERR(xcvr_pdev)) {
        dev_err(&dev->dev, "Failed to register xcvr node\n");
        err = PTR_ERR(xcvr_pdev);
        goto err_unregister_regio;
    }

    for (i = 0; i < num_i2c_bus; i++) {
        i2c_bus_configs[i].res[0].start += rstart;
        i2c_bus_configs[i].res[0].end += rstart;

        printk("start %x ... end %x\n", i2c_bus_configs[i].res[0].start,
               i2c_bus_configs[i].res[0].end);

        switch (i2c_bus_configs[i].id) {
            case 1:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_1;
                break;
            case 2:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_2;
                break;
            case 3:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_3;
                break;
            case 4:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_4;
                break;
            case 5:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_5;
                break;
            case 6:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_6;
                break;
            case 7:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_7;
                break;
            case 8:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_8;
                break;
            case 9:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_9;
                break;
            case 10:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_10;
                break;
            case 11:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_11;
                break;
            case 12:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_12;
                break;
            case 13:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_13;
                break;
            case 14:
                i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_14;
                break;
            default:
                i2c_bus_configs[i].pdata.bus_khz = OCORE_BUS_CLK_khz;
        }

        dev_dbg(&dev->dev, "i2c-bus.%d: 0x%llx - 0x%llx\n",
                i2c_bus_configs[i].id, i2c_bus_configs[i].res[0].start,
                i2c_bus_configs[i].res[0].end);

        i2cbuses_pdev[i] = platform_device_register_resndata(
            &dev->dev, "ocores-i2c",  //"i2c-ocores","cls-ocores-i2c"
            i2c_bus_configs[i].id, i2c_bus_configs[i].res,
            i2c_bus_configs[i].num_res, &i2c_bus_configs[i].pdata,
            sizeof(i2c_bus_configs[i].pdata));

        if (IS_ERR(i2cbuses_pdev[i])) {
            dev_err(&dev->dev, "Failed to register ocores-i2c.%d\n",
                    i2c_bus_configs[i].id);
            err = PTR_ERR(i2cbuses_pdev[i]);
            goto err_unregister_ocore;
        }
    }

    priv->base = rstart;
    priv->num_i2c_bus = num_i2c_bus;
    priv->i2cbuses_pdev = i2cbuses_pdev;
    priv->regio_pdev = regio_pdev;
    priv->xcvr_pdev = xcvr_pdev;

    // Set default read address to VERSION
    fpga_data->fpga_read_addr = fpga_dev.data_base_addr + FPGA_VERSION;
    mutex_init(&fpga_data->fpga_lock);

    return 0;

err_unregister_ocore:
    for (i = 0; i < num_i2c_bus; i++) {
        if (priv->i2cbuses_pdev[i]) {
            platform_device_unregister(priv->i2cbuses_pdev[i]);
        }
    }
err_unregister_xcvr:
    platform_device_unregister(xcvr_pdev);
err_unregister_regio:
    platform_device_unregister(regio_pdev);
err_remove_grp_fpga:
    sysfs_remove_group(swfpga, &fpga_attr_grp);
err_remove_fpga:
    kobject_put(swfpga);
    fpga_pci_remove();
err_disable_device:
    pci_disable_device(dev);
err_exit:
    return err;
}

static void cls_fpga_remove(struct pci_dev *dev) {
    int i;
    struct fpga_priv *priv = pci_get_drvdata(dev);

    for (i = 0; i < priv->num_i2c_bus; i++) {
        if (priv->i2cbuses_pdev[i])
            platform_device_unregister(priv->i2cbuses_pdev[i]);
    }
    platform_device_unregister(priv->xcvr_pdev);
    platform_device_unregister(priv->regio_pdev);
    sysfs_remove_group(swfpga, &fpga_attr_grp);
    kobject_put(swfpga);
    fpga_pci_remove();
    pci_disable_device(dev);

    return;
};

static const struct pci_device_id pci_fpga[] = {
    {PCI_VDEVICE(XILINX, FPGA_PCIE_DEVICE_ID)},
    {
        0,
    }};

MODULE_DEVICE_TABLE(pci, pci_fpga);

static struct pci_driver cls_fpga_pci_driver = {
    .name = DRV_NAME,
    .id_table = pci_fpga,
    .probe = cls_fpga_probe,
    .remove = cls_fpga_remove,
};

module_pci_driver(cls_fpga_pci_driver);

MODULE_AUTHOR("CLS");
MODULE_DESCRIPTION("Celestica Blackstone fpga driver");
MODULE_VERSION(MOD_VERSION);
MODULE_LICENSE("GPL");
