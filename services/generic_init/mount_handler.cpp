/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mount_handler.h"

namespace MountHandler {

void OnPreBlockDevices(void) {
}

void OnBlockDeviceAdd(const android::init::Uevent& uevent, const std::string& devpath, const std::vector<std::string>& links) {
}

void OnPostBlockDevices(void) {
}

}  // namespace
