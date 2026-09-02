#pragma once

/**
 * @file Timer.h
 * @brief Repeating Boost.Asio timer wrapper used by service plugins.
 */

#include <functional>
#include <memory>
#include <boost/asio.hpp>

namespace nestdaq::daq::service {
namespace net = boost::asio;
using strand_t = net::strand<net::io_context::executor_type>;

/**
 * @brief Repeating timer that keeps invoking a callback until it returns false.
 */
class Timer {
public:
    Timer() = default;
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;
    ~Timer() noexcept;

    /**
     * @brief Start the timer on @p ctx with a fixed millisecond interval.
     *
     * The callback receives the Asio error code. Returning true schedules the
     * next tick; returning false stops the repeating timer.
     */
    void start(const std::shared_ptr<net::io_context> &ctx,
//           const std::shared_ptr<strand_t> &strand,
               unsigned int timeout_ms,
               std::function<bool(const std::error_code &)> f);

private:
    void start();

    std::shared_ptr<net::io_context> fContext;
//  std::shared_ptr<strand_t> fStrand;
    std::unique_ptr<net::steady_timer> fTimer;
    unsigned int fTimeoutMs{0};
    std::function<bool(const std::error_code &)> fHandle;

};

} // namespace nestdaq::daq::service
