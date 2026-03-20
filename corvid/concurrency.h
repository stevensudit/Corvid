// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
// Umbrella header for the Corvid concurrency module.
//
// Includes:
//  sync_lock       - `synchronizer`, `lock`, and `reverse_lock` attestation idiom
//  notifiable      - value guarded by mutex and condition variable
//  tombstone       - thread-safe atomic value with a final "dead" state
//  timers          - thread-safe priority-queue timer scheduler
//  relaxed_atomic  - `std::atomic<T>` wrapper with relaxed load/store operators
#include "concurrency/sync_lock.h"
#include "concurrency/notifiable.h"
#include "concurrency/tombstone.h"
#include "concurrency/timers.h"
#include "concurrency/relaxed_atomic.h"
