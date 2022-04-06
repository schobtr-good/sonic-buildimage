// SPDX-License-Identifier: GPL-2.0+
/*
 * misc_cpld.c - LED_CPLD driver
 *
 * Copyright (C) 2019 Celestica Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>

#define CPLD1_ADDR      0x30
#define SCRATCH_ADDR    0x01
#define LED_OPMODE      0x09
#define LED_TEST        0x0A

struct misc_cpld_data {
	struct i2c_client *client;
	uint8_t read_addr;
	const char *link_name;
};

static ssize_t getreg_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct misc_cpld_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int value;

	value = i2c_smbus_read_byte_data(client, data->read_addr);
	if (value < 0)
		return value;

	return sprintf(buf, "0x%.2x\n", value);
}

static ssize_t getreg_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	uint8_t value;
	ssize_t status;
	struct misc_cpld_data *data = dev_get_drvdata(dev);

	status = kstrtou8(buf, 0, &value);
	if (status != 0)
		return status;

	data->read_addr = value;

	return size;
}

static ssize_t setreg_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	uint8_t addr, value;
	ssize_t status;
	struct misc_cpld_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	char *tok;

	tok = strsep((char **)&buf, " ");
	if (tok == NULL)
		return -EINVAL;
	status = kstrtou8(tok, 0, &addr);
	if (status != 0)
		return status;

	tok = strsep((char **)&buf, " ");
	if (tok == NULL)
		return -EINVAL;
	status = kstrtou8(tok, 0, &value);
	if (status != 0)
		return status;

	status = i2c_smbus_write_byte_data(client, addr, value);
	if (status == 0)
		status = size;
	return status;
}

static ssize_t scratch_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct misc_cpld_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int value;

	value = i2c_smbus_read_byte_data(client, SCRATCH_ADDR);
	if (value < 0)
		return value;

	return sprintf(buf, "0x%.2x\n", value);
}

static ssize_t scratch_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	uint8_t value;
	ssize_t status;
	struct misc_cpld_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	status = kstrtou8(buf, 0, &value);
	if (status != 0)
		return status;
	status = i2c_smbus_write_byte_data(client, SCRATCH_ADDR, value);
	if (status == 0)
		status = size;
	return status;
}

DEVICE_ATTR_RW(getreg);
DEVICE_ATTR_WO(setreg);
DEVICE_ATTR_RW(scratch);

static struct attribute *misc_cpld_attrs[] = {
	&dev_attr_getreg.attr,
	&dev_attr_setreg.attr,
	&dev_attr_scratch.attr,
	NULL,
};
ATTRIBUTE_GROUPS(misc_cpld);

// static ssize_t port_led_mode_show(struct device *dev,
//                                   struct device_attribute *attr, char *buf)
// {
//         int led_mode_1, led_mode_2;
//         struct misc_cpld_data *data = dev_get_drvdata(dev);
//         struct i2c_client *client1 = data->client;

//         led_mode_1 = i2c_smbus_read_byte_data(client1, LED_OPMODE);
//         if (led_mode_1 < 0)
//                 return led_mode_1;

//         return sprintf(buf, "%s %s\n",
//                        led_mode_1 ? "test" : "normal",
//                        led_mode_2 ? "test" : "normal");
// }

// static ssize_t port_led_mode_store(struct device *dev,
//                                    struct device_attribute *attr,
//                                    const char *buf, size_t size)
// {
//         int status;
//         uint8_t led_mode;
//         struct misc_cpld_data *data = dev_get_drvdata(dev);
//         struct i2c_client *client1 = data->client;

//         if (sysfs_streq(buf, "test"))
//                 led_mode = 0x01;
//         else if (sysfs_streq(buf, "normal"))
//                 led_mode = 0x00;
//         else
//                 return -EINVAL;

//         status = i2c_smbus_write_byte_data(client1, LED_OPMODE, led_mode);
//         if (status != 0) {
//                 return status;
//         }

//         return size;
// }

// static ssize_t port_led_color_show(struct device *dev,
//                                    struct device_attribute *attr, char *buf)
// {
//         int led_color1, led_color2;
//         struct misc_cpld_data *data = dev_get_drvdata(dev);
//         struct i2c_client *client1 = data->client;

//         led_color1 = i2c_smbus_read_byte_data(client1, LED_TEST);
//         if (led_color1 < 0)
//                 return led_color1;

//         return sprintf(buf, "%s %s\n",
//                        led_color1 == 0x02 ? "green" :
//                        led_color1 == 0x01 ? "amber" : "off",

//                        led_color2 == 0x07 ? "off" :
//                        led_color2 == 0x06 ? "green" :
//                        led_color2 == 0x05 ? "red" :
//                        led_color2 == 0x04 ? "yellow" :
//                        led_color2 == 0x03 ? "blue" :
//                        led_color2 == 0x02 ? "cyan" :
//                        led_color2 == 0x01 ? "magenta" : "white");
// }

// static ssize_t port_led_color_store(struct device *dev,
//                                     struct device_attribute *attr,
//                                     const char *buf, size_t size)
// {
//         int status;
//         uint8_t led_color1, led_color2;
//         struct misc_cpld_data *data = dev_get_drvdata(dev);
//         struct i2c_client *client1 = data->client;

//         if (sysfs_streq(buf, "off")) {
//                 led_color1 = 0x07;
//                 led_color2 = 0x07;
//         } else if (sysfs_streq(buf, "green")) {
//                 led_color1 = 0x07;
//                 led_color2 = 0x06;
//         } else if (sysfs_streq(buf, "red")) {
//                 led_color1 = 0x07;
//                 led_color2 = 0x05;
//         } else if (sysfs_streq(buf, "yellow")) {
//                 led_color1 = 0x07;
//                 led_color2 = 0x04;
//         } else if (sysfs_streq(buf, "blue")) {
//                 led_color1 = 0x07;
//                 led_color2 = 0x03;
//         } else if (sysfs_streq(buf, "cyan")) {
//                 led_color1 = 0x02;
//                 led_color2 = 0x02;
//         } else if (sysfs_streq(buf, "magenta")) {
//                 led_color1 = 0x01;
//                 led_color2 = 0x01;
//         } else if (sysfs_streq(buf, "white")) {
//                 led_color1 = 0x07;
//                 led_color2 = 0x00;
//         } else {
//                 return -EINVAL;
//         }

//         status = i2c_smbus_write_byte_data(client1, LED_TEST, led_color1);
//         if (status != 0) {
//                 return status;
//         }

//         return size;
// }

// DEVICE_ATTR_RW(port_led_mode);
// DEVICE_ATTR_RW(port_led_color);

// static struct attribute *sff_led_attrs[] = {
//         &dev_attr_port_led_mode.attr,
//         &dev_attr_port_led_color.attr,
//         NULL,
// };

// static struct attribute_group sff_led_groups = {
//         .attrs = sff_led_attrs,
// };

static int misc_cpld_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int err;
	struct misc_cpld_data *drvdata1;
	struct device *hwmon_dev;
	char *device_name;

	device_name = "CPLD1";

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"failed to check functionality.\n");
		return -EPFNOSUPPORT;
	}

	drvdata1 = devm_kzalloc(&client->dev,
				sizeof(struct misc_cpld_data), GFP_KERNEL);

	if (!drvdata1) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"failed to kzalloc drvdata1.\n");
		goto exit;
	}

	drvdata1->client = client;
	drvdata1->read_addr = 0x00;
	drvdata1->link_name = device_name;
	i2c_set_clientdata(client, drvdata1);
	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
							   device_name,
							   drvdata1,
							   misc_cpld_groups);

	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		dev_err(&client->dev,
			"failed to initialize hwmon.\n");
		goto exit;
	}

	err = sysfs_create_link(&client->dev.kobj, &hwmon_dev->kobj,
				device_name);
	if (err) {
		dev_err(&client->dev,
			"failed to create sysfs link.\n");
		goto exit;
	}

	return 0;

err_link:
	sysfs_remove_link(&client->dev.kobj, device_name);

exit:
	dev_err(&client->dev, "probe error %d\n", err);
	return err;
}

static int misc_cpld_remove(struct i2c_client *client)
{
	struct misc_cpld_data *data = dev_get_drvdata(&client->dev);
	char *device_name = data->link_name;

	sysfs_remove_link(&client->dev.kobj, device_name);
	return 0;
}

static const struct i2c_device_id misc_cpld_ids[] = {
	{ "misc_cpld1", CPLD1_ADDR },
	{}
};

MODULE_DEVICE_TABLE(i2c, misc_cpld_ids);

static struct i2c_driver misc_cpld_driver = {
	.driver = {
		.name   = "misc_cpld",
		.owner = THIS_MODULE,
	},
	.probe          = misc_cpld_probe,
	.remove         = misc_cpld_remove,
	.id_table       = misc_cpld_ids,
};

module_i2c_driver(misc_cpld_driver);

MODULE_AUTHOR("Saranpong Chobtrong<schobtr@celestica.com>");
MODULE_DESCRIPTION("Celestica Blackstone MISC_CPLD driver");
MODULE_VERSION("1.0.1");
MODULE_LICENSE("GPL");
