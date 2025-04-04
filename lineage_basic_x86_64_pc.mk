#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

# Inherit from device
$(call inherit-product, device/pc/basic_x86_64_pc/device.mk)

PRODUCT_NAME := lineage_basic_x86_64_pc
PRODUCT_DEVICE := basic_x86_64_pc
PRODUCT_BRAND := PC
PRODUCT_MANUFACTURER := PC
PRODUCT_MODEL := Basic x86_64 PC
