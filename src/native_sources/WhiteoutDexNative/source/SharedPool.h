// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

/**
 * @file SharedPool.h
 * @brief Single global thread pool shared across all WhiteoutDexNative modules.
 *
 * IMPROVEMENT: Replaces three separate pools (g_mpqPool, g_cascPool, g_pool)
 * that each created hardware_concurrency() threads.  On an 8-core machine
 * the old code spawned 24 threads total; now it's just 8.
 *
 * Usage:
 *   #include "SharedPool.h"
 *   auto* pool = whiteoutdex::getSharedPool();
 *   pool->submit(task);
 *   pool->waitIdle();
 */

#include <whiteout/utils/simple_thread_pool.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

namespace whiteoutdex {

/// Returns the single global thread pool.  Created on first call,
/// lives until program exit.  Thread-safe.
inline whiteout::utils::SimpleThreadPool* getSharedPool() {
    static std::once_flag flag;
    static std::unique_ptr<whiteout::utils::SimpleThreadPool> pool;

    std::call_once(flag, []() {
        const size_t cores = std::max<size_t>(
            std::thread::hardware_concurrency(), 2);
        pool = std::make_unique<whiteout::utils::SimpleThreadPool>(cores);
    });

    return pool.get();
}

/// Minimum number of sectors/chunks before parallel dispatch is worthwhile.
/// Below this threshold, sequential processing avoids thread-dispatch overhead.
constexpr int kMinParallelSectors = 4;
constexpr int kMinParallelChunks  = 3;

} // namespace whiteoutdex
