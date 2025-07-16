#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/aosp_basic_x86_64_pc.mk \
    $(LOCAL_DIR)/lineage_basic_x86_64_pc.mk

$(foreach build_type, user userdebug eng, \
    $(eval COMMON_LUNCH_CHOICES += aosp_basic_x86_64_pc-$(build_type)) \
    $(eval COMMON_LUNCH_CHOICES += lineage_basic_x86_64_pc-$(build_type)))
