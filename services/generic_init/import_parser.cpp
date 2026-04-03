/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "import_parser.h"

#include <android-base/logging.h>
#include <android-base/strings.h>

#include <cstring>

extern bool fs_mgr_get_boot_config(const std::string& key, std::string* out_val);

namespace android {
namespace init {

namespace {

using android::base::StartsWith;

bool TranslatePropName(std::string* prop_name) {
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

// From util.cpp
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
            LOG(ERROR) << "using deprecated syntax for specifying property '" << c
                        << "', use ${name} instead";
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

}  // namespace

Result<void> ImportParser::ParseSection(std::vector<std::string>&& args,
                                        const std::string& filename, int line) {
    if (args.size() != 2) {
        return Error() << "single argument needed for import\n";
    }

    auto conf_file = ExpandProps(args[1]);
    if (!conf_file.ok()) {
        return Error() << "Could not expand import: " << conf_file.error();
    }

    LOG(INFO) << "Added '" << *conf_file << "' to import list";
    if (filename_.empty()) filename_ = filename;
    imports_.emplace_back(std::move(*conf_file), line);
    return {};
}

Result<void> ImportParser::ParseLineSection(std::vector<std::string>&&, int) {
    return Error() << "Unexpected line found after import statement";
}

void ImportParser::EndFile() {
    auto current_imports = std::move(imports_);
    imports_.clear();
    for (const auto& [import, line_num] : current_imports) {
        parser_->ParseConfig(import);
    }
}

}  // namespace init
}  // namespace android
