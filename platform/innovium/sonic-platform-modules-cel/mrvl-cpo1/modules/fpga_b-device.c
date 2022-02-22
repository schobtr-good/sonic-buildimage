/*
 * fpga_b-device.c - PCI device driver for Marvell CPO1 Base board FPGA.
 *
 * Author: Wirut Getbamrung
 *
 * Copyright (C) 2021 Celestica Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
Baseboard FPGA:
    /sys/devices/platform/
                        ├── fpga-sys
                        │   ├── dump
                        │   ├── getreg
                        │   ├── scratch
                        │   ├── setreg
                        │   └── version
                        ├── fpga-xcvr/
                        │   ├── SFP1
                        │   │	├── sfp_modabs
                        │   │	├── sfp_rxlos
                        │   │	├── sfp_txdisable
                        │   │	├── sfp_txfault
                        │   └── SFP2
                        │       ├── sfp_modabs
                        │       ├── sfp_rxlos
                        │       ├── sfp_txdisable
                        │       └── sfp_txfault
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/i2c/pca954x.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/stddef.h>

#include "cls-i2c-ocores.h"
#include "fpga-xcvr.h"

#define MOD_VERSION "1.0.0"
#define DRV_NAME "fpga_device"

#define I2C_MUX_CHANNEL(_ch, _adap_id, _deselect) \
    [_ch] = {.adap_id = _adap_id, .deselect_on_exit = _deselect}

#define FPGA_PCIE_DEVICE_ID 0x7021

#define FPGA_TYPE_ADDR 0x0C
#define FPGA_OTHER_CR_ADDR 0x14

#define BMC_PRESENT_BIT 0x08
#define BMC_PRESENT                                     \
    0x00 /* FPGA_OTHER_CR_ADDR bit8 0-bmc present 1-bmc \
            absent*/

// #define FPGA_EXCLUDE_MIN_BUS                                                \
//     8 /* iic core 0-7 is share with BMC, 8 and 9 is for FPGA only, mask 0-7 \
//          when BMC is present*/

#define MMIO_BAR 0
#define PCA9548_I2C_BUS_OFS \
    11 /* for mrvl-cpo1, 2 bus for COMe, 10 bus for FPGA, [0~11] */

/* I2C ocore configurations */
#define OCORE_REGSHIFT 2
#define OCORE_IP_CLK_khz 62500
#define OCORE_BUS_CLK_khz 100
#define OCORE_REG_IO_WIDTH 1

/* Optical port xcvr configuration */
#define XCVR_REG_SHIFT 2
#define XCVR_NUM_PORT 32
#define XCVR_PORT_REG_SIZE 0x10

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

/* switchbrd_priv - switchboard private data */
struct switchbrd_priv {
    void __iomem *iomem;
    unsigned long base;
    int num_i2c_bus;
    const char *i2c_devname;
    const char *xcvr_devname;
    const char *fpga_devname;
    struct platform_device **i2cbuses_pdev;
    struct platform_device *regio_pdev;
    struct platform_device *spiflash_pdev;
    struct platform_device *xcvr_pdev;
    struct platform_device *fpga_pdev;
};

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
/* Resource IOMEM for i2c bus 6 for Baseboard FPGA*/
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

/* Resource IOMEM for i2c bus 10 for FPGA_SFPP_0_I2C*/
static struct resource cls_i2c_res_10[] = {
    {
        .start = 0x920,
        .end = 0x93F,
        .flags = IORESOURCE_MEM,
    },
};

/* Resource IOMEM for i2c bus 11 for FPGA_SFPP_1_I2C*/
static struct resource cls_i2c_res_11[] = {
    {
        .start = 0x940,
        .end = 0x95F,
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

/* Resource IOMEM for front panel XCVR */
static struct resource fpga_res[] = {
    {
        .start = 0x00,
        .end = 0xFF,
        .flags = IORESOURCE_MEM,
    },
};

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
                .num_devices = 0,
                .devices = NULL,
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
};

/* xcvr front panel mapping */
static struct port_info front_panel_ports[] = {
    {"SFP1", 1, SFP}, {"SFP2", 2, SFP},
    /* END OF LIST */
};

static struct cls_xcvr_platform_data xcvr_data[] = {
    {
        .port_reg_size = 0x04,
        .num_ports = ARRAY_SIZE(front_panel_ports),
        .devices = front_panel_ports,
    },
};

// TODO: Add a platform configuration struct, and use probe as a factory,
//	 so xcvr, fwupgrade device can configured as options.

static int cls_fpga_probe(struct pci_dev *dev, const struct pci_device_id *id) {
    int err;
    int num_i2c_bus, i = 0, ret, vector;
    int bmc_present = 0; /* 0-present 1-absent */
    unsigned long rstart;
    void __iomem *base_addr;
    struct switchbrd_priv *priv;
    struct platform_device **i2cbuses_pdev;
    struct platform_device *fpga_pdev;
    struct platform_device *xcvr_pdev;
    uint32_t fpga_type;

    err = pci_enable_device(dev);
    if (err) {
        dev_err(&dev->dev, "Failed to enable PCI device\n");
        goto err_exit;
    }
    pci_set_master(dev);

    /* Check for valid MMIO address */
    base_addr = pci_iomap(dev, MMIO_BAR, 0);
    if (!base_addr) {
        dev_err(&dev->dev, "Failed to map PCI device mem\n");
        err = -ENODEV;
        goto err_disable_device;
    }

    // device type checking
    fpga_type = ioread32(base_addr + 0x0014);
    printk("board type val: 0x%lx\n", fpga_type);
    if (fpga_type != 0x01) {
        printk("unknow board type val: 0x%lx\n", fpga_type);
        err = -ENXIO;
        goto err_unmap;
    }

    // ret = pci_enable_msi(dev);
    // if (ret) {
    //     dev_err(&dev->dev, "failed to allocate MSI entry\n");
    //     goto err_unmap;
    // }

#if 0 /* only one FPGA, so not to judge FPGA TYTE*/
	fpga_type = ioread32(base_addr + FPGA_TYPE_ADDR);
	printk("fpga Type:0x%8.8x\n",fpga_type);
	if (fpga_type == FPGA_CMM_TYPE) {
		err = 0;
		goto err_exit;
	}
#endif
    bmc_present =
        (ioread32(base_addr + FPGA_OTHER_CR_ADDR) >> BMC_PRESENT_BIT) & 0x01;
    if (bmc_present == BMC_PRESENT) {
        printk("BMC present\n");
    } else {
        printk("BMC absent\n");
    }

    rstart = pci_resource_start(dev, MMIO_BAR);
    if (!rstart) {
        dev_err(&dev->dev,
                "Baseboard base address uninitialized, "
                "check FPGA\n");
        err = -ENODEV;
        goto err_diable_msi;
    }

    dev_dbg(&dev->dev, "BAR%d res: 0x%lx-0x%llx\n", MMIO_BAR, rstart,
            pci_resource_end(dev, MMIO_BAR));

    printk("BAR%d res: 0x%lx-0x%llx\n", MMIO_BAR, rstart,
           pci_resource_end(dev, MMIO_BAR));

    priv = devm_kzalloc(&dev->dev, sizeof(struct switchbrd_priv), GFP_KERNEL);
    if (!priv) {
        err = -ENOMEM;
        goto err_diable_msi;
    }

    pci_set_drvdata(dev, priv);
    num_i2c_bus = ARRAY_SIZE(i2c_bus_configs);
    i2cbuses_pdev = devm_kzalloc(
        &dev->dev, num_i2c_bus * sizeof(struct platform_device *), GFP_KERNEL);

    fpga_res[0].start += rstart;
    fpga_res[0].end += rstart;
    xcvr_res[0].start += rstart;
    xcvr_res[0].end += rstart;
    printk("num_i2c_bus = %x,fpga_res start/end %x/%x,restart=%x\n",
           num_i2c_bus, fpga_res[0].start, fpga_res[0].end, rstart);
    printk("num_i2c_bus = %x,xcvr_res start/end %x/%x,restart=%x\n",
           num_i2c_bus, xcvr_res[0].start, xcvr_res[0].end, rstart);

    priv->i2c_devname = "cls-ocores-i2c";
    priv->xcvr_devname = "fpga-xcvr";
    priv->fpga_devname = "fpga-sys";

    fpga_pdev = platform_device_register_resndata(
        NULL, priv->fpga_devname, -1, fpga_res, ARRAY_SIZE(fpga_res), NULL, 0);
    if (IS_ERR(fpga_pdev)) {
        dev_err(&dev->dev, "Failed to register fpga node\n");
        err = PTR_ERR(fpga_pdev);
        goto err_unmap;
    }
    printk("register fpga node\n");

    xcvr_pdev = platform_device_register_resndata(
        NULL, priv->xcvr_devname, -1, xcvr_res, ARRAY_SIZE(xcvr_res),
        &xcvr_data, sizeof(xcvr_data));
    if (IS_ERR(xcvr_pdev)) {
        dev_err(&dev->dev, "Failed to register xcvr node\n");
        err = PTR_ERR(xcvr_pdev);
        goto err_unregister_fpga_dev;
    }
    printk("register xcvr node\n");

    // if (bmc_present == BMC_PRESENT) {
    //     i += FPGA_EXCLUDE_MIN_BUS; /* skip share bus so there's no dual i2c
    //                                   masters situation */
    // } else {
    //     i = 0;
    // }
    for (; i < num_i2c_bus; i++) {
        /* override resource with MEM/IO resource offset */
        i2c_bus_configs[i].res[0].start += rstart;
        i2c_bus_configs[i].res[0].end += rstart;

        // /* all fpga i2c bus share pci device msi irq */
        // i2c_bus_configs[i].pdata.irq = dev->irq;

        dev_dbg(&dev->dev, "i2c-bus.%d: 0x%llx - 0x%llx\n",
                i2c_bus_configs[i].id, i2c_bus_configs[i].res[0].start,
                i2c_bus_configs[i].res[0].end);
        printk("bus id:%d, i2c_bus_configs[%d].res[0].start/end=%x:%x\n",
               i2c_bus_configs[i].id, i, i2c_bus_configs[i].res[0].start,
               i2c_bus_configs[i].res[0].end);

        switch (i + 1) {
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
            default:
                i2c_bus_configs[i].pdata.bus_khz = OCORE_BUS_CLK_khz;
        }

        i2cbuses_pdev[i] = platform_device_register_resndata(
            &dev->dev, priv->i2c_devname, i2c_bus_configs[i].id,
            i2c_bus_configs[i].res, i2c_bus_configs[i].num_res,
            &i2c_bus_configs[i].pdata, sizeof(i2c_bus_configs[i].pdata));
        if (IS_ERR(i2cbuses_pdev[i])) {
            dev_err(&dev->dev, "Failed to register ocores-i2c-cls.%d\n",
                    i2c_bus_configs[i].id);
            err = PTR_ERR(i2cbuses_pdev[i]);
            goto err_unregister_ocore;
        }
    }

    priv->iomem = base_addr;
    priv->base = rstart;
    priv->num_i2c_bus = num_i2c_bus;
    priv->i2cbuses_pdev = i2cbuses_pdev;
    priv->xcvr_pdev = xcvr_pdev;
    priv->fpga_pdev = fpga_pdev;
    printk("base_addr=%x\n", base_addr);
    return 0;

err_unregister_ocore:
    for (i = 0; i < num_i2c_bus; i++) {
        if (priv->i2cbuses_pdev[i]) {
            platform_device_unregister(priv->i2cbuses_pdev[i]);
        }
    }
    platform_device_unregister(xcvr_pdev);
err_unregister_fpga_dev:
    platform_device_unregister(fpga_pdev);
#if 0
err_free_vector:
	pci_free_irq_vectors(dev);
#endif
err_diable_msi:
    pci_disable_msi(dev);
err_unmap:
    pci_iounmap(dev, base_addr);
err_disable_device:
    pci_disable_device(dev);
err_exit:
    return err;
}

static void cls_fpga_remove(struct pci_dev *dev) {
    int i;
    struct switchbrd_priv *priv = pci_get_drvdata(dev);

    if (priv == NULL)
	    return;

    for (i = 0; i < priv->num_i2c_bus; i++) {
        if (priv->i2cbuses_pdev[i])
            platform_device_unregister(priv->i2cbuses_pdev[i]);
    }
    platform_device_unregister(priv->xcvr_pdev);
    platform_device_unregister(priv->fpga_pdev);
#if 0
	pci_free_irq_vectors(dev);
#endif
    pci_disable_msi(dev);
    pci_iounmap(dev, priv->iomem);
    pci_disable_device(dev);
    return;
};

static const struct pci_device_id pci_clsswbrd[] = {
    {PCI_VDEVICE(XILINX, FPGA_PCIE_DEVICE_ID)},
    {
        0,
    }};

MODULE_DEVICE_TABLE(pci, pci_clsswbrd);

static struct pci_driver cls_pci_driver = {
    .name = DRV_NAME,
    .id_table = pci_clsswbrd,
    .probe = cls_fpga_probe,
    .remove = cls_fpga_remove,
};

module_pci_driver(cls_pci_driver);

MODULE_AUTHOR("Wirut Getbamrung<wgetbumr@celestica.com>");
MODULE_DESCRIPTION("Celestica MRVL_CPO1 Baseboard FPGA device driver");
MODULE_VERSION(MOD_VERSION);
MODULE_LICENSE("GPL");
