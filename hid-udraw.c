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

struct udraw {
	struct input_dev *joy_input_dev;
	struct input_dev *touch_input_dev;
	bool touch_input_dev_registered;
	struct hid_device *hdev;
};
#if 0
static int get_key(int data)
{
	/*
	 * The key is coded accross bits 2..9:
	 *
	 * 0x00 or 0x01 (        )	key:  0		-> KEY_RESERVED
	 * 0x02 or 0x03 (  menu  )	key:  1		-> KEY_MENU
	 * 0x04 or 0x05 (   >"   )	key:  2		-> KEY_PLAYPAUSE
	 * 0x06 or 0x07 (   >>   )	key:  3		-> KEY_FORWARD
	 * 0x08 or 0x09 (   <<   )	key:  4		-> KEY_BACK
	 * 0x0a or 0x0b (    +   )	key:  5		-> KEY_VOLUMEUP
	 * 0x0c or 0x0d (    -   )	key:  6		-> KEY_VOLUMEDOWN
	 * 0x0e or 0x0f (        )	key:  7		-> KEY_RESERVED
	 * 0x50 or 0x51 (        )	key:  8		-> KEY_RESERVED
	 * 0x52 or 0x53 (        )	key:  9		-> KEY_RESERVED
	 * 0x54 or 0x55 (        )	key: 10		-> KEY_RESERVED
	 * 0x56 or 0x57 (        )	key: 11		-> KEY_RESERVED
	 * 0x58 or 0x59 (        )	key: 12		-> KEY_RESERVED
	 * 0x5a or 0x5b (        )	key: 13		-> KEY_RESERVED
	 * 0x5c or 0x5d ( middle )	key: 14		-> KEY_ENTER
	 * 0x5e or 0x5f (   >"   )	key: 15		-> KEY_PLAYPAUSE
	 *
	 * Packets starting with 0x5 are part of a two-packets message,
	 * we notify the caller by sending a negative value.
	 */
	int key = (data >> 1) & KEY_MASK;

	if ((data & TWO_PACKETS_MASK))
		/* Part of a 2 packets-command */
		key = -key;

	return key;
}

static void key_up(struct hid_device *hdev, struct udraw *udraw, int key)
{
	input_report_key(udraw->input_dev, key, 0);
	input_sync(udraw->input_dev);
}

static void key_down(struct hid_device *hdev, struct udraw *udraw, int key)
{
	input_report_key(udraw->input_dev, key, 1);
	input_sync(udraw->input_dev);
}

static void battery_flat(struct udraw *udraw)
{
	dev_err(&udraw->input_dev->dev, "possible flat battery?\n");
}
#endif
static int udraw_raw_event(struct hid_device *hdev, struct hid_report *report,
	 u8 *data, int len)
{
	struct udraw *udraw = hid_get_drvdata(hdev);
	int x, y;

	if (len != 0x1B)
		goto out;

	/* joypad */
	input_report_key(udraw->joy_input_dev, BTN_WEST, data[0] & 1);
	input_report_key(udraw->joy_input_dev, BTN_SOUTH, data[0] & 2);
	input_report_key(udraw->joy_input_dev, BTN_EAST, data[0] & 4);
	input_report_key(udraw->joy_input_dev, BTN_NORTH, data[0] & 8);

	input_report_key(udraw->joy_input_dev, BTN_SELECT, data[1] & 1);
	input_report_key(udraw->joy_input_dev, BTN_START, data[1] & 2);
	input_report_key(udraw->joy_input_dev, BTN_MODE, data[1] & 16);

	//???
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
	/* Finger(s) in use */
	if (data[11] == 0x80 || data[11] >= 191) {
		if (data[15] != 0x0F && data[17] != 0xFF)
			x = data[15] * 256 + data[17];
		if (data[16] != 0x0F && data[18] != 0xFF)
			y = data[16] * 256 + data[18];

		input_report_key(udraw->touch_input_dev, BTN_TOUCH, 1);
		input_report_key(udraw->touch_input_dev, BTN_TOOL_DOUBLETAP,
				(data[11] != 0x80));

		input_report_abs(udraw->touch_input_dev, ABS_X, x);
		input_report_abs(udraw->touch_input_dev, ABS_Y, y);
	} else {
		input_report_key(udraw->touch_input_dev, BTN_TOUCH, 0);
		input_report_key(udraw->touch_input_dev, BTN_TOOL_DOUBLETAP, 0);
	}
	input_sync(udraw->touch_input_dev);
#if 0
	static const u8 keydown[] = { 0x25, 0x87, 0xee };
	static const u8 keyrepeat[] = { 0x26, };
	static const u8 flatbattery[] = { 0x25, 0x87, 0xe0 };
	unsigned long flags;

	if (len != 5)
		goto out;

	if (!memcmp(data, keydown, sizeof(keydown))) {
		int index;

		spin_lock_irqsave(&udraw->lock, flags);
		/*
		 * If we already have a key down, take it up before marking
		 * this one down
		 */
		if (udraw->current_key)
			key_up(hid, udraw, udraw->current_key);

		/* Handle dual packet commands */
		if (udraw->prev_key_idx > 0)
			index = udraw->prev_key_idx;
		else
			index = get_key(data[4]);

		if (index >= 0) {
			udraw->current_key = udraw->keymap[index];

			key_down(hid, udraw, udraw->current_key);
			/*
			 * Remote doesn't do key up, either pull them up, in
			 * the test above, or here set a timer which pulls
			 * them up after 1/8 s
			 */
			udraw->prev_key_idx = 0;
		} else
			/* Remember key for next packet */
			udraw->prev_key_idx = -index;
		spin_unlock_irqrestore(&udraw->lock, flags);
		goto out;
	}

	udraw->prev_key_idx = 0;

	if (!memcmp(data, keyrepeat, sizeof(keyrepeat))) {
		key_down(hid, udraw, udraw->current_key);
		/*
		 * Remote doesn't do key up, either pull them up, in the test
		 * above, or here set a timer which pulls them up after 1/8 s
		 */
		mod_timer(&udraw->key_up_timer, jiffies + HZ / 8);
		goto out;
	}

	if (!memcmp(data, flatbattery, sizeof(flatbattery))) {
		battery_flat(udraw);
		/* Fall through */
	}
#endif
out:
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

static struct input_dev *allocate_and_setup(struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = input_allocate_device();
	if (!input_dev)
		return NULL;

	input_dev->name = hdev->name;
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

	input_dev = allocate_and_setup(hdev);
	if (!input_dev)
		return NULL;

	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);

	set_bit(ABS_X, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_X, 0, 1920, 0, 0);
	set_bit(ABS_Y, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_X, 0, 1080, 0, 0);

	set_bit(BTN_TOOL_DOUBLETAP, input_dev->keybit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	set_bit(INPUT_PROP_POINTER, input_dev->propbit);
	set_bit(INPUT_PROP_BUTTONPAD, input_dev->propbit);

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
	int error = 0;

	/* joypad, uses the hid device */
	udraw->joy_input_dev = input_dev;
	udraw_setup_joypad(udraw, input_dev);

	/* touchpad */
	udraw->touch_input_dev = udraw_setup_touch(udraw, hdev);
	if (!udraw->touch_input_dev)
		return -1;
	error = input_register_device(udraw->touch_input_dev);
	if (error)
		goto fail_register_touch_input;
	udraw->touch_input_dev_registered = true;

	/* pen */

	/* accelerometer */

	//FIXME
	// udraw_setup_pen
	// udraw_setup_accel

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
