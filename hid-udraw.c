/*
 * HID driver for THQ PS3 uDraw tablet
 *
 * Copyright (C) 2016 Red Hat Inc. All Rights Reserved
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
//#include "hid-ids.h"

MODULE_AUTHOR("Bastien Nocera <hadess@hadess.net>");
MODULE_DESCRIPTION("PS3 uDraw tablet driver");
MODULE_LICENSE("GPL");

/*
 * Protocol information from:
 * http://brandonw.net/udraw/
 * and the source code of:
 * https://vvvv.org/contribution/udraw-hid
 */

/*
 * The device is setup with multiple input devices to make it easier
 * to handle in user-space:
 * - the touch area which works as a touchpad
 * - the tablet area which works as a touchpad/drawing tablet
 * - a joypad with a d-pad, and 7 buttons
 * - an optional, disabled by default, accelerometer device
 */

static const unsigned short udraw_joy_key_table[] = {
	BTN_SOUTH,
	BTN_NORTH,
	BTN_EAST,
	BTN_WEST,
	BTN_SELECT,
	BTN_START,
	BTN_MODE
};

#define DEVICE_NAME "THQ uDraw Game Tablet for PS3"

struct udraw {
	struct input_dev *joy_input_dev;
	struct input_dev *touch_input_dev;
	bool touch_input_dev_registered;
	struct input_dev *pen_input_dev;
	bool pen_input_dev_registered;
	struct input_dev *accel_input_dev;
	bool accel_input_dev_registered;
	struct hid_device *hdev;
};

static int udraw_raw_event(struct hid_device *hdev, struct hid_report *report,
	 u8 *data, int len)
{
	struct udraw *udraw = hid_get_drvdata(hdev);
	int x, y, z;

	if (len != 0x1B)
		return 0;

	/* joypad */
	input_report_key(udraw->joy_input_dev, BTN_WEST, data[0] & 1);
	input_report_key(udraw->joy_input_dev, BTN_SOUTH, data[0] & 2);
	input_report_key(udraw->joy_input_dev, BTN_EAST, data[0] & 4);
	input_report_key(udraw->joy_input_dev, BTN_NORTH, data[0] & 8);

	input_report_key(udraw->joy_input_dev, BTN_SELECT, data[1] & 1);
	input_report_key(udraw->joy_input_dev, BTN_START, data[1] & 2);
	input_report_key(udraw->joy_input_dev, BTN_MODE, data[1] & 16);

	x = y = 0;
	switch (data[2]) {
	case 0x0:
		y = 127;
		break;
	case 0x1:
		y = 127;
		x = 127;
		break;
	case 0x2:
		x = 127;
		break;
	case 0x3:
		y = -127;
		x = 127;
		break;
	case 0x4:
		y = -127;
		break;
	case 0x5:
		y = -127;
		x = -127;
		break;
	case 0x6:
		x = -127;
		break;
	case 0x7:
		y = 127;
		x = -127;
		break;
	default:
		;;
	}

	input_report_abs(udraw->joy_input_dev, ABS_X, x);
	input_report_abs(udraw->joy_input_dev, ABS_Y, y);

	input_sync(udraw->joy_input_dev);

	/* touchpad */
	x = y = 0;
	/* Finger(s) in use? */
	if (data[11] == 0x80 || data[11] >= 191) {
		bool single_touch = (data[11] == 0x80);
		if (data[15] != 0x0F && data[17] != 0xFF)
			x = data[15] * 256 + data[17];
		if (data[16] != 0x0F && data[18] != 0xFF)
			y = data[16] * 256 + data[18];

		input_report_key(udraw->touch_input_dev, BTN_TOUCH, 1);
		input_report_key(udraw->touch_input_dev, BTN_TOOL_FINGER,
				single_touch);
		input_report_key(udraw->touch_input_dev, BTN_TOOL_DOUBLETAP,
				!single_touch);

		input_report_abs(udraw->touch_input_dev, ABS_X, x);
		input_report_abs(udraw->touch_input_dev, ABS_Y, y);
	} else {
		input_report_key(udraw->touch_input_dev, BTN_TOUCH, 0);
		input_report_key(udraw->touch_input_dev, BTN_TOOL_FINGER, 0);
		input_report_key(udraw->touch_input_dev, BTN_TOOL_DOUBLETAP, 0);
	}
	input_sync(udraw->touch_input_dev);

	/* pen */
	x = y = 0;
	if (data[11] == 0x40) {
		int level = data[13] - 0x74;

		if (level < 0)
			level = 0;

		if (data[15] != 0x0F && data[17] != 0xFF)
			x = data[15] * 256 + data[17];
		if (data[16] != 0x0F && data[18] != 0xFF)
			y = data[16] * 256 + data[18];

		input_report_key(udraw->pen_input_dev, BTN_TOOL_PEN, 1);
		input_report_abs(udraw->pen_input_dev, ABS_PRESSURE, level);
		input_report_abs(udraw->pen_input_dev, ABS_X, x);
		input_report_abs(udraw->pen_input_dev, ABS_Y, y);
	} else {
		input_report_abs(udraw->pen_input_dev, ABS_PRESSURE, 0);
		input_report_key(udraw->pen_input_dev, BTN_TOOL_PEN, 0);
	}
	input_sync(udraw->touch_input_dev);

	/* accel */
	x = (data[19] + data[20] * 0xFF);
	y = (data[21] + data[22] * 0xFF);
	z = (data[23] + data[24] * 0xFF);
	input_report_abs(udraw->accel_input_dev, ABS_RX, x);
	input_report_abs(udraw->accel_input_dev, ABS_RY, y);
	input_report_abs(udraw->accel_input_dev, ABS_RZ, z);
	input_sync(udraw->accel_input_dev);

	/* let hidraw and hiddev handle the report */
	return 0;
}

static int udraw_open(struct input_dev *dev)
{
	struct udraw *udraw = input_get_drvdata(dev);

	return hid_hw_open(udraw->hdev);
}

static void udraw_close(struct input_dev *dev)
{
	struct udraw *udraw = input_get_drvdata(dev);

	hid_hw_close(udraw->hdev);
}

static struct input_dev *allocate_and_setup(struct hid_device *hdev,
		const char *name)
{
	struct input_dev *input_dev;

	input_dev = input_allocate_device();
	if (!input_dev)
		return NULL;

	input_dev->name = name;
	input_dev->phys = hdev->phys;
	input_dev->dev.parent = &hdev->dev;
	input_dev->open = udraw_open;
	input_dev->close = udraw_close;
	input_dev->uniq = hdev->uniq;
	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor  = hdev->vendor;
	input_dev->id.product = hdev->product;
	input_dev->id.version = hdev->version;
	input_set_drvdata(input_dev, hid_get_drvdata(hdev));

	return input_dev;
}

static struct input_dev *udraw_setup_touch(struct udraw *udraw,
		struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " (touchpad)");
	if (!input_dev)
		return NULL;

	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);

	set_bit(ABS_X, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_X, 0, 1920, 1, 0);
	set_bit(ABS_Y, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_Y, 0, 1080, 1, 0);

	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);
	set_bit(BTN_TOOL_DOUBLETAP, input_dev->keybit);

	set_bit(INPUT_PROP_POINTER, input_dev->propbit);
	set_bit(INPUT_PROP_BUTTONPAD, input_dev->propbit);

	return input_dev;
}

static struct input_dev *udraw_setup_pen(struct udraw *udraw,
		struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " (pen)");
	if (!input_dev)
		return NULL;

	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);

	set_bit(ABS_X, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_X, 0, 1920, 1, 0);
	set_bit(ABS_Y, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_Y, 0, 1080, 1, 0);
	set_bit(ABS_PRESSURE, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 0xFF - 0x74 - 0x01, 0, 0);

	set_bit(BTN_TOOL_PEN, input_dev->keybit);

	set_bit(INPUT_PROP_POINTER, input_dev->propbit);

	return input_dev;
}

static struct input_dev *udraw_setup_accel(struct udraw *udraw,
		struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " (accelerometer)");
	if (!input_dev)
		return NULL;

	input_dev->evbit[0] = BIT(EV_ABS);

	//FIXME the default values are wrong
	set_bit(ABS_RX, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_RX, 0, 1920, 1, 0);
	set_bit(ABS_RY, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_RY, 0, 1080, 1, 0);
	set_bit(ABS_RZ, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_RZ, 0, 1080, 1, 0);

	set_bit(INPUT_PROP_ACCELEROMETER, input_dev->propbit);

	return input_dev;
}

static void udraw_setup_joypad(struct udraw *udraw, struct input_dev *input_dev)
{
	int i;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	set_bit(ABS_X, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_X, -127, 127, 0, 0);
	set_bit(ABS_Y, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_Y, -127, 127, 0, 0);

	for (i = 0; i < ARRAY_SIZE(udraw_joy_key_table); i++)
		set_bit(udraw_joy_key_table[i], input_dev->keybit);
}

static int udraw_input_configured(struct hid_device *hdev,
		struct hid_input *hidinput)
{
	struct input_dev *input_dev = hidinput->input;
	struct udraw *udraw = hid_get_drvdata(hdev);
	int error;

	/* joypad, uses the hid device */
	udraw->joy_input_dev = input_dev;
	udraw_setup_joypad(udraw, input_dev);

	/* touchpad */
	error = -1;
	udraw->touch_input_dev = udraw_setup_touch(udraw, hdev);
	if (!udraw->touch_input_dev)
		goto fail_register_touch_input;
	error = input_register_device(udraw->touch_input_dev);
	if (error)
		goto fail_register_touch_input;
	udraw->touch_input_dev_registered = true;

	/* pen */
	error = -1;
	udraw->pen_input_dev = udraw_setup_pen(udraw, hdev);
	if (!udraw->pen_input_dev)
		goto fail_register_pen_input;
	error = input_register_device(udraw->pen_input_dev);
	if (error)
		goto fail_register_pen_input;
	udraw->pen_input_dev_registered = true;

	/* accelerometer */
	error = -1;
	udraw->accel_input_dev = udraw_setup_accel(udraw, hdev);
	if (!udraw->accel_input_dev)
		goto fail_register_accel_input;
	error = input_register_device(udraw->accel_input_dev);
	if (error)
		goto fail_register_accel_input;
	udraw->accel_input_dev_registered = true;

	return 0;

fail_register_accel_input:
	input_unregister_device(udraw->pen_input_dev);
fail_register_pen_input:
	input_unregister_device(udraw->touch_input_dev);
fail_register_touch_input:
	return error;
}

static int udraw_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	return -1;
}

static int udraw_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct udraw *udraw;

	udraw = kzalloc(sizeof(struct udraw), GFP_KERNEL);
	if (!udraw) {
		ret = -ENOMEM;
		goto allocfail;
	}

	udraw->hdev = hdev;

	/* force input as some remotes bypass the input registration */
	hdev->quirks |= HID_QUIRK_HIDINPUT_FORCE;

	hid_set_drvdata(hdev, udraw);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto fail;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT | HID_CONNECT_HIDDEV_FORCE);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto fail;
	}

	return 0;
fail:
	kfree(udraw);
allocfail:
	return ret;
}

static void udraw_remove(struct hid_device *hdev)
{
	struct udraw *udraw = hid_get_drvdata(hdev);
	hid_hw_stop(hdev);
	/* joy_input_device is always registered and allocated */
	if (udraw->touch_input_dev) {
		if (udraw->touch_input_dev_registered)
			input_unregister_device(udraw->touch_input_dev);
		input_free_device(udraw->touch_input_dev);
	}
	kfree(udraw);
}

//FIXME
#define USB_VENDOR_ID_THQ           0x20d6
#define USB_DEVICE_ID_THQ_PS3_UDRAW 0xcb17

static const struct hid_device_id udraw_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_THQ, USB_DEVICE_ID_THQ_PS3_UDRAW) },
	{ }
};
MODULE_DEVICE_TABLE(hid, udraw_devices);

static struct hid_driver udraw_driver = {
	.name = "hid-udraw",
	.id_table = udraw_devices,
	.raw_event = udraw_raw_event,
	.input_configured = udraw_input_configured,
	.probe = udraw_probe,
	.remove = udraw_remove,
	.input_mapping = udraw_input_mapping,
};
module_hid_driver(udraw_driver);
