/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "uevent.h"

#include <string>
#include <vector>

namespace MountHandler {

void OnBlockDeviceAdd(const android::init::Uevent& uevent, const std::string& devpath, const std::vector<std::string>& links);

}  // namespace
