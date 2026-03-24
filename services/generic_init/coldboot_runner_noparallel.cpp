/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "coldboot_runner.h"

namespace android {
namespace init {

ColdbootRunnerNoParallel::ColdbootRunnerNoParallel(
        const std::vector<Uevent>& uevent_queue,
        const std::vector<std::shared_ptr<UeventHandler>>& uevent_handlers)
    : uevent_queue_(uevent_queue),
      uevent_handlers_(uevent_handlers) {}

void ColdbootRunnerNoParallel::UeventHandlerMain(void) {
    for (const auto& uevent : uevent_queue_) {
        for (const auto& uevent_handler : uevent_handlers_) {
            uevent_handler->HandleUevent(uevent);
        }
    }
}

void ColdbootRunnerNoParallel::StartInBackground() {}

void ColdbootRunnerNoParallel::Wait() {
    UeventHandlerMain();
}

}  // namespace init
}  // namespace android
