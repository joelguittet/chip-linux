/*
 * Copyright (C) 2015 - Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_fdt.h>

#include "cape-manager.h"

LIST_HEAD(cape_list);
DEFINE_SPINLOCK(cape_lock);

#define CAPE_MANAGER_MAGIC	0x43484950	/* CHIP */

struct cape {
	struct list_head	head;

	struct cape_header	*header;

	unsigned int		loaded:1;

	char			*dtbo;
	const struct firmware	*fw;
	struct device_node	*overlay;
	int			overlay_id;
};

static int cape_manager_check_overlay(struct device *dev, struct cape *cape)
{
	 struct property *p;
	 const char *s = NULL;
	 bool compatible = false;

	 p = of_find_property(cape->overlay, "compatible", NULL);
	 if (!p) {
		dev_err(dev, "Missing compatible property in %s\n",
			 cape->dtbo);
		return -EINVAL;
	 }

	 do {
		s = of_prop_next_string(p, s);
		if (of_machine_is_compatible(s)) {
			compatible = true;
			break;
		}
	 } while(s);

	if (!compatible) {
		dev_err(dev, "Incompatible overlay\n");
		return -EINVAL;
	 }

	return 0;
}

static int cape_manager_load(struct device *dev, struct cape *cape)
{
	int err;

	if (cape->loaded) {
		dev_err(dev, "Overlay already loaded for cape %s\n",
			cape->header->product_name);
		return -EAGAIN;
	}

	cape->dtbo = kasprintf(GFP_KERNEL, "cape-%x-%x-%x.dtbo",
			cape->header->vendor_id, cape->header->product_id,
			cape->header->product_version);
	if (!cape->dtbo)
		return -ENOMEM;

	err = request_firmware_direct(&cape->fw, cape->dtbo, dev);
	if (err) {
		dev_err(dev, "Could not find overlay %s for cape %s\n",
			cape->dtbo, cape->header->product_name);
		return err;
	}

	of_fdt_unflatten_tree((unsigned long *)cape->fw->data, &cape->overlay);
	if (!cape->overlay) {
		dev_err(dev, "Could not unflatten %s\n", cape->dtbo);
		err = -EINVAL;
		goto err;
	}

	of_node_set_flag(cape->overlay, OF_DETACHED);

	err = of_resolve_phandles(cape->overlay);
	if (err) {
		dev_err(dev, "Could not resolve phandles for overlay %s (%d)\n",
			cape->dtbo, err);
		goto err;
	}

	if (cape_manager_check_overlay(dev, cape))
		goto err;

	cape->overlay_id = of_overlay_create(cape->overlay);
	if (cape->overlay_id < 0) {
		dev_err(dev, "Could not apply overlay %s for cape %s\n",
			cape->dtbo, cape->header->product_name);
		err = cape->overlay_id;
		goto err;
	}

	cape->loaded = 1;
	return 0;

err:
	cape->overlay = NULL;
	release_firmware(cape->fw);
	return err;
}

/*
 * Called by an id provider when a new id is detected. The id is stored and an
 * overlay is applied if it matches the id.
 */
void cape_manager_insert(struct device *dev, struct cape_header *header)
{
	struct list_head *pos, *n;
	struct cape *cape;

	if (header->magic != CAPE_MANAGER_MAGIC) {
		dev_err(dev, "Bad magic value (%x)\n", header->magic);
		return;
	}

	/* a few sanitary modifications */
	header->vendor_name[31] = '\0';
	header->product_name[31] = '\0';

#if 0
	/* debug */
	pr_err("magic: 0x%x\n", header->magic);
	pr_err("version: %d\n", header->version);
	pr_err("vendor id: 0x%x\n", header->vendor_id);
	pr_err("product id: 0x%x\n", header->product_id);
	pr_err("product version: %d\n", header->product_version);
	pr_err("vendor name: %s\n", header->vendor_name);
	pr_err("product name: %s\n", header->product_name);
#endif

	spin_lock(&cape_lock);

	list_for_each_safe(pos, n, &cape_list) {
		cape = list_entry(pos, struct cape, head);

		if (cape->header->vendor_id == header->vendor_id &&
		    cape->header->product_id == header->product_id &&
		    cape->header->product_version == header->product_version) {
			dev_err(dev, "Cape already loaded\n");
			goto err;
		}
	}

	cape = devm_kzalloc(dev, sizeof(*cape), GFP_KERNEL);
	if (!cape)
		goto err;

	cape->loaded = 0;
	cape->header = header;

	if (cape_manager_load(dev, cape)) {
		dev_err(dev, "Couldn't load cape %s\n", header->product_name);
		goto err;
	}

	list_add_tail(&cape->head, &cape_list);

	dev_info(dev, "Overlay %s for cape %s applied!\n", cape->dtbo,
		 header->product_name);

err:
	spin_unlock(&cape_lock);
}
EXPORT_SYMBOL_GPL(cape_manager_insert);
