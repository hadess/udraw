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
#include "hid-ids.h"

MODULE_AUTHOR("Bastien Nocera <hadess@hadess.net>");
MODULE_DESCRIPTION("PS3 uDraw tablet driver");
MODULE_LICENSE("GPL");

#define KEY_MASK		0x0F
#define TWO_PACKETS_MASK	0x40

/*
 * Protocol information from:
 * http://brandonw.net/udraw/
 */

static const unsigned short udraw_key_table[] = {
	KEY_RESERVED,
	KEY_MENU,
	KEY_PLAYPAUSE,
	KEY_FORWARD,
	KEY_BACK,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_ENTER,
	KEY_PLAYPAUSE,
	KEY_RESERVED,
};

struct udraw {
	struct input_dev *input_dev;
	struct hid_device *hid;
	unsigned short keymap[ARRAY_SIZE(udraw_key_table)];
	spinlock_t lock;		/* protects .current_key */
	int current_key;		/* the currently pressed key */
	int prev_key_idx;		/* key index in a 2 packets message */
};

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

static void key_up(struct hid_device *hid, struct udraw *udraw, int key)
{
	input_report_key(udraw->input_dev, key, 0);
	input_sync(udraw->input_dev);
}

static void key_down(struct hid_device *hid, struct udraw *udraw, int key)
{
	input_report_key(udraw->input_dev, key, 1);
	input_sync(udraw->input_dev);
}

static void battery_flat(struct udraw *udraw)
{
	dev_err(&udraw->input_dev->dev, "possible flat battery?\n");
}

static void key_up_tick(unsigned long data)
{
	struct udraw *udraw = (struct udraw *)data;
	struct hid_device *hid = udraw->hid;
	unsigned long flags;

	spin_lock_irqsave(&udraw->lock, flags);
	if (udraw->current_key) {
		key_up(hid, udraw, udraw->current_key);
		udraw->current_key = 0;
	}
	spin_unlock_irqrestore(&udraw->lock, flags);
}

static int udraw_raw_event(struct hid_device *hid, struct hid_report *report,
	 u8 *data, int len)
{
	struct udraw *udraw = hid_get_drvdata(hid);

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

static int udraw_input_configured(struct hid_device *hid,
		struct hid_input *hidinput)
{
	struct input_dev *input_dev = hidinput->input;
	struct udraw *udraw = hid_get_drvdata(hid);
	int i;

	udraw->input_dev = input_dev;

	input_dev->keycode = udraw->keymap;
	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = ARRAY_SIZE(udraw->keymap);

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);

	memcpy(udraw->keymap, udraw_key_table, sizeof(udraw->keymap));
	for (i = 0; i < ARRAY_SIZE(udraw_key_table); i++)
		set_bit(udraw->keymap[i], input_dev->keybit);
	clear_bit(KEY_RESERVED, input_dev->keybit);

	return 0;
}

static int udraw_input_mapping(struct hid_device *hid,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	return -1;
}

static int udraw_probe(struct hid_device *hid, const struct hid_device_id *id)
{
	int ret;
	struct udraw *udraw;

	udraw = kzalloc(sizeof(struct udraw), GFP_KERNEL);
	if (!udraw) {
		ret = -ENOMEM;
		goto allocfail;
	}

	udraw->hid = hid;

	/* force input as some remotes bypass the input registration */
	hid->quirks |= HID_QUIRK_HIDINPUT_FORCE;

#if 0
	spin_lock_init(&udraw->lock);
	setup_timer(&udraw->key_up_timer,
		    key_up_tick, (unsigned long) udraw);
#endif
	hid_set_drvdata(hid, udraw);

	ret = hid_parse(hid);
	if (ret) {
		hid_err(hid, "parse failed\n");
		goto fail;
	}

	ret = hid_hw_start(hid, HID_CONNECT_DEFAULT | HID_CONNECT_HIDDEV_FORCE);
	if (ret) {
		hid_err(hid, "hw start failed\n");
		goto fail;
	}

	return 0;
fail:
	kfree(udraw);
allocfail:
	return ret;
}

static void udraw_remove(struct hid_device *hid)
{
	struct udraw *udraw = hid_get_drvdata(hid);
	hid_hw_stop(hid);
	//del_timer_sync(&udraw->key_up_timer);
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
	.name = "udraw",
	.id_table = udraw_devices,
	.raw_event = udraw_raw_event,
	.input_configured = udraw_input_configured,
	.probe = udraw_probe,
	.remove = udraw_remove,
	.input_mapping = udraw_input_mapping,
};
module_hid_driver(udraw_driver);
