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
//  jthread_stoppable_sleep - interruptible deadline sleep for `std::jthread`
//  notifiable              - value guarded by mutex and condition variable
//  owner_thread_dispatcher - dispatches callbacks to execute only in the
//                            owning thread
//  relaxed_atomic          - `std::atomic<T>` wrapper with relaxed
//                            load/store operators
//  sync_lock               - `synchronizer`, `lock`, and `reverse_lock`
//                            attestation idiom
//  timeout_sweeper         - heap of (`expiration`, `callback`) pairs swept
//                            by an external driver
//  timer_fuse              - copyable liveness token for per-operation
//                            timeouts on a `shared_ptr`-managed resource
//  timers                  - thread-safe priority-queue timer scheduler
//  timing_wheel            - single-level O(1) timing wheel
//  tombstone               - thread-safe atomic value with a final "dead"
//                            state
#include "concurrency/jthread_stoppable_sleep.h"
#include "concurrency/notifiable.h"
#include "concurrency/owner_thread_dispatcher.h"
#include "concurrency/relaxed_atomic.h"
#include "concurrency/sync_lock.h"
#include "concurrency/timeout_sweeper.h"
#include "concurrency/timer_fuse.h"
#include "concurrency/timers.h"
#include "concurrency/timing_wheel.h"
#include "concurrency/tombstone.h"

// TODO: Kill timing_wheel and replace it with the same interface as
// timeout_sweeper, but with a hierarchical multi-level design to achieve O(1)
// schedule and tick.
