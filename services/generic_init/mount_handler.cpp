/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mount_handler.h"
#include "mount_helpers.h"

#include <android-base/logging.h>
#include <fstab/fstab.h>
#include <sys/mount.h>

#include <unordered_map>

namespace MountHandler {

using namespace ::android::fs_mgr;

std::unordered_map<std::string, bool> wanted_block_devices = {
    // devpath, state
    {"/dev/block/sda", false},
    {"/dev/block/sdb", false},
};

Fstab fstab_data = {
    {
        .blk_device = "cache",
        .mount_point = "/cache",
        .fs_type = "tmpfs",
        .fs_mgr_flags = {
            .first_stage_mount = true,
            .no_fail = true,
        }
    },
    {
        .blk_device = "data",
        .mount_point = "/data",
        .fs_type = "tmpfs",
        .fs_mgr_flags = {
            .first_stage_mount = true,
        }
    },
    {
        .blk_device = "metadata",
        .mount_point = "/metadata",
        .fs_type = "tmpfs",
        .fs_mgr_flags = {
            .first_stage_mount = true,
            .no_fail = true,
        }
    }
};

Fstab fstab_system = {
    {
        .blk_device = "/dev/block/sda",
        .mount_point = "/system",
        .fs_type = "ext4",
        .flags = MS_RDONLY,
        .fs_mgr_flags = {
            .first_stage_mount = true,
        }
    },
    {
        .blk_device = "/dev/block/sdb",
        .mount_point = "/vendor",
        .fs_type = "ext4",
        .flags = MS_RDONLY,
        .fs_mgr_flags = {
            .first_stage_mount = true,
        }
    }
};

bool IsAllWantedBlockDevicesPresent(void) {
    bool ret = true;
    for (const auto& [blk, state] : wanted_block_devices) {
        if (!state) {
            LOG(INFO) << "Wanted block device " << blk << " is not present yet";
            ret = false;
        }
    }
    return ret;
}

void OnPreBlockDevices(void) {
}

void OnBlockDeviceAdd(const android::init::Uevent& uevent, const std::string& devpath, const std::vector<std::string>& links) {
    if (wanted_block_devices.find(devpath) != wanted_block_devices.end()) {
        LOG(INFO) << "Got wanted block device " << devpath;
        wanted_block_devices[devpath] = true;
    }
}

bool CanQuitUeventd(void) {
    return IsAllWantedBlockDevicesPresent();
}

void OnPostBlockDevices(void) {
    MountHelpers::MountPartitions(fstab_system);
    MountHelpers::MountPartitions(fstab_data);
}

}  // namespace
