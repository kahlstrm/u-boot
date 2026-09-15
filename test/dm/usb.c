// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Google, Inc
 */

#include <console.h>
#include <dm.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <usb.h>
#include <usb_ether.h>
#include <asm/io.h>
#include <asm/state.h>
#include <asm/test.h>
#include <dm/device-internal.h>
#include <dm/test.h>
#include <dm/uclass-internal.h>
#include <test/test.h>
#include <test/ut.h>

struct keyboard_test_data {
	const char modifiers;
	const char scancode;
	const char result[6];
};

static int dm_test_usb_descriptor_accessors(struct unit_test_state *uts)
{
	ALLOC_CACHE_ALIGN_BUFFER(struct usb_device, devices, 2);
	struct usb_interface *iface, *copy;
	unsigned int i, ep;

	memset(devices, 0, 2 * sizeof(*devices));
	devices[0].config.no_of_if = USB_MAXINTERFACES;
	for (i = 0; i < USB_MAXINTERFACES; i++) {
		iface = usb_get_interface(&devices[0], i);
		ut_asserteq_ptr(&devices[0].config.if_desc[i], iface);
		iface->desc.bInterfaceNumber = i + 1;
		iface->no_of_ep = USB_MAXENDPOINTS;
		ut_asserteq_ptr(&iface->desc, usb_get_interface_desc(iface));
		ut_asserteq(i + 1,
			    usb_get_interface_desc(iface)->bInterfaceNumber);
		for (ep = 0; ep < USB_MAXENDPOINTS; ep++) {
			ut_asserteq_ptr(&iface->ep_desc[ep],
					usb_get_endpoint_desc(iface, ep));
			ut_asserteq_ptr(&iface->ss_ep_comp_desc[ep],
					usb_get_ss_ep_comp_desc(iface, ep));
		}
	}

	devices[1] = devices[0];
	iface = usb_get_interface(&devices[0], USB_MAXINTERFACES - 1);
	copy = usb_get_interface(&devices[1], USB_MAXINTERFACES - 1);
	usb_get_interface_desc(copy)->bInterfaceNumber = 42;
	usb_get_endpoint_desc(copy, USB_MAXENDPOINTS - 1)->bInterval = 7;
	usb_get_ss_ep_comp_desc(copy, USB_MAXENDPOINTS - 1)->bMaxBurst = 3;
	ut_asserteq(USB_MAXINTERFACES, iface->desc.bInterfaceNumber);
	ut_asserteq(0, iface->ep_desc[USB_MAXENDPOINTS - 1].bInterval);
	ut_asserteq(0, iface->ss_ep_comp_desc[USB_MAXENDPOINTS - 1].bMaxBurst);
	ut_asserteq(42, copy->desc.bInterfaceNumber);
	ut_asserteq(7, copy->ep_desc[USB_MAXENDPOINTS - 1].bInterval);
	ut_asserteq(3, copy->ss_ep_comp_desc[USB_MAXENDPOINTS - 1].bMaxBurst);

	return 0;
}

DM_TEST(dm_test_usb_descriptor_accessors, 0);

static int dm_test_usb_interface_number(struct unit_test_state *uts)
{
	ALLOC_CACHE_ALIGN_BUFFER(struct usb_device, udev, 1);

	memset(udev, 0, sizeof(*udev));
	udev->config.desc.bNumInterfaces = 2;
	udev->config.no_of_if = 2;
	udev->config.if_desc[0].desc.bInterfaceNumber = 3;
	udev->config.if_desc[0].num_altsetting = 1;
	udev->config.if_desc[1].desc.bInterfaceNumber = 7;
	udev->config.if_desc[1].num_altsetting = 1;
	ut_assertok(usb_set_interface(udev, 3, 0));
	ut_assertok(usb_set_interface(udev, 7, 0));
	ut_asserteq(-EINVAL, usb_set_interface(udev, 0, 0));
	ut_asserteq(-EINVAL, usb_set_interface(udev, 1, 0));

	return 0;
}

DM_TEST(dm_test_usb_interface_number, 0);

static int dm_test_usb_device_accessor(struct unit_test_state *uts)
{
	struct udevice *dev;

	state_set_skip_delays(true);
	ut_assertok(usb_init());
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 0, &dev));
	ut_assertnonnull(usb_get_device(dev));
	ut_asserteq_ptr(dev_get_parent_priv(dev), usb_get_device(dev));
	ut_assertok(uclass_get_device_by_name(UCLASS_KEYBOARD, "usb_kbd", &dev));
	ut_assertnonnull(usb_get_device(dev));
	ut_asserteq_ptr(dev_get_parent_priv(dev), usb_get_device(dev));
	ut_assertok(uclass_get_device(UCLASS_USB_HUB, 0, &dev));
	ut_assertnonnull(usb_get_device(dev));
	ut_asserteq_ptr(dev_get_parent_priv(dev), usb_get_device(dev));
	ut_assertok(usb_stop());

	return 0;
}

DM_TEST(dm_test_usb_device_accessor, UTF_SCAN_PDATA | UTF_SCAN_FDT);

#if CONFIG_IS_ENABLED(USB_HOST_ETHER)
static int dm_test_usb_ether_endpoints(struct unit_test_state *uts)
{
	struct ueth_data ueth = {};
	struct usb_interface saved, *iface;
	struct usb_device *udev;
	struct udevice *dev;
	int ret;

	state_set_skip_delays(true);
	ut_assertok(usb_init());
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 0, &dev));
	udev = usb_get_device(dev);
	iface = usb_get_interface(udev, 0);
	saved = *iface;
	/* Supply Ethernet-shaped descriptors without emulating network traffic. */
	memset(iface, 0, sizeof(*iface));
	iface->desc.bNumEndpoints = 3;
	iface->num_altsetting = 1;
	iface->no_of_ep = 3;
	iface->ep_desc[0].bEndpointAddress = USB_DIR_IN | 3;
	iface->ep_desc[0].bmAttributes = USB_ENDPOINT_XFER_INT;
	iface->ep_desc[0].bInterval = 7;
	iface->ep_desc[1].bEndpointAddress = 2;
	iface->ep_desc[1].bmAttributes = USB_ENDPOINT_XFER_BULK;
	iface->ep_desc[2].bEndpointAddress = USB_DIR_IN | 1;
	iface->ep_desc[2].bmAttributes = USB_ENDPOINT_XFER_BULK;
	ret = usb_ether_register(dev, &ueth, 512);
	*iface = saved;
	free(ueth.rxbuf);
	ueth.rxbuf = NULL;
	ut_assertok(ret);
	ut_asserteq(0, ueth.ifnum);
	ut_asserteq(1, ueth.ep_in);
	ut_asserteq(2, ueth.ep_out);
	ut_asserteq(3, ueth.ep_int);
	ut_asserteq(7, ueth.irqinterval);
	ut_asserteq_ptr(udev, ueth.pusb_dev);
	ut_assertok(usb_stop());

	return 0;
}

DM_TEST(dm_test_usb_ether_endpoints, UTF_SCAN_PDATA | UTF_SCAN_FDT);
#endif

/* Test that sandbox USB works correctly */
static int dm_test_usb_base(struct unit_test_state *uts)
{
	struct udevice *bus;

	ut_asserteq(-ENODEV, uclass_get_device_by_seq(UCLASS_USB, 0, &bus));
	ut_assertok(uclass_get_device(UCLASS_USB, 0, &bus));
	ut_asserteq(-ENODEV, uclass_get_device_by_seq(UCLASS_USB, 2, &bus));

	return 0;
}
DM_TEST(dm_test_usb_base, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test that we can use the flash stick. This is more of a functional test. It
 * covers scanning the bug, setting up a hub and a flash stick and reading
 * data from the flash stick.
 */
static int dm_test_usb_flash(struct unit_test_state *uts)
{
	struct blk_desc *dev_desc, *chk;
	struct udevice *dev, *blk;
	char cmp[1024];

	state_set_skip_delays(true);
	ut_assertok(usb_init());
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 0, &dev));
	ut_assertok(blk_get_device_by_str("usb", "0", &dev_desc));
	chk = blk_get_by_device(dev);
	ut_asserteq_ptr(chk, dev_desc);

	ut_assertok(device_find_first_child_by_uclass(dev, UCLASS_BLK, &blk));
	ut_asserteq_ptr(chk, blk_get_by_device(dev));

	/* Read a few blocks and look for the string we expect */
	ut_asserteq(512, dev_desc->blksz);
	memset(cmp, '\0', sizeof(cmp));
	ut_asserteq(2, blk_read(blk, 0, 2, cmp));
	ut_asserteq_str("this is a test", cmp);

	strcpy(cmp, "another test");
	ut_asserteq(1, blk_write(blk, 1, 1, cmp));

	memset(cmp, '\0', sizeof(cmp));
	ut_asserteq(2, blk_read(blk, 0, 2, cmp));
	ut_asserteq_str("this is a test", cmp);
	ut_asserteq_str("another test", cmp + 512);

	memset(cmp, '\0', sizeof(cmp));
	ut_asserteq(1, blk_write(blk, 1, 1, cmp));

	memset(cmp, '\0', sizeof(cmp));
	ut_asserteq(2, blk_read(blk, 0, 2, cmp));
	ut_asserteq_str("this is a test", cmp);
	ut_asserteq_str("", cmp + 512);

	ut_assertok(usb_stop());

	return 0;
}
DM_TEST(dm_test_usb_flash, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* test that we can handle multiple storage devices */
static int dm_test_usb_multi(struct unit_test_state *uts)
{
	struct udevice *dev;

	state_set_skip_delays(true);
	ut_assertok(usb_init());
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 0, &dev));
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 1, &dev));
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 2, &dev));
	ut_assertok(usb_stop());

	return 0;
}
DM_TEST(dm_test_usb_multi, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* test that we have an associated ofnode with the usb device */
static int dm_test_usb_fdt_node(struct unit_test_state *uts)
{
	struct udevice *dev;
	ofnode node;

	state_set_skip_delays(true);
	ut_assertok(usb_init());
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 0, &dev));
	node = ofnode_path("/usb@1/hub/usbstor@1");
	ut_asserteq(1, ofnode_equal(node, dev_ofnode(dev)));
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 1, &dev));
	ut_asserteq(1, ofnode_equal(ofnode_null(), dev_ofnode(dev)));
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 2, &dev));
	node = ofnode_path("/usb@1/hub/usbstor@3");
	ut_asserteq(1, ofnode_equal(node, dev_ofnode(dev)));
	ut_assertok(usb_stop());

	return 0;
}
DM_TEST(dm_test_usb_fdt_node, UTF_SCAN_PDATA | UTF_SCAN_FDT);

static int count_usb_devices(void)
{
	struct udevice *hub;
	struct uclass *uc;
	int count = 0;
	int ret;

	ret = uclass_get(UCLASS_USB_HUB, &uc);
	if (ret)
		return ret;

	uclass_foreach_dev(hub, uc) {
		struct udevice *dev;

		count++;
		for (device_find_first_child(hub, &dev);
		     dev;
		     device_find_next_child(&dev)) {
			count++;
		}
	}

	return count;
}

/* test that no USB devices are found after we stop the stack */
static int dm_test_usb_stop(struct unit_test_state *uts)
{
	struct udevice *dev;

	/* Scan and check that all devices are present */
	state_set_skip_delays(true);
	ut_assertok(usb_init());
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 0, &dev));
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 1, &dev));
	ut_assertok(uclass_get_device(UCLASS_MASS_STORAGE, 2, &dev));
	ut_asserteq(6, count_usb_devices());
	ut_assertok(usb_stop());
	ut_asserteq(0, count_usb_devices());

	return 0;
}
DM_TEST(dm_test_usb_stop, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/**
 * dm_test_usb_keyb() - test USB keyboard driver
 *
 * This test copies USB keyboard scan codes into the key buffer of the USB
 * keyboard emulation driver. These are picked up during emulated interrupts
 * by the USB keyboard driver and converted to characters and escape sequences.
 * The test then reads and verifies these characters and escape sequences from
 * the standard input.
 *
 * TODO: The following features are not yet tested:
 *
 * * LED status
 * * caps-lock
 * * num-lock
 * * numerical pad keys
 *
 * TODO: The following features are not yet implemented by the USB keyboard
 * driver and therefore not tested:
 *
 * * modifiers for non-alpha-numeric keys, e.g. <SHIFT><TAB> and <ALT><F4>
 * * some special keys, e.g. <PRINT>
 * * some modifiers, e.g. <ALT> and <META>
 * * alternative keyboard layouts
 *
 * @uts:	unit test state
 * Return:	0 on success
 */
static int dm_test_usb_keyb(struct unit_test_state *uts)
{
	struct udevice *dev;
	const struct keyboard_test_data *pos;
	const struct keyboard_test_data kbd_test_data[] = {
		/* <A> */
		{0x00, 0x04, "a"},
		/* <B> */
		{0x00, 0x05, "b"},
		/* <C> */
		{0x00, 0x06, "c"},
		/* <D> */
		{0x00, 0x07, "d"},
		/* <E> */
		{0x00, 0x08, "e"},
		/* <F> */
		{0x00, 0x09, "f"},
		/* <G> */
		{0x00, 0x0a, "g"},
		/* <H> */
		{0x00, 0x0b, "h"},
		/* <I> */
		{0x00, 0x0c, "i"},
		/* <J> */
		{0x00, 0x0d, "j"},
		/* <K> */
		{0x00, 0x0e, "k"},
		/* <L> */
		{0x00, 0x0f, "l"},
		/* <M> */
		{0x00, 0x10, "m"},
		/* <N> */
		{0x00, 0x11, "n"},
		/* <O> */
		{0x00, 0x12, "o"},
		/* <P> */
		{0x00, 0x13, "p"},
		/* <Q> */
		{0x00, 0x14, "q"},
		/* <R> */
		{0x00, 0x15, "r"},
		/* <S> */
		{0x00, 0x16, "s"},
		/* <T> */
		{0x00, 0x17, "t"},
		/* <U> */
		{0x00, 0x18, "u"},
		/* <V> */
		{0x00, 0x19, "v"},
		/* <W> */
		{0x00, 0x1a, "w"},
		/* <X> */
		{0x00, 0x1b, "x"},
		/* <Y> */
		{0x00, 0x1c, "y"},
		/* <Z> */
		{0x00, 0x1d, "z"},

		/* <LEFT-SHIFT><A> */
		{0x02, 0x04, "A"},
		/* <RIGHT-SHIFT><Z> */
		{0x20, 0x1d, "Z"},

		/* <LEFT-CONTROL><A> */
		{0x01, 0x04, "\x01"},
		/* <RIGHT-CONTROL><Z> */
		{0x10, 0x1d, "\x1a"},

		/* <1> */
		{0x00, 0x1e, "1"},
		/* <2> */
		{0x00, 0x1f, "2"},
		/* <3> */
		{0x00, 0x20, "3"},
		/* <4> */
		{0x00, 0x21, "4"},
		/* <5> */
		{0x00, 0x22, "5"},
		/* <6> */
		{0x00, 0x23, "6"},
		/* <7> */
		{0x00, 0x24, "7"},
		/* <8> */
		{0x00, 0x25, "8"},
		/* <9> */
		{0x00, 0x26, "9"},
		/* <0> */
		{0x00, 0x27, "0"},

		/* <LEFT-SHIFT><1> */
		{0x02, 0x1e, "!"},
		/* <RIGHT-SHIFT><2> */
		{0x20, 0x1f, "@"},
		/* <LEFT-SHIFT><3> */
		{0x02, 0x20, "#"},
		/* <RIGHT-SHIFT><4> */
		{0x20, 0x21, "$"},
		/* <LEFT-SHIFT><5> */
		{0x02, 0x22, "%"},
		/* <RIGHT-SHIFT><6> */
		{0x20, 0x23, "^"},
		/* <LEFT-SHIFT><7> */
		{0x02, 0x24, "&"},
		/* <RIGHT-SHIFT><8> */
		{0x20, 0x25, "*"},
		/* <LEFT-SHIFT><9> */
		{0x02, 0x26, "("},
		/* <RIGHT-SHIFT><0> */
		{0x20, 0x27, ")"},

		/* <ENTER> */
		{0x00, 0x28, "\r"},
		/* <ESCAPE> */
		{0x00, 0x29, "\x1b"},
		/* <BACKSPACE> */
		{0x00, 0x2a, "\x08"},
		/* <TAB> */
		{0x00, 0x2b, "\x09"},
		/* <SPACE> */
		{0x00, 0x2c, " "},
		/* <MINUS> */
		{0x00, 0x2d, "-"},
		/* <EQUAL> */
		{0x00, 0x2e, "="},
		/* <LEFT BRACE> */
		{0x00, 0x2f, "["},
		/* <RIGHT BRACE> */
		{0x00, 0x30, "]"},
		/* <BACKSLASH> */
		{0x00, 0x31, "\\"},
		/* <HASH-TILDE> */
		{0x00, 0x32, "#"},
		/* <SEMICOLON> */
		{0x00, 0x33, ";"},
		/* <APOSTROPHE> */
		{0x00, 0x34, "'"},
		/* <GRAVE> */
		{0x00, 0x35, "`"},
		/* <COMMA> */
		{0x00, 0x36, ","},
		/* <DOT> */
		{0x00, 0x37, "."},
		/* <SLASH> */
		{0x00, 0x38, "/"},

		/* <LEFT-SHIFT><ENTER> */
		{0x02, 0x28, "\r"},
		/* <RIGHT-SHIFT><ESCAPE> */
		{0x20, 0x29, "\x1b"},
		/* <LEFT-SHIFT><BACKSPACE> */
		{0x02, 0x2a, "\x08"},
		/* <RIGHT-SHIFT><TAB> */
		{0x20, 0x2b, "\x09"},
		/* <LEFT-SHIFT><SPACE> */
		{0x02, 0x2c, " "},
		/* <MINUS> */
		{0x20, 0x2d, "_"},
		/* <LEFT-SHIFT><EQUAL> */
		{0x02, 0x2e, "+"},
		/* <RIGHT-SHIFT><LEFT BRACE> */
		{0x20, 0x2f, "{"},
		/* <LEFT-SHIFT><RIGHT BRACE> */
		{0x02, 0x30, "}"},
		/* <RIGHT-SHIFT><BACKSLASH> */
		{0x20, 0x31, "|"},
		/* <LEFT-SHIFT><HASH-TILDE> */
		{0x02, 0x32, "~"},
		/* <RIGHT-SHIFT><SEMICOLON> */
		{0x20, 0x33, ":"},
		/* <LEFT-SHIFT><APOSTROPHE> */
		{0x02, 0x34, "\""},
		/* <RIGHT-SHIFT><GRAVE> */
		{0x20, 0x35, "~"},
		/* <LEFT-SHIFT><COMMA> */
		{0x02, 0x36, "<"},
		/* <RIGHT-SHIFT><DOT> */
		{0x20, 0x37, ">"},
		/* <LEFT-SHIFT><SLASH> */
		{0x02, 0x38, "?"},
#ifdef CONFIG_USB_KEYBOARD_FN_KEYS
		/* <F1> */
		{0x00, 0x3a, "\x1bOP"},
		/* <F2> */
		{0x00, 0x3b, "\x1bOQ"},
		/* <F3> */
		{0x00, 0x3c, "\x1bOR"},
		/* <F4> */
		{0x00, 0x3d, "\x1bOS"},
		/* <F5> */
		{0x00, 0x3e, "\x1b[15~"},
		/* <F6> */
		{0x00, 0x3f, "\x1b[17~"},
		/* <F7> */
		{0x00, 0x40, "\x1b[18~"},
		/* <F8> */
		{0x00, 0x41, "\x1b[19~"},
		/* <F9> */
		{0x00, 0x42, "\x1b[20~"},
		/* <F10> */
		{0x00, 0x43, "\x1b[21~"},
		/* <F11> */
		{0x00, 0x44, "\x1b[23~"},
		/* <F12> */
		{0x00, 0x45, "\x1b[24~"},
		/* <INSERT> */
		{0x00, 0x49, "\x1b[2~"},
		/* <HOME> */
		{0x00, 0x4a, "\x1b[H"},
		/* <PAGE UP> */
		{0x00, 0x4b, "\x1b[5~"},
		/* <DELETE> */
		{0x00, 0x4c, "\x1b[3~"},
		/* <END> */
		{0x00, 0x4d, "\x1b[F"},
		/* <PAGE DOWN> */
		{0x00, 0x4e, "\x1b[6~"},
		/* <RIGHT> */
		{0x00, 0x4f, "\x1b[C"},
		/* <LEFT> */
		{0x00, 0x50, "\x1b[D"},
		/* <DOWN> */
		{0x00, 0x51, "\x1b[B"},
		/* <UP> */
		{0x00, 0x52, "\x1b[A"},
#endif /* CONFIG_USB_KEYBOARD_FN_KEYS */

		/* End of list */
		{0x00, 0x00, "\0"}
	};

	state_set_skip_delays(true);
	ut_assertok(usb_init());

	/* Initially there should be no characters */
	ut_asserteq(0, tstc());

	ut_assertok(uclass_get_device_by_name(UCLASS_USB_EMUL, "keyb@3",
					      &dev));

	/*
	 * Add scan codes to the USB keyboard buffer. They should appear as
	 * corresponding characters and escape sequences in stdin.
	 */
	for (pos = kbd_test_data; pos->scancode; ++pos) {
		const char *c;
		char scancodes[USB_KBD_BOOT_REPORT_SIZE] = {0};

		scancodes[0] = pos->modifiers;
		scancodes[2] = pos->scancode;

		ut_assertok(sandbox_usb_keyb_add_string(dev, scancodes));

		for (c = pos->result; *c; ++c) {
			ut_asserteq(1, tstc());
			ut_asserteq(*c, getchar());
		}
		ut_asserteq(0, tstc());
	}
	ut_assertok(usb_stop());

	return 0;
}
DM_TEST(dm_test_usb_keyb, UTF_SCAN_PDATA | UTF_SCAN_FDT);
