/*
 *  Android virtual input device
 *
 *  Copyright (c) 2011 Philip Åkesson <philip.akesson@gmail.com>
 *
 *  Based on vnckbd.c, Copyright (c) 2010 Danke Xie <danke.xie@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#define __UNUSED __attribute__((unused))

#define X_AXIS_MAX		640
#define Y_AXIS_MAX		480

static unsigned int keycode_list[KEY_MAX]; /* allow all keycodes */

struct android_input {
	unsigned int keycode[ARRAY_SIZE(keycode_list)];
	struct input_dev *input;
	int suspended; /*. need? */
	spinlock_t lock;
};

#ifdef CONFIG_INPUT_ANDROID
struct platform_device android_input_device = {
	.name	= "android-input",
	.id	= -1,
};
#endif /* CONFIG_INPUT_ANDROID */

static int __devinit android_input_probe(struct platform_device *pdev) 
{
	int i;
	struct android_input *android_input;
	struct input_dev *input_dev;
	int error;

	printk(KERN_INFO "android-input: Probing\n");

	android_input = kzalloc(sizeof(struct android_input), GFP_KERNEL);
	if (!android_input) {
		return -ENOMEM;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		kfree(android_input);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, android_input);

	spin_lock_init(&android_input->lock);

	android_input->input = input_dev;

	input_set_drvdata(input_dev, android_input);
	input_dev->name = "Android virtual input";
	input_dev->phys = "android/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_capability(input_dev, EV_ABS, ABS_X);
	input_set_capability(input_dev, EV_ABS, ABS_Y);
	input_set_abs_params(input_dev, ABS_X, 0, X_AXIS_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, Y_AXIS_MAX, 0, 0);
	
	input_dev->keycode = android_input->keycode;
	input_dev->keycodesize = sizeof(unsigned int);
	input_dev->keycodemax = ARRAY_SIZE(keycode_list);

	/* one-to-one mapping from scancode to keycode */
	for (i = 0; i < ARRAY_SIZE(keycode_list); i++) {
		keycode_list[i] = i;
	}

	memcpy(android_input->keycode, keycode_list, sizeof(keycode_list));

	for (i = 0; i < ARRAY_SIZE(keycode_list); i++)
		__set_bit(android_input->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	error = input_register_device(input_dev);
	if (error) {
		printk(KERN_ERR "android-input: Unable to register input device, "
				"error: %d\n", error);
		goto fail;
	}

	printk(KERN_INFO "android-input: Registered\n");

	return 0;

fail:
	platform_set_drvdata(pdev, NULL);
	input_free_device(input_dev);
	kfree(android_input);

	return error;
}

static int __devexit android_input_remove(struct platform_device *dev)
{
	struct android_input *android_input = platform_get_drvdata(dev);

	input_unregister_device(android_input->input);

	kfree(android_input);

	return 0;
}

static struct platform_driver android_input_driver = {
	.probe		= android_input_probe,
	.remove		= __devexit_p(android_input_remove),
	.driver		= {
		.name	= "android-input",
		.owner	= THIS_MODULE,
	},
};

static int __devinit android_input_init(void)
{
	int rc;

	rc = platform_driver_register(&android_input_driver);
	if (rc) return rc;

#ifdef CONFIG_INPUT_ANDROID
	rc = platform_device_register(&vnc_keyboard_device);
#endif

	return rc;
}

static void __exit android_input_exit(void)
{
	platform_driver_unregister(&android_input_driver);
}

module_init(android_input_init);
module_exit(android_input_exit);

MODULE_AUTHOR("Philip Åkesson <philip.akesson@gmail.com>");
MODULE_AUTHOR("Danke Xie <danke.xie@gmail.com>");
MODULE_DESCRIPTION("Android Input Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:android-input");
