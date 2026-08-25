#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace xn {

struct TokenBalances {
    double xnm = 0;
    double xuni = 0;
    double xblk = 0;
};

/// Wallet RPC runs on its own thread. The mining loop only reads the cache.
class WalletBalanceTracker {
public:
    WalletBalanceTracker(std::string address, std::filesystem::path history_path);
    ~WalletBalanceTracker();

    WalletBalanceTracker(const WalletBalanceTracker&) = delete;
    WalletBalanceTracker& operator=(const WalletBalanceTracker&) = delete;

    void start();
    void stop();
    /// Non-blocking. force=true wakes the worker; never performs HTTP here.
    void maybe_refresh(bool force = false);
    std::optional<TokenBalances> current() const;
    std::string summary_line() const;
    /// One-shot log line when xenblocks.io balances move or the poll recovers/fails.
    std::optional<std::string> take_notice();

private:
    bool fetch(TokenBalances& out, std::string& source);
    bool fetch_rpc(const char* url, TokenBalances& out);
    bool fetch_leaderboard(TokenBalances& out);
    void load_last_known();
    void loop();
    void apply_fetch(bool ok, const TokenBalances& bal, const std::string& source);

    std::string address_;
    std::filesystem::path history_path_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::optional<TokenBalances> current_;
    std::optional<TokenBalances> baseline_;
    std::string source_;
    std::string notice_;
    double last_attempt_ = 0;
    double last_ok_s_ = 0;
    int fail_streak_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> force_{false};
    std::thread thread_;
};

}  // namespace xn
