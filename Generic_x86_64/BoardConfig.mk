# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from parent
include device/mainline/generic/BoardConfig.mk

# Architecture
TARGET_CPU_ABI := x86_64
TARGET_ARCH := x86_64
TARGET_ARCH_VARIANT := sandybridge

# Kernel
BOARD_KERNEL_IMAGE_NAME := bzImage
ifeq ($(MAINLINE_GENERIC_USE_PRISTINE_KERNEL),true)
TARGET_KERNEL_CONFIG := x86_64_defconfig
TARGET_KERNEL_CONFIG_EXT := \
    kernel/mainline/configs/fragments/android-base-pre/common.config \
    kernel/mainline/configs/fragments/android-base-pre/x86_64.config \
    kernel/configs/b/android-6.12/android-base.config \
    kernel/mainline/configs/fragments/android-base-conditional/CONFIG_X86-y.config \
    kernel/mainline/configs/fragments/android-base-conditional/CONFIG_X86_64-y.config \
    kernel/mainline/configs/fragments/common.config \
    $(TARGET_DEVICE_PATH)/configs/kernel/basic_x86_64_pc.config \
    kernel/mainline/configs/fragments/y/fbcon.config
else
TARGET_KERNEL_CONFIG := gki_defconfig
TARGET_KERNEL_CONFIG_EXT := \
    $(TARGET_DEVICE_PATH)/configs/kernel/basic_x86_64_pc.config \
    kernel/mainline/configs/fragments/y/fbcon.config
endif
