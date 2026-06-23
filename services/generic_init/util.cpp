/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <format>
#include <map>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <cutils/sockets.h>

#if defined(__ANDROID__)
#include <fs_mgr.h>
#endif

#include "reboot_utils.h"

extern bool fs_mgr_get_boot_config(const std::string& key, std::string* out_val);

using android::base::boot_clock;
using android::base::StartsWith;
using namespace std::literals::string_literals;

namespace android {
namespace init {

void (*trigger_shutdown)(const std::string& command) = nullptr;

static int SelinuxGetVendorAndroidVersion() {
    return __ANDROID_API_P__;
}

// DecodeUid() - decodes and returns the given string, which can be either the
// numeric or name representation, into the integer uid or gid.
Result<uid_t> DecodeUid(const std::string& name) {
    if (isalpha(name[0])) {
        passwd* pwd = getpwnam(name.c_str());
        if (!pwd) return ErrnoError() << "getpwnam failed";

        return pwd->pw_uid;
    }

    errno = 0;
    uid_t result = static_cast<uid_t>(strtoul(name.c_str(), 0, 0));
    if (errno) return ErrnoError() << "strtoul failed";

    return result;
}

Result<std::string> ReadFile(const std::string& path) {
    android::base::unique_fd fd(
        TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
    if (fd == -1) {
        return ErrnoError() << "open() failed";
    }

    // For security reasons, disallow world-writable
    // or group-writable files.
    struct stat sb;
    if (fstat(fd.get(), &sb) == -1) {
        return ErrnoError() << "fstat failed()";
    }
    if ((sb.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return Error() << "Skipping insecure file";
    }

    std::string content;
    if (!android::base::ReadFdToString(fd, &content)) {
        return ErrnoError() << "Unable to read file contents";
    }
    return content;
}

static int OpenFile(const std::string& path, int flags, mode_t mode) {
    return open(path.c_str(), flags, mode);
}

Result<void> WriteFile(const std::string& path, const std::string& content) {
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(
        OpenFile(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_TRUNC | O_CLOEXEC, 0600)));
    if (fd == -1) {
        return ErrnoError() << "open() failed";
    }
    if (!android::base::WriteStringToFd(content, fd)) {
        return ErrnoError() << "Unable to write file contents";
    }
    return {};
}

bool mkdir_recursive(const std::string& path, mode_t mode) {
    std::string::size_type slash = 0;
    while ((slash = path.find('/', slash + 1)) != std::string::npos) {
        auto directory = path.substr(0, slash);
        struct stat info;
        if (stat(directory.c_str(), &info) != 0) {
            auto ret = make_dir(directory, mode);
            if (!ret && errno != EEXIST) return false;
        }
    }
    auto ret = make_dir(path, mode);
    if (!ret && errno != EEXIST) return false;
    return true;
}

int wait_for_file(const char* filename, std::chrono::nanoseconds timeout) {
    android::base::Timer t;
    while (t.duration() < timeout) {
        struct stat sb;
        if (stat(filename, &sb) != -1) {
            LOG(INFO) << "wait for '" << filename << "' took " << t;
            return 0;
        }
        std::this_thread::sleep_for(10ms);
    }
    LOG(WARNING) << "wait for '" << filename << "' timed out and took " << t;
    return -1;
}

bool make_dir(const std::string& path, mode_t mode) {
    return mkdir(path.c_str(), mode) == 0;
}

/*
 * Returns true is pathname is a directory
 */
bool is_dir(const char* pathname) {
    struct stat info;
    if (stat(pathname, &info) == -1) {
        return false;
    }
    return S_ISDIR(info.st_mode);
}

static bool TranslatePropName(std::string* prop_name) {
    if (*prop_name == "ro.board.platform") {
        *prop_name = "board_platform";
    } else if (*prop_name == "ro.hardware") {
        *prop_name = "hardware";
    } else if (StartsWith(*prop_name, "ro.boot.")) {
        *prop_name = prop_name->substr(strlen("ro.boot."));
    } else {
        return false;
    }
    return true;
}

Result<std::string> ExpandProps(const std::string& src) {
    const char* src_ptr = src.c_str();

    std::string dst;

    /* - variables can either be $x.y or ${x.y}, in case they are only part
     *   of the string.
     * - will accept $$ as a literal $.
     * - no nested property expansion, i.e. ${foo.${bar}} is not supported,
     *   bad things will happen
     * - ${x.y:-default} will return default value if property empty.
     */
    while (*src_ptr) {
        const char* c;

        c = strchr(src_ptr, '$');
        if (!c) {
            dst.append(src_ptr);
            return dst;
        }

        dst.append(src_ptr, c);
        c++;

        if (*c == '$') {
            dst.push_back(*(c++));
            src_ptr = c;
            continue;
        } else if (*c == '\0') {
            return dst;
        }

        std::string prop_name;
        std::string def_val;
        if (*c == '{') {
            c++;
            const char* end = strchr(c, '}');
            if (!end) {
                // failed to find closing brace, abort.
                return Error() << "unexpected end of string in '" << src << "', looking for }";
            }
            prop_name = std::string(c, end);
            c = end + 1;
            size_t def = prop_name.find(":-");
            if (def < prop_name.size()) {
                def_val = prop_name.substr(def + 2);
                prop_name = prop_name.substr(0, def);
            }
        } else {
            prop_name = c;
            if (SelinuxGetVendorAndroidVersion() >= __ANDROID_API_R__) {
                return Error() << "using deprecated syntax for specifying property '" << c
                               << "', use ${name} instead";
            } else {
                LOG(ERROR) << "using deprecated syntax for specifying property '" << c
                           << "', use ${name} instead";
            }
            c += prop_name.size();
        }

        if (prop_name.empty()) {
            return Error() << "invalid zero-length property name in '" << src << "'";
        }

        std::string prop_val = "";
        if (TranslatePropName(&prop_name)) {
            fs_mgr_get_boot_config(prop_name, &prop_val);
        }
        if (prop_val.empty()) {
            if (def_val.empty()) {
                return Error() << "property '" << prop_name << "' doesn't exist while expanding '"
                               << src << "'";
            }
            prop_val = def_val;
        }

        dst.append(prop_val);
        src_ptr = c;
    }

    return dst;
}

// Reads the content of device tree file under the platform's Android DT directory.
// Returns true if the read is success, false otherwise.
bool read_android_dt_file(const std::string& sub_path, std::string* dt_content) {
#if defined(__ANDROID__)
    const std::string file_name = android::fs_mgr::GetAndroidDtDir() + sub_path;
    if (android::base::ReadFileToString(file_name, dt_content)) {
        if (!dt_content->empty()) {
            dt_content->pop_back();  // Trims the trailing '\0' out.
            return true;
        }
    }
#endif
    return false;
}

bool is_android_dt_value_expected(const std::string& sub_path, const std::string& expected_content) {
    std::string dt_content;
    if (read_android_dt_file(sub_path, &dt_content)) {
        if (dt_content == expected_content) {
            return true;
        }
    }
    return false;
}

bool IsLegalPropertyName(const std::string& name) {
    size_t namelen = name.size();

    if (namelen < 1) return false;
    if (name[0] == '.') return false;
    if (name[namelen - 1] == '.') return false;

    /* Only allow alphanumeric, plus '.', '-', '@', ':', or '_' */
    /* Don't allow ".." to appear in a property name */
    for (size_t i = 0; i < namelen; i++) {
        if (name[i] == '.') {
            // i=0 is guaranteed to never have a dot. See above.
            if (name[i - 1] == '.') return false;
            continue;
        }
        if (name[i] == '_' || name[i] == '-' || name[i] == '@' || name[i] == ':') continue;
        if (name[i] >= 'a' && name[i] <= 'z') continue;
        if (name[i] >= 'A' && name[i] <= 'Z') continue;
        if (name[i] >= '0' && name[i] <= '9') continue;
        return false;
    }

    return true;
}

// Remove unnecessary slashes so that any later checks (e.g., the check for
// whether the path is a top-level directory in /data) don't get confused.
std::string CleanDirPath(const std::string& path) {
    std::string result;
    result.reserve(path.length());
    // Collapse duplicate slashes, e.g. //data//foo// => /data/foo/
    for (char c : path) {
        if (c != '/' || result.empty() || result.back() != '/') {
            result += c;
        }
    }
    // Remove trailing slash, e.g. /data/foo/ => /data/foo
    if (result.length() > 1 && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

static void InitAborter(const char* abort_message) {
    InitFatalReboot(SIGABRT);
}

// The kernel opens /dev/console and uses that fd for stdin/stdout/stderr if there is a serial
// console enabled and no initramfs, otherwise it does not provide any fds for stdin/stdout/stderr.
// SetStdioToDevNull() is used to close these existing fds if they exist and replace them with
// /dev/null regardless.
//
// In the case that these fds are provided by the kernel, the exec of second stage init causes an
// SELinux denial as it does not have access to /dev/console.  In the case that they are not
// provided, exec of any further process is potentially dangerous as the first fd's opened by that
// process will take the stdin/stdout/stderr fileno's, which can cause issues if printf(), etc is
// then used by that process.
//
// Lastly, simply calling SetStdioToDevNull() in first stage init is not enough, since first
// stage init still runs in kernel context, future child processes will not have permissions to
// access any fds that it opens, including the one opened below for /dev/null.  Therefore,
// SetStdioToDevNull() must be called again in second stage init.
void SetStdioToDevNull(char** argv) {
    // Make stdin/stdout/stderr all point to /dev/null.
    int fd = open("/dev/null", O_RDWR);  // NOLINT(android-cloexec-open)
    if (fd == -1) {
        int saved_errno = errno;
        android::base::InitLogging(argv, &android::base::KernelLogger, InitAborter);
        errno = saved_errno;
        PLOG(FATAL) << "Couldn't open /dev/null";
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
}

void InitKernelLogging(char** argv) {
    SetFatalRebootTarget();
    android::base::InitLogging(argv, &android::base::KernelLogger, InitAborter);
}

bool IsRecoveryMode() {
    return access("/system/bin/recovery", F_OK) == 0;
}

// Check if default mount namespace is ready to be used with APEX modules
static bool is_default_mount_namespace_ready = false;

bool IsDefaultMountNamespaceReady() {
    return is_default_mount_namespace_ready;
}

void SetDefaultMountNamespaceReady() {
    is_default_mount_namespace_ready = true;
}

bool Has32BitAbi() {
    static bool has = !android::base::GetProperty("ro.product.cpu.abilist32", "").empty();
    return has;
}

std::string GetApexNameFromFileName(const std::string& path) {
    [[clang::no_destroy]] static const std::string kApexDir = "/apex/";
    if (StartsWith(path, kApexDir)) {
        auto begin = kApexDir.size();
        auto end = path.find('/', begin);
        return path.substr(begin, end - begin);
    }
    return "";
}

std::vector<std::string> FilterVersionedConfigs(const std::vector<std::string>& configs,
                                                int active_sdk) {
    std::vector<std::string> filtered_configs;

    std::map<std::string, std::pair<std::string, int>> script_map;
    for (const auto& c : configs) {
        int sdk = 0;
        const std::vector<std::string> parts = android::base::Split(c, ".");
        std::string base;
        if (parts.size() < 2) {
            continue;
        }

        // parts[size()-1], aka the suffix, should be "rc" or "#rc"
        // any other pattern gets discarded

        const auto& suffix = parts[parts.size() - 1];
        if (suffix == "rc") {
            sdk = 0;
        } else {
            char trailer[9] = {0};
            int r = sscanf(suffix.c_str(), "%d%8s", &sdk, trailer);
            if (r != 2) {
                continue;
            }
            if (strlen(trailer) > 2 || strcmp(trailer, "rc") != 0) {
                continue;
            }
        }

        if (sdk < 0 || sdk > active_sdk) {
            continue;
        }

        base = parts[0];
        for (unsigned int i = 1; i < parts.size() - 1; i++) {
            base = base + "." + parts[i];
        }

        // is this preferred over what we already have
        auto it = script_map.find(base);
        if (it == script_map.end() || it->second.second < sdk) {
            script_map[base] = std::make_pair(c, sdk);
        }
    }

    for (const auto& m : script_map) {
        filtered_configs.push_back(m.second.first);
    }
    return filtered_configs;
}

// Forks, executes the provided program in the child, and waits for the completion in the parent.
// Child's stderr is captured and logged using LOG(ERROR).
bool ForkExecveAndWaitForCompletion(const char* filename, char* const argv[]) {
    // Create a pipe used for redirecting child process's output.
    // * pipe_fds[0] is the FD the parent will use for reading.
    // * pipe_fds[1] is the FD the child will use for writing.
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        PLOG(ERROR) << "Failed to create pipe";
        return false;
    }

    pid_t child_pid = fork();
    if (child_pid == -1) {
        PLOG(ERROR) << "Failed to fork for " << filename;
        return false;
    }

    if (child_pid == 0) {
        // fork succeeded -- this is executing in the child process

        // Close the pipe FD not used by this process
        close(pipe_fds[0]);

        // Redirect stderr to the pipe FD provided by the parent
        if (TEMP_FAILURE_RETRY(dup2(pipe_fds[1], STDERR_FILENO)) == -1) {
            PLOG(ERROR) << "Failed to redirect stderr of " << filename;
            _exit(127);
            return false;
        }
        close(pipe_fds[1]);

        if (execv(filename, argv) == -1) {
            PLOG(ERROR) << "Failed to execve " << filename;
            return false;
        }
        // Unreachable because execve will have succeeded and replaced this code
        // with child process's code.
        _exit(127);
        return false;
    } else {
        // fork succeeded -- this is executing in the original/parent process

        // Close the pipe FD not used by this process
        close(pipe_fds[1]);

        // Log the redirected output of the child process.
        // It's unfortunate that there's no standard way to obtain an istream for a file descriptor.
        // As a result, we're buffering all output and logging it in one go at the end of the
        // invocation, instead of logging it as it comes in.
        const int child_out_fd = pipe_fds[0];
        std::string child_output;
        if (!android::base::ReadFdToString(child_out_fd, &child_output)) {
            PLOG(ERROR) << "Failed to capture full output of " << filename;
        }
        close(child_out_fd);
        if (!child_output.empty()) {
            // Log captured output, line by line, because LOG expects to be invoked for each line
            std::istringstream in(child_output);
            std::string line;
            while (std::getline(in, line)) {
                LOG(ERROR) << filename << ": " << line;
            }
        }

        // Wait for child to terminate
        int status;
        if (TEMP_FAILURE_RETRY(waitpid(child_pid, &status, 0)) != child_pid) {
            PLOG(ERROR) << "Failed to wait for " << filename;
            return false;
        }

        if (WIFEXITED(status)) {
            int status_code = WEXITSTATUS(status);
            if (status_code == 0) {
                return true;
            } else {
                LOG(ERROR) << filename << " exited with status " << status_code;
            }
        } else if (WIFSIGNALED(status)) {
            LOG(ERROR) << filename << " killed by signal " << WTERMSIG(status);
        } else if (WIFSTOPPED(status)) {
            LOG(ERROR) << filename << " stopped by signal " << WSTOPSIG(status);
        } else {
            LOG(ERROR) << "waitpid for " << filename << " returned unexpected status: " << status;
        }

        return false;
    }
}

std::string SignalName(int signum) {
#if defined(__BIONIC__)
    char str[SIG2STR_MAX];
    if (sig2str(signum, str) == 0) {
        return "SIG" + std::string(str);
    }
    return std::format("invalid signal ({})", signum);
#else
    // sig2str is missing in many platforms including glibc. glibc has sigabbrev_np instead, but it
    // doesn't seem available in our repo.
    return std::format("signal {}", signum);
#endif
}

}  // namespace init
}  // namespace android
