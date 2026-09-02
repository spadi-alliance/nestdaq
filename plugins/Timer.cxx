/** @file
 *  @brief Implements periodic timer utilities used by plugins.
 */

#include <chrono>
#include <utility>

#include <boost/system/error_code.hpp>

#include <fairlogger/Logger.h>

#include "plugins/Timer.h"

namespace nestdaq::daq::service {

Timer::~Timer() noexcept
{
    if (fTimer) {
        boost::system::error_code ec;
        fTimer->cancel(ec);
    }
}

void Timer::start(const std::shared_ptr<net::io_context> &ctx,
                  //const std::shared_ptr<strand_t> &strand,
                  unsigned int timeout_ms,
                  std::function<bool(const std::error_code &)> f)
{
    // std::cout << " timer start " << timeout_ms << " msec" << std::endl;
    fContext   = ctx;
// fStrand    = strand;
    fTimer     = std::make_unique<net::steady_timer>(*fContext);
    fTimeoutMs = timeout_ms;
    fHandle    = std::move(f);
    start();
}

void Timer::start()
{
    fTimer->expires_after(std::chrono::milliseconds(fTimeoutMs));
    fTimer->async_wait( //
    [this](const auto &ec) {
        // std::cout << "# timer canceled : " << ec.message() << std::endl;
        if (ec == std::errc::operation_canceled) {
            LOG(debug) << " timer canceled : " << ec.message();
        } else if (ec) {
            LOG(debug) << " error. timer stopped: " << ec.message();
        } else {
            if (!fHandle(ec)) {
                // std::cout << " restart timer" << std::endl;
                fTimer->cancel();
                start();
            }
            // std::cout << " no restart timer" << std::endl;
        }
    });
}

} // namespace nestdaq::daq::service
