/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>
#include <vector>

namespace MountHandler {

void OnBlockDeviceAdd(const std::string& devpath, const std::vector<std::string>& links);

}  // namespace
