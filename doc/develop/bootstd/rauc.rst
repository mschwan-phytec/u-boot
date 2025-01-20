.. SPDX-License-Identifier: GPL-2.0+:

RAUC Bootmeth
=============

This bootmeth provides a way to locate and run a A/B system with RAUC as its
update client. The booted distro must supply a script on an MMC device
containing the final boot instructions necessary.

The boot script must be located in the partition provided by the slot
configuration in the environment variable ``rauc_slots``. The bootmeth searches
for "boot.scr.uimg" first, then "boot.scr" if not found.

In addition, the RAUC binary and system configuration file must be present in
the specified root filesystem.

The settings for configuring the layout and partitions are done through the
following variables:

rauc_slots
    If ``BOOT_ORDER`` is not set, this variable is used to set the default boot
    order. It also specifies the order in which partition indexes are specified
    in ``rauc_partitions``.

rauc_partitions
    This variable configures the boot and root partitions for each slot. The
    content must be a list of pairs, with the following syntax: ``1,2 3,4``,
    where 1 and 3 are the slots' boot partition and 2 and 4 the slots' root
    partition.

rauc_slot_default_tries
    If slots have no variable ``BOOT_*_LEFT`` set, they are set with the value
    of this variable.

When the bootflow is booted, the bootmeth sets these environment variables:

devtype
    device type (e.g. "mmc")

devnum
    device number, corresponding to the device 'sequence' number
    ``dev_seq(dev)``

distro_bootpart
    partition number on the device (numbered from 1)

raucargs
    kernel command line arguments needed for RAUC to detect the currently booted
    slot

The script file must be a FIT or a legacy uImage. It is loaded into memory and
executed.

The compatible string "u-boot,distro-rauc" is used for the driver. It is present
if ``CONFIG_BOOTMETH_RAUC`` is enabled.
