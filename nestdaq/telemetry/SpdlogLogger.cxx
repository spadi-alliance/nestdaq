/** @file
 *  @brief Implements spdlog logger creation with OTel sink or console fallback.
 */

#include "nestdaq/telemetry/SpdlogLogger.h"

#include "nestdaq/telemetry/Telemetry.h"

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace nestdaq::telemetry {
namespace {
struct ThreadPoolEntry {
    uint32_t queue_size{};
    uint32_t thread_count{};
    std::shared_ptr<spdlog::details::thread_pool> pool;
};

auto threadPoolMutex() -> std::mutex& {
    static auto gThreadPoolMutex = std::mutex{};
    return gThreadPoolMutex;
}

auto threadPools() -> std::vector<ThreadPoolEntry>& {
    static auto gThreadPools = std::vector<ThreadPoolEntry> {};
    return gThreadPools;
}

auto makeOverflowPolicy(std::string_view value) -> spdlog::async_overflow_policy {
    if (value == "overrun_oldest") {
        return spdlog::async_overflow_policy::overrun_oldest;
    }
    if (value == "discard_new") {
        return spdlog::async_overflow_policy::discard_new;
    }
    return spdlog::async_overflow_policy::block;
}

auto getOrCreateThreadPool(const SpdlogAsyncOptions& options) -> std::shared_ptr<spdlog::details::thread_pool> {
    const auto kLock = std::scoped_lock{threadPoolMutex()};
    for (const auto& entry : threadPools()) {
        if (entry.queue_size == options.queue_size && entry.thread_count == options.thread_count) {
            return entry.pool;
        }
    }

    auto pool = std::make_shared<spdlog::details::thread_pool>(options.queue_size, options.thread_count);
    threadPools().push_back(ThreadPoolEntry{
        .queue_size = options.queue_size,
        .thread_count = options.thread_count,
        .pool = pool,
    });
    return pool;
}
} // namespace

auto createSpdlogLogger(std::string_view name) -> std::shared_ptr<spdlog::logger> {
    auto sinks = std::vector<spdlog::sink_ptr> {};
    if (getSpdlogNativeConsoleEnabled()) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern(getSpdlogConsolePattern());
        sinks.emplace_back(std::move(console_sink));
    }
    if (auto otel_sink = createActiveSpdlogSink()) {
        sinks.emplace_back(std::move(otel_sink));
    }
    const auto kAsyncOptions = getSpdlogAsyncOptions();
    if (kAsyncOptions.enabled) {
        return std::make_shared<spdlog::async_logger>(
                   std::string{name},
                   sinks.begin(),
                   sinks.end(),
                   getOrCreateThreadPool(kAsyncOptions),
                   makeOverflowPolicy(kAsyncOptions.overflow_policy));
    }
    return std::make_shared<spdlog::logger>(std::string{name}, sinks.begin(), sinks.end());
}

} // namespace nestdaq::telemetry
