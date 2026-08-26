#pragma once

#include "common.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace xn {

int accept_network_difficulty(int raw, int fallback);

class NetworkPoller {
public:
    NetworkPoller(std::string difficulty_url, int poll_interval_s, int down_poll_interval_s,
                  int timeout_s);
    ~NetworkPoller();

    void start();
    void stop();
    NetworkStatus get_status() const;
    NetworkStatus poll_once(int timeout_s = -1);
    /// Fired after every GET /difficulty snapshot (ok or fail) so CPU can arm flush immediately.
    void set_on_update(std::function<void()> cb);

private:
    void loop();

    std::string url_;
    int poll_interval_s_ = 15;
    int down_poll_interval_s_ = 30;
    int timeout_s_ = 3;
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    NetworkStatus status_;
    uint64_t seq_ = 0;
    std::function<void()> on_update_;
    std::thread thread_;
};

}  // namespace xn
