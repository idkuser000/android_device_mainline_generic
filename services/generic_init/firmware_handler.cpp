/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "firmware_handler.h"

#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

#include "exthandler/exthandler.h"

using android::base::ReadFdToString;
using android::base::Socketpair;
using android::base::Split;
using android::base::Timer;
using android::base::Trim;
using android::base::unique_fd;
using android::base::WriteFully;

namespace android {
namespace init {

namespace {
bool PrefixMatch(const std::string& pattern, const std::string& path) {
    return android::base::StartsWith(path, pattern);
}

bool FnMatch(const std::string& pattern, const std::string& path) {
    return fnmatch(pattern.c_str(), path.c_str(), 0) == 0;
}

bool EqualMatch(const std::string& pattern, const std::string& path) {
    return pattern == path;
}
}  // namespace

static void LoadFirmware(const std::string& firmware, const std::string& root, int fw_fd,
                         size_t fw_size, int loading_fd, int data_fd) {
    // Start transfer.
    WriteFully(loading_fd, "1", 1);

    // Copy the firmware.
    int rc = sendfile(data_fd, fw_fd, nullptr, fw_size);
    if (rc == -1) {
        PLOG(ERROR) << "firmware: sendfile failed { '" << root << "', '" << firmware << "' }";
    }

    // Tell the firmware whether to abort or commit.
    const char* response = (rc != -1) ? "0" : "-1";
    WriteFully(loading_fd, response, strlen(response));
}

ExternalFirmwareHandler::ExternalFirmwareHandler(std::string devpath, uid_t uid, gid_t gid,
                                                 std::string handler_path)
    : devpath(std::move(devpath)), uid(uid), gid(gid), handler_path(std::move(handler_path)) {
    auto wildcard_position = this->devpath.find('*');
    if (wildcard_position != std::string::npos) {
        if (wildcard_position == this->devpath.length() - 1) {
            this->devpath.pop_back();
            match = std::bind(PrefixMatch, this->devpath, std::placeholders::_1);
        } else {
            match = std::bind(FnMatch, this->devpath, std::placeholders::_1);
        }
    } else {
        match = std::bind(EqualMatch, this->devpath, std::placeholders::_1);
    }
}

ExternalFirmwareHandler::ExternalFirmwareHandler(std::string devpath, uid_t uid,
                                                 std::string handler_path)
    : ExternalFirmwareHandler(devpath, uid, 0, handler_path) {}

FirmwareHandler::FirmwareHandler(std::vector<std::string> firmware_directories,
                                 std::vector<ExternalFirmwareHandler> external_firmware_handlers,
                                 bool serial_handler_after_coldboot)
    : firmware_directories_(std::move(firmware_directories)),
      external_firmware_handlers_(std::move(external_firmware_handlers)),
      serial_handler_after_coldboot_(serial_handler_after_coldboot) {}

void FirmwareHandler::ColdbootDone() {
    if (serial_handler_after_coldboot_) {
        enables_parallel_handlers_ = false;
    }
}

std::string FirmwareHandler::GetFirmwarePath(const Uevent& uevent) const {
    for (const auto& external_handler : external_firmware_handlers_) {
        if (external_handler.match(uevent.path)) {
            LOG(INFO) << "Launching external firmware handler '" << external_handler.handler_path
                      << "' for devpath: '" << uevent.path << "' firmware: '" << uevent.firmware
                      << "'";

            std::unordered_map<std::string, std::string> envs_map;
            envs_map["FIRMWARE"] = uevent.firmware;
            envs_map["DEVPATH"] = uevent.path;

            auto result = RunExternalHandler(external_handler.handler_path, external_handler.uid,
                                             external_handler.gid, envs_map);
            if (!result.ok()) {
                LOG(ERROR) << "Using default firmware; External firmware handler failed: "
                           << result.error();
                return uevent.firmware;
            }
            if (result->find("..") != std::string::npos) {
                LOG(ERROR) << "Using default firmware; External firmware handler provided an "
                              "invalid path, '"
                           << *result << "'";
                return uevent.firmware;
            }
            LOG(INFO) << "Loading firmware '" << *result << "' in place of '" << uevent.firmware
                      << "'";
            return *result;
        }
    }
    LOG(INFO) << "firmware: loading '" << uevent.firmware << "' for '" << uevent.path << "'";
    return uevent.firmware;
}

void FirmwareHandler::ProcessFirmwareEvent(const std::string& path, const std::string& firmware,
                                           bool in_thread_pool, Timer t) const {
    std::string root = "/sys" + path;
    std::string loading = root + "/loading";
    std::string data = root + "/data";

    unique_fd loading_fd(open(loading.c_str(), O_WRONLY | O_CLOEXEC));
    if (loading_fd == -1) {
        PLOG(ERROR) << "couldn't open firmware loading fd for " << firmware;
        return;
    }

    unique_fd data_fd(open(data.c_str(), O_WRONLY | O_CLOEXEC));
    if (data_fd == -1) {
        PLOG(ERROR) << "couldn't open firmware data fd for " << firmware;
        return;
    }

    std::vector<std::string> attempted_paths_and_errors;
    auto TryLoadFirmware = [&](const std::string& firmware_directory) {
        std::string file = firmware_directory + firmware;
        unique_fd fw_fd(open(file.c_str(), O_RDONLY | O_CLOEXEC));
        if (fw_fd == -1) {
            attempted_paths_and_errors.emplace_back("firmware: attempted " + file +
                                                    ", open failed: " + strerror(errno));
            return false;
        }
        struct stat sb;
        if (fstat(fw_fd.get(), &sb) == -1) {
            attempted_paths_and_errors.emplace_back("firmware: attempted " + file +
                                                    ", fstat failed: " + strerror(errno));
            return false;
        }
        LOG(INFO) << "found " << file << " for " << path;
        LoadFirmware(firmware, root, fw_fd.get(), sb.st_size, loading_fd.get(), data_fd.get());
        return true;
    };

    if (ForEachFirmwareDirectory(TryLoadFirmware)) {
        LOG(INFO) << "loading " << path << " took " << t;
        return;
    }

    LOG(ERROR) << "firmware: could not find firmware for " << firmware;
    for (const auto& message : attempted_paths_and_errors) {
        LOG(ERROR) << message;
    }

    // Write "-1" as our response to the kernel's firmware request, since we have nothing for it.
    write(loading_fd.get(), "-1", 2);
}

bool FirmwareHandler::ForEachFirmwareDirectory(
        std::function<bool(const std::string&)> handler) const {
    for (const std::string& firmware_directory : firmware_directories_) {
        if (std::invoke(handler, firmware_directory)) {
            return true;
        }
    }

    glob_t glob_result;
    glob("/apex/*/etc/firmware/", GLOB_MARK, nullptr, &glob_result);
    auto free_glob = android::base::make_scope_guard(std::bind(&globfree, &glob_result));
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        char* apex_firmware_directory = glob_result.gl_pathv[i];
        // Filter-out /apex/<name>@<ver> paths. The paths are bind-mounted to
        // /apex/<name> paths, so unless we filter them out, we will look into the
        // same apex twice.
        if (strchr(apex_firmware_directory, '@')) {
            continue;
        }
        if (std::invoke(handler, apex_firmware_directory)) {
            return true;
        }
    }

    return false;
}

void FirmwareHandler::HandleUeventInternal(const Uevent& uevent, bool in_thread_pool) const {
    Timer t;
    auto firmware = GetFirmwarePath(uevent);
    ProcessFirmwareEvent(uevent.path, firmware, in_thread_pool, t);
}

void FirmwareHandler::HandleUevent(const Uevent& uevent) {
    if (uevent.subsystem != "firmware" || uevent.action != "add") return;

    if (enables_parallel_handlers_) {
        // Loading the firmware in a child means we can do that in parallel...
        auto pid = fork();
        if (pid == -1) {
            PLOG(ERROR) << "could not fork to process firmware event for " << uevent.firmware;
        } else if (pid == 0) {
            // Child does the actual work
            HandleUeventInternal(uevent, /*in_thread_pool=*/false);
            _exit(EXIT_SUCCESS);
        } else {
            // The main process returns here. Let the child do the actual work in parallel.
            return;
        }
    }
    HandleUeventInternal(uevent, /*in_thread_pool=*/false);
}

void FirmwareHandler::EnqueueUevent(const Uevent& uevent, ThreadPool& thread_pool) {
    if (uevent.subsystem != "firmware" || uevent.action != "add") return;

    thread_pool.Enqueue(kPriorityFirmware,
                        [&uevent, this] { HandleUeventInternal(uevent, /*in_thread_pool=*/true); });
}

}  // namespace init
}  // namespace android
