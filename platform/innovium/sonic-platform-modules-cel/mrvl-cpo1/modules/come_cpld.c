/*
 * come-cpld.c - The CPLD driver for the COME of MRVL_CPO1
 * The driver implement sysfs to access CPLD register on the COME of MRVL_CPO1
 * via LPC bus. Copyright (C) 2021 Celestica Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <uapi/linux/stat.h>

#define DRIVER_NAME "come_cpld"
/**
 * CPLD register address for read and write.
 */
#define START_ADDR 0xA1E0
#define VERSION_ADDR 0xA1E0
#define SCRATCH_ADDR 0xA1E1
#define BLT_MONTH_ADDR 0xA1E2
#define BLT_DATE_ADDR 0xA1E3
#define REGISTER_SIZE 0xA

struct cpld_c_data {
    struct mutex cpld_lock;
    uint16_t read_addr;
};

struct cpld_c_data *cpld_data;

static ssize_t scratch_show(struct device *dev,
                            struct device_attribute *devattr, char *buf) {
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(SCRATCH_ADDR);
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%2.2x\n", data);
}

static ssize_t scratch_store(struct device *dev,
                             struct device_attribute *devattr, const char *buf,
                             size_t count) {
    unsigned long data;
    char *last;

    mutex_lock(&cpld_data->cpld_lock);
    data = (uint16_t)strtoul(buf, &last, 16);
    if (data == 0 && buf == last) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }
    outb(data, SCRATCH_ADDR);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}
static DEVICE_ATTR_RW(scratch);

/* CPLD version attributes */
static ssize_t version_show(struct device *dev, struct device_attribute *attr,
                            char *buf) {
    u8 version;
    mutex_lock(&cpld_data->cpld_lock);
    version = inb(VERSION_ADDR);
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%d.%d\n", version >> 4, version & 0x0F);
}
static DEVICE_ATTR_RO(version);

/* CPLD version attributes */
static ssize_t build_date_show(struct device *dev,
                               struct device_attribute *attr, char *buf) {
    u8 month, day_of_month;
    mutex_lock(&cpld_data->cpld_lock);
    day_of_month = inb(BLT_DATE_ADDR);
    month = inb(BLT_MONTH_ADDR);
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%x/%x\n", day_of_month, month);
}
static DEVICE_ATTR_RO(build_date);

static ssize_t getreg_store(struct device *dev,
                            struct device_attribute *devattr, const char *buf,
                            size_t count) {
    uint16_t addr;
    char *last;

    addr = (uint16_t)strtoul(buf, &last, 16);
    if (addr == 0 && buf == last) {
        return -EINVAL;
    }
    cpld_data->read_addr = addr;
    return count;
}

static ssize_t getreg_show(struct device *dev, struct device_attribute *attr,
                           char *buf) {
    int len = 0;

    mutex_lock(&cpld_data->cpld_lock);
    len = sprintf(buf, "0x%2.2x\n", inb(cpld_data->read_addr));
    mutex_unlock(&cpld_data->cpld_lock);
    return len;
}
static DEVICE_ATTR_RW(getreg);

static ssize_t setreg_store(struct device *dev,
                            struct device_attribute *devattr, const char *buf,
                            size_t count) {
    uint16_t addr;
    uint8_t value;
    char *tok;
    char clone[count];
    char *pclone = clone;
    char *last;

    strcpy(clone, buf);

    mutex_lock(&cpld_data->cpld_lock);
    tok = strsep((char **)&pclone, " ");
    if (tok == NULL) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }
    addr = (uint16_t)strtoul(tok, &last, 16);
    if (addr == 0 && tok == last) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }

    tok = strsep((char **)&pclone, " ");
    if (tok == NULL) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }
    value = (uint8_t)strtoul(tok, &last, 16);
    if (value == 0 && tok == last) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }

    outb(value, addr);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}
static DEVICE_ATTR_WO(setreg);

/**
 * Read all CPLD register in binary mode.
 */
static ssize_t dump_read(struct file *filp, struct kobject *kobj,
                         struct bin_attribute *attr, char *buf, loff_t off,
                         size_t count) {
    unsigned long i = 0;
    ssize_t status;

    mutex_lock(&cpld_data->cpld_lock);
begin:
    if (i < count) {
        buf[i++] = inb(START_ADDR + off);
        off++;
        msleep(1);
        goto begin;
    }
    status = count;

    mutex_unlock(&cpld_data->cpld_lock);
    return status;
}
static BIN_ATTR_RO(dump, REGISTER_SIZE);

static struct attribute *cpld_c_attrs[] = {
    &dev_attr_version.attr, &dev_attr_build_date.attr, &dev_attr_scratch.attr,
    &dev_attr_getreg.attr,  &dev_attr_setreg.attr,     NULL,
};

static struct bin_attribute *cpld_c_bin_attrs[] = {
    &bin_attr_dump,
    NULL,
};

static struct attribute_group cpld_c_attrs_grp = {
    .attrs = cpld_c_attrs,
    .bin_attrs = cpld_c_bin_attrs,
};

static struct resource cpld_c_resources[] = {
    {
        .start = START_ADDR,
        .end = START_ADDR + REGISTER_SIZE - 1,
        .flags = IORESOURCE_IO,
    },
};

static void cpld_c_dev_release(struct device *dev) { return; }

static struct platform_device cpld_c_dev = {
    .name = DRIVER_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(cpld_c_resources),
    .resource = cpld_c_resources,
    .dev = {
        .release = cpld_c_dev_release,
    }};

static int cpld_c_drv_probe(struct platform_device *pdev) {
    struct resource *res;
    int err = 0;

    cpld_data =
        devm_kzalloc(&pdev->dev, sizeof(struct cpld_c_data), GFP_KERNEL);
    if (!cpld_data) return -ENOMEM;

    mutex_init(&cpld_data->cpld_lock);

    cpld_data->read_addr = VERSION_ADDR;

    res = platform_get_resource(pdev, IORESOURCE_IO, 0);
    if (unlikely(!res)) {
        printk(KERN_ERR "Specified Resource Not Available...\n");
        return -ENODEV;
    }

    err = sysfs_create_group(&pdev->dev.kobj, &cpld_c_attrs_grp);
    if (err) {
        printk(KERN_ERR "Cannot create sysfs for COME CPLD\n");
        return err;
    }
    return 0;
}

static int cpld_c_drv_remove(struct platform_device *pdev) {
    sysfs_remove_group(&pdev->dev.kobj, &cpld_c_attrs_grp);
    return 0;
}

static struct platform_driver cpld_c_drv = {
    .probe = cpld_c_drv_probe,
    .remove = __exit_p(cpld_c_drv_remove),
    .driver =
        {
            .name = DRIVER_NAME,
        },
};

int cpld_c_init(void) {
    // Register platform device and platform driver
    platform_device_register(&cpld_c_dev);
    platform_driver_register(&cpld_c_drv);
    return 0;
}

void cpld_c_exit(void) {
    // Unregister platform device and platform driver
    platform_driver_unregister(&cpld_c_drv);
    platform_device_unregister(&cpld_c_dev);
}

module_init(cpld_c_init);
module_exit(cpld_c_exit);

MODULE_AUTHOR("Celestica Inc.");
MODULE_DESCRIPTION("Celestica MRVL_CPO1 CPLD COME driver");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL");