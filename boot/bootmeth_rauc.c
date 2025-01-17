// SPDX-License-Identifier: GPL-2.0+
/*
 * Bootmethod for distro boot with RAUC
 *
 * Copyright 2025 PHYTEC Messtechnik GmbH
 * Written by Martin Schwan <m.schwan@phytec.de>
 */

#define LOG_CATEGORY UCLASS_BOOTSTD
#define LOG_DEBUG

#include <asm/cache.h>
#include <blk.h>
#include <bootflow.h>
#include <bootmeth.h>
#include <bootstd.h>
#include <dm.h>
#include <env.h>
#include <fs.h>
#include <malloc.h>
#include <mapmem.h>
#include <string.h>

static const char * const script_names[] = { "boot.scr.uimg", "boot.scr", NULL };
static const char * const rauc_files[] = { "/usr/bin/rauc", "/etc/rauc/system.conf", NULL };

/**
 * struct distro_rauc_slot - Slot information
 *
 * @name	The slot name
 * @left	The number of boot tries left
 * @boot_part	The boot partition number on disk
 * @root_part	The boot partition number on disk
 */
struct distro_rauc_slot {
	char *name;
	ulong left;
	int boot_part;
	int root_part;
};

/**
 * struct distro_rauc_priv - Private data
 *
 * @slots	All slots of the device in default order
 * @boot_order	The current boot order string containing the active slot names
 */
struct distro_rauc_priv {
	struct distro_rauc_slot *slots;
	char *boot_order;
};

static int distro_rauc_check(struct udevice *dev, struct bootflow_iter *iter)
{
	/* This distro only works on whole MMC devices, as multiple partitions
	 * are needed for an A/B system. */
	if (bootflow_iter_check_mmc(iter))
		return log_msg_ret("mmc", -EOPNOTSUPP);
	if (iter->part != 0)
		return log_msg_ret("part", -EOPNOTSUPP);

	return 0;
}

static int distro_rauc_scan_boot_part(struct bootflow *bflow)
{
	struct blk_desc *desc;
	struct distro_rauc_priv *priv;
	int exists;
	int i;
	int j;
	int ret;

	desc = dev_get_uclass_plat(bflow->blk);
	if (!desc)
		return log_msg_ret("desc", -ENOMEM);

	priv = bflow->bootmeth_priv;
	if (!priv || !priv->slots || !priv->boot_order)
		return log_msg_ret("priv", -EINVAL);

	for (i = 0; priv->slots[i].name != NULL; i++) {
		exists = 0;
		for (j = 0; script_names[j] != NULL; j++) {
			ret = fs_set_blk_dev_with_part(desc, priv->slots[i].boot_part);
			if (ret)
				return log_msg_ret("blk", ret);
			exists |= fs_exists(script_names[j]);
		}
		if (!exists)
			return log_msg_ret("fs", -ENOENT);
	}

	return 0;
}

static int distro_rauc_scan_root_part(struct bootflow *bflow)
{
	struct blk_desc *desc;
	struct distro_rauc_priv *priv;
	int exists;
	int i;
	int j;
	int ret;

	desc = dev_get_uclass_plat(bflow->blk);
	if (!desc)
		return log_msg_ret("desc", -ENOMEM);

	priv = bflow->bootmeth_priv;
	if (!priv || !priv->slots || !priv->boot_order)
		return log_msg_ret("priv", -EINVAL);

	for (i = 0; priv->slots[i].name != NULL; i++) {
		exists = 0;
		for (j = 0; rauc_files[j] != NULL; j++) {
			ret = fs_set_blk_dev_with_part(desc,
					priv->slots[i].root_part);
			if (ret)
				return log_msg_ret("set", ret);
			if (!fs_exists(rauc_files[j]))
				return log_msg_ret("fs", -ENOENT);
		}
	}

	return 0;
}

static int distro_rauc_read_bootflow(struct udevice *dev, struct bootflow *bflow)
{
	struct distro_rauc_priv *priv;
	int ret;
	char *slot;
	int i;
	char *boot_order;
	char *partitions;
	const char **partitions_list;
	char *slots;
	const char **slots_list;
	char boot_left[32];
	char *parts;
	ulong default_tries;

	bflow->state = BOOTFLOWST_MEDIA;

	priv = malloc(sizeof(struct distro_rauc_priv));
	if (!priv)
		return log_msg_ret("buf", -ENOMEM);


	partitions = env_get("rauc_partitions");
	if (!partitions)
		return log_msg_ret("env", -ENOENT);
	partitions_list = str_to_list(partitions);

	slots = env_get("rauc_slots");
	if (!slots)
		return log_msg_ret("env", -ENOENT);
	slots_list = str_to_list(slots);

	boot_order = env_get("BOOT_ORDER");
	if (!boot_order) {
		if (env_set("BOOT_ORDER", slots))
			return log_msg_ret("env", -EPERM);
	}
	priv->boot_order = strdup(boot_order);

	default_tries = env_get_ulong("rauc_slot_default_tries", 10, 3);
	for (i = 0; slots_list[i] != NULL; i++) {
		sprintf(boot_left, "BOOT_%s_LEFT", slots_list[i]);
		if (!env_get(boot_left))
			env_set_ulong(boot_left, default_tries);
	}

	for (i = 0; slots_list[i] != NULL && partitions_list[i] != NULL; i++);
	priv->slots = malloc((i + 1) * sizeof(struct distro_rauc_slot));
	if (!priv->slots)
		return log_msg_ret("priv", -ENOMEM);
	for (i = 0; slots_list[i] != NULL && partitions_list[i] != NULL; i++) {
		priv->slots[i].name = strdup(slots_list[i]);
		sprintf(boot_left, "BOOT_%s_LEFT", slot);
		priv->slots[i].left = env_get_ulong(boot_left, 10, default_tries);
		parts = strdup(partitions_list[i]);
		priv->slots[i].boot_part = simple_strtoul(strsep(&parts, ","), NULL, 10);
		priv->slots[i].root_part = simple_strtoul(strsep(&parts, ","), NULL, 10);
		free(parts);
		parts = NULL;
	}
	priv->slots[i].name = NULL;
	priv->slots[i].left = 0;
	priv->slots[i].boot_part = 0;
	priv->slots[i].root_part = 0;

	bflow->bootmeth_priv = priv;
	bflow->state = BOOTFLOWST_FS;

	ret = distro_rauc_scan_boot_part(bflow);
	if (ret < 0)
		goto free_priv;
	ret = distro_rauc_scan_root_part(bflow);
	if (ret < 0)
		goto free_priv;

	bflow->state = BOOTFLOWST_READY;

	return 0;

free_priv:
	for (i = 0; priv->slots[i].name != NULL; i++)
		free(priv->slots[i].name);
	free(priv->slots);
	free(priv);
	bflow->bootmeth_priv = NULL;
	return ret;
}

static int distro_rauc_read_file(struct udevice *dev, struct bootflow *bflow,
				 const char *file_path, ulong addr,
				 enum bootflow_img_t type, ulong *sizep)
{
	/* Reading individual files is not supported since we only operate on
	 * whole MMC devices (because we require multiple partitions)
	 */
	return log_msg_ret("Unsupported", -ENOSYS);
}

static int distro_rauc_load_boot_script(struct bootflow *bflow, const char *slot)
{
	struct blk_desc *desc;
	struct distro_rauc_priv *priv;
	const char *prefix = "/";
	int ret;
	int i;

	if (!slot)
		return log_msg_ret("slot", -EINVAL);

	desc = dev_get_uclass_plat(bflow->blk);
	priv = bflow->bootmeth_priv;

	bflow->part = 0;
	for (i = 0; priv->slots[i].name != NULL; i++) {
		if (strcmp(priv->slots[i].name, slot) == 0) {
			bflow->part = priv->slots[i].boot_part;
			break;
		}
	}
	if (!bflow->part)
		return log_msg_ret("part", -ENOENT);

	ret = bootmeth_setup_fs(bflow, desc);
	if (ret)
		return log_msg_ret("set", ret);

	for (i = 0; script_names[i] != NULL; i++) {
		if (!bootmeth_try_file(bflow, desc, prefix, script_names[i]))
			break;
	}
	if (bflow->state != BOOTFLOWST_FILE)
		return log_msg_ret("file", -ENOENT);

	bflow->subdir = strdup(prefix);

	ret = bootmeth_alloc_file(bflow, 0x10000, ARCH_DMA_MINALIGN,
				  (enum bootflow_img_t)IH_TYPE_SCRIPT);
	if (ret)
		return log_msg_ret("read", ret);

	return 0;
}

static int decrement_slot_tries(const char *slot)
{
	ulong tries;
	char boot_left[32];

	sprintf(boot_left, "BOOT_%s_LEFT", slot);
	tries = env_get_ulong(boot_left, 10, ULONG_MAX);
	if (tries == ULONG_MAX)
		return log_msg_ret("env", -ENOENT);
	if (tries <= 0)
		return 0;

	return env_set_ulong(boot_left, tries - 1);
}

static int distro_rauc_boot(struct udevice *dev, struct bootflow *bflow)
{
	struct blk_desc *desc;
	struct distro_rauc_priv *priv;
	char *boot_order;
	const char **boot_order_list;
	const char *active_slot;
	char raucargs[32];
	ulong addr;
	int ret = 0;
	int i;

	desc = dev_get_uclass_plat(bflow->blk);
	if (desc->uclass_id != UCLASS_MMC)
		return log_msg_ret("blk", -EINVAL);
	priv = bflow->bootmeth_priv;

	/* RAUC variables */
	boot_order = env_get("BOOT_ORDER");
	if (!boot_order)
		return log_msg_ret("env", -ENOENT);
	boot_order_list = str_to_list(boot_order);

	/* Device info variables */
	ret = env_set("devtype", blk_get_devtype(bflow->blk));
	if (ret)
		return log_msg_ret("env", ret);

	ret = env_set_hex("devnum", desc->devnum);
	if (ret)
		return log_msg_ret("env", ret);

	/* Kernel command line arguments */
	active_slot = strdup(boot_order_list[0]);
	if (!active_slot)
		return log_msg_ret("env", -ENOENT);
	sprintf(raucargs, "rauc.slot=%s", active_slot);
	ret = env_set("raucargs", raucargs);
	if (ret)
		return log_msg_ret("env", ret);

	/* TODO: If active_slot is in boot_order, but has 0 tries left, remove
	 * it from boot_order. Then, set active_slot to other, if available.
	 */

	/* Partition info variables */
	for (i = 0; priv->slots[i].name != NULL; i++) {
		if (strcmp(priv->slots[i].name, active_slot) == 0) {
			ret = env_set_hex("mmcroot", priv->slots[i].root_part);
			if (ret)
				return log_msg_ret("env", ret);
			break;
		}
	}

	/* Load distro boot script */
	ret = distro_rauc_load_boot_script(bflow, active_slot);
	if (ret)
		return log_msg_ret("load", ret);
	ret = env_set_hex("distro_bootpart", bflow->part);
	if (ret)
		return log_msg_ret("env", ret);

	ret = decrement_slot_tries(active_slot);
	if (ret)
		return log_msg_ret("env", ret);
	ret = env_save();
	if (ret)
		return log_msg_ret("env", ret);

	log_debug("devtype: %s\n", env_get("devtype"));
	log_debug("devnum: %s\n", env_get("devnum"));
	log_debug("distro_bootpart: %s\n", env_get("distro_bootpart"));
	log_debug("mmcroot: %s\n", env_get("mmcroot"));
	log_debug("raucargs: %s\n", env_get("raucargs"));
	log_debug("rauc_slots: %s\n", env_get("rauc_slots"));
	log_debug("rauc_partitions: %s\n", env_get("rauc_partitions"));
	log_debug("rauc_slot_default_tries: %s\n", env_get("rauc_slot_default_tries"));
	log_debug("BOOT_ORDER: %s\n", env_get("BOOT_ORDER"));
	for (i = 0; priv->slots[i].name != NULL; i++) {
		log_debug("BOOT_%s_LEFT: %ld\n", priv->slots[i].name,
			  priv->slots[i].left);
	}

	/* Run distro boot script */
	addr = map_to_sysmem(bflow->buf);
	ret = cmd_source_script(addr, NULL, NULL);
	if (ret)
		return log_msg_ret("boot", ret);

	return 0;
}

static int distro_rauc_bootmeth_bind(struct udevice *dev)
{
	struct bootmeth_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->desc = "RAUC distro boot from MMC";
	plat->flags = BOOTMETHF_ANY_PART;

	return 0;
}

static struct bootmeth_ops distro_rauc_bootmeth_ops = {
	.check		= distro_rauc_check,
	.read_bootflow	= distro_rauc_read_bootflow,
	.read_file	= distro_rauc_read_file,
	.boot		= distro_rauc_boot,
};

static const struct udevice_id distro_rauc_bootmeth_ids[] = {
	{ .compatible = "u-boot,distro-rauc" },
	{ }
};

U_BOOT_DRIVER(bootmeth_rauc) = {
	.name		= "bootmeth_rauc",
	.id		= UCLASS_BOOTMETH,
	.of_match	= distro_rauc_bootmeth_ids,
	.ops		= &distro_rauc_bootmeth_ops,
	.bind		= distro_rauc_bootmeth_bind,
};
