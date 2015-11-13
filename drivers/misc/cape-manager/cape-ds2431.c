/*
 * Copyright (C) 2015 - Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "cape-manager.h"
#include "../../w1/w1.h"
#include "../../w1/w1_family.h"

#define W1_F2D_READ_EEPROM      0xF0
#define CAPE_DS2431_CHUNK	8
#define CAPE_DS2431_MAX_TRIES	10

/* FIXME: non global definition */
struct device *plop;

static int cape_ds2431_read(struct w1_slave *sl, int offset, u8 *buf)
{
	int tries = CAPE_DS2431_MAX_TRIES;
	u8 wrbuf[3], cmp[CAPE_DS2431_CHUNK];

	do {
		wrbuf[0] = W1_F2D_READ_EEPROM;
		wrbuf[1] = offset & 0xff;
		wrbuf[2] = offset >> 8;

		if (w1_reset_select_slave(sl))
			return -1;

		w1_write_block(sl->master, wrbuf, 3);
		w1_read_block(sl->master, buf, CAPE_DS2431_CHUNK);

		if (w1_reset_select_slave(sl))
			return -1;

		w1_write_block(sl->master, wrbuf, 3);
		w1_read_block(sl->master, cmp, CAPE_DS2431_CHUNK);

		if (!memcmp(cmp, buf, CAPE_DS2431_CHUNK))
			return 0;
	} while (tries--);

	dev_err(plop, "Read failed.\n");
	return -1;
}

int cape_ds2431_callback(struct w1_slave *sl)
{
	struct cape_header *header;
	int i;

	header = devm_kzalloc(plop, sizeof(struct cape_header), GFP_KERNEL);
	if (!header)
		return -ENOMEM;

	/* 
	 * Here, sizeof(struct cape_header) is a multiple of CAPE_DS2431_CHUNK
	 * FIXME: do a proper solution
	 */
	for (i = 0; i < sizeof(struct cape_header); i += CAPE_DS2431_CHUNK) {
		if (cape_ds2431_read(sl, i, &((u8 *)header)[i]))
			return -EIO;
	}

	cape_manager_insert(plop, header);

	return 0;
}

static struct w1_family_ops w1_f2d_fops = {
	.callback = &cape_ds2431_callback,
};

static struct w1_family w1_family_2d = {
	.fid	= W1_EEPROM_DS2431,
	.fops	= &w1_f2d_fops,
};

static int cape_ds2431_probe(struct platform_device *pdev)
{
	plop = &pdev->dev;
	w1_register_family(&w1_family_2d);

	return 0;
}

static int cape_ds2431_remove(struct platform_device *pdev)
{
	w1_unregister_family(&w1_family_2d);

	return 0;
}

static struct platform_driver capemgr_driver = {
	.probe	= cape_ds2431_probe,
	.remove	= cape_ds2431_remove,
	.driver	= {
		.name = "cape-ds2431",
		.owner = THIS_MODULE,
	},
};
module_platform_driver(capemgr_driver);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_DESCRIPTION("Cape manager ID provider from a DS2431 EEPROM");
MODULE_LICENSE("GPL v2");
