#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

USES_DEVICE_PC_BASIC_X86_64_PC := true

# Inherit from mainline/common
include device/mainline/common/BoardConfigMainlineCommon.mk

# A/B
AB_OTA_UPDATER := false

# Architecture
TARGET_CPU_ABI := x86_64
TARGET_ARCH := x86_64
TARGET_ARCH_VARIANT := sandybridge

# Boot parameters
BOARD_KERNEL_CMDLINE := \
    $(MAINLINE_COMMON_ANDROIDBOOT_PARAMS) \
    $(MAINLINE_COMMON_KERNEL_PARAMS) \
    androidboot.boot_devices=any \
    androidboot.hardware=basic_x86_64_pc \
    androidboot.partition_map=sda1,system;sda2,vendor;sda3,userdata \
    androidboot.selinux=permissive \
    androidboot.verifiedbootstate=orange \
    audit=0 \
    console=tty0 \
    mitigations=off

# Filesystem
TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USERIMAGES_USE_EXT4 := true

# Kernel
BOARD_KERNEL_IMAGE_NAME := bzImage
ifeq ($(BASIC_X86_64_PC_USE_PRISTINE_KERNEL),true)
TARGET_KERNEL_CONFIG := x86_64_defconfig
TARGET_KERNEL_CONFIG_EXT := \
    kernel/mainline/configs/fragments/android-base-pre/common.config \
    kernel/mainline/configs/fragments/android-base-pre/x86_64.config \
    kernel/configs/b/android-6.12/android-base.config \
    kernel/mainline/configs/fragments/android-base-conditional/CONFIG_X86-y.config \
    kernel/mainline/configs/fragments/android-base-conditional/CONFIG_X86_64-y.config \
    kernel/mainline/configs/fragments/common.config \
    $(DEVICE_PATH)/configs/basic_x86_64_pc.config \
    kernel/mainline/configs/fragments/y/fbcon.config
TARGET_KERNEL_SOURCE ?= kernel/mainline/linux
else
TARGET_KERNEL_CONFIG := gki_defconfig
TARGET_KERNEL_CONFIG_EXT := \
    $(DEVICE_PATH)/configs/basic_x86_64_pc.config \
    kernel/mainline/configs/fragments/y/fbcon.config
TARGET_KERNEL_SOURCE := kernel/virt/virtio
endif

# OTA
TARGET_SKIP_OTA_PACKAGE := true

# Partitions
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_SYSTEMIMAGE_EXTFS_INODE_COUNT := -1
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEMIMAGE_PARTITION_RESERVED_SIZE := 67108864
BOARD_VENDORIMAGE_EXTFS_INODE_COUNT := -1
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_PARTITION_RESERVED_SIZE := 67108864
TARGET_COPY_OUT_VENDOR := vendor

# Platform
TARGET_BOARD_PLATFORM := basic_x86_64_pc

# Ramdisk
BOARD_RAMDISK_USE_LZ4 := true

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/fstab.basic_x86_64_pc

# VINTF
DEVICE_MANIFEST_FILE := \
    $(DEVICE_PATH)/configs/manifest.xml
