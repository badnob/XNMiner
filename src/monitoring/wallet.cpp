#include "monitoring/wallet.hpp"

#include "util/cpu_affinity.hpp"
#include "util/http.hpp"
#include "util/paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace xn {
namespace {

constexpr const char* XUNI_CONTRACT = "0x999999cf1046e68e36e1aa2e0e07105eddd00002";
constexpr const char* XBLK_CONTRACT = "0x999999cf1046e68e36e1aa2e0e07105eddd00001";
constexpr double WEI_PER_TOKEN = 1e18;
constexpr const char* RPC_PRIMARY = "https://xenblocks.io:5556";
constexpr const char* RPC_FALLBACK = "http://xenblocks.io:5555";
// Holdings value only (dashboard XNM/XUNI/XBLK). Never m= and never a flush clock.
constexpr const char* LEADERBOARD_URL = "https://xenblocks.io/v1/leaderboard?limit=400";
constexpr int kWalletRpcTimeoutMs = 5000;
constexpr int kLeaderboardTimeoutMs = 8000;
// Idle poll: tight enough that Holdings age stays honest on the dashboard.
constexpr double kWalletIdleIntervalS = 25.0;
// After /verify credits, don't HTTP-spam the chain RPC (32-wide flush).
constexpr double kWalletForceMinIntervalS = 8.0;
constexpr double kWalletStaleS = 75.0;

std::string lower_copy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

double now_s() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<std::string> rpc_result_url(const char* url, const std::string& method,
                                          const nlohmann::json& params) {
    nlohmann::json body = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}, {"id", 1}};
    auto resp = http_post_json(url, body.dump(), kWalletRpcTimeoutMs);
    if (resp.status < 200 || resp.status >= 300) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (!j.contains("result")) return std::nullopt;
        if (j["result"].is_string()) return j["result"].get<std::string>();
        return j["result"].dump();
    } catch (...) {
    }
    return std::nullopt;
}

double hex_wei_to_token(const std::string& hex) {
    std::string h = hex;
    if (h.rfind("0x", 0) == 0 || h.rfind("0X", 0) == 0) h = h.substr(2);
    if (h.empty()) return 0;
    long double val = 0;
    for (char c : h) {
        int d = 0;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else continue;
        val = val * 16.0L + d;
    }
    return static_cast<double>(val / static_cast<long double>(WEI_PER_TOKEN));
}

double json_wei_to_token(const nlohmann::json& v) {
    if (v.is_number()) return v.get<double>() / WEI_PER_TOKEN;
    if (v.is_string()) {
        const auto s = v.get<std::string>();
        if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) return hex_wei_to_token(s);
        try {
            return std::stold(s) / static_cast<long double>(WEI_PER_TOKEN);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }

bool balances_changed(const TokenBalances& a, const TokenBalances& b) {
    return std::fabs(a.xnm - b.xnm) >= 0.05 || std::fabs(a.xuni - b.xuni) >= 0.05 ||
           std::fabs(a.xblk - b.xblk) >= 0.05;
}

std::string fmt_tok(double v) {
    v = round2(v);
    std::ostringstream o;
    o << std::fixed;
    if (std::fabs(v - std::round(v)) < 0.001)
        o << std::setprecision(0) << v;
    else
        o << std::setprecision(1) << v;
    return o.str();
}

std::string fmt_delta(double d) {
    d = round2(d);
    if (std::fabs(d) < 0.05) return {};
    std::ostringstream o;
    o << std::fixed;
    if (std::fabs(d - std::round(d)) < 0.001)
        o << std::setprecision(0) << (d >= 0 ? "+" : "") << d;
    else
        o << std::setprecision(1) << (d >= 0 ? "+" : "") << d;
    return o.str();
}

std::string tok_field(const char* name, double now, double base) {
    std::string s = std::string(name) + " " + fmt_tok(now);
    auto d = fmt_delta(now - base);
    if (!d.empty()) s += " " + d;
    return s;
}

std::string age_txt(double s) {
    if (s < 0) s = 0;
    int n = static_cast<int>(s);
    if (n < 90) return std::to_string(n) + "s";
    return std::to_string(n / 60) + "m";
}

}  // namespace

WalletBalanceTracker::WalletBalanceTracker(std::string address, std::filesystem::path history_path)
    : address_(std::move(address)), history_path_(std::move(history_path)) {
    ensure_parent_dir(history_path_);
    load_last_known();
}

WalletBalanceTracker::~WalletBalanceTracker() { stop(); }

void WalletBalanceTracker::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] {
        cpu::pin_this_thread(cpu::Role::Dashboard);
        loop();
    });
}

void WalletBalanceTracker::stop() {
    if (!running_ && !thread_.joinable()) return;
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WalletBalanceTracker::maybe_refresh(bool force) {
    // Mining thread: never HTTP. The worker polls on its own interval.
    if (!force) return;
    force_ = true;
    cv_.notify_one();
}

void WalletBalanceTracker::load_last_known() {
    std::ifstream in(history_path_);
    if (!in) return;
    std::string line, last;
    while (std::getline(in, line)) {
        if (!line.empty()) last = line;
    }
    if (last.empty()) return;
    try {
        auto j = nlohmann::json::parse(last, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return;
        if (j.contains("ok") && j["ok"].is_boolean() && !j["ok"].get<bool>()) return;
        TokenBalances bal;
        bal.xnm = j.value("xnm", 0.0);
        bal.xuni = j.value("xuni", 0.0);
        bal.xblk = j.value("xblk", 0.0);
        std::lock_guard<std::mutex> lock(mu_);
        current_ = bal;
        baseline_ = bal;
        source_ = j.value("source", std::string("cached"));
        last_ok_s_ = 0;
    } catch (...) {
    }
}

bool WalletBalanceTracker::fetch_leaderboard(TokenBalances& out) {
    auto resp = http_get(LEADERBOARD_URL, kLeaderboardTimeoutMs);
    if (resp.status < 200 || resp.status >= 300 || resp.body.empty()) return false;
    try {
        auto j = nlohmann::json::parse(resp.body, nullptr, false);
        if (j.is_discarded() || !j.contains("miners") || !j["miners"].is_array()) return false;
        const std::string want = lower_copy(address_);
        for (const auto& m : j["miners"]) {
            if (!m.is_object()) continue;
            if (lower_copy(m.value("account", "")) != want) continue;
            out.xnm = json_wei_to_token(m.value("xnm", nlohmann::json(0)));
            out.xuni = json_wei_to_token(m.value("xuni", nlohmann::json(0)));
            out.xblk = json_wei_to_token(m.value("xblk", nlohmann::json(0)));
            return true;
        }
    } catch (...) {
    }
    return false;
}

bool WalletBalanceTracker::fetch_rpc(const char* url, TokenBalances& out) {
    auto xnm_hex = rpc_result_url(url, "eth_getBalance", nlohmann::json::array({address_, "latest"}));
    if (!xnm_hex) return false;
    out.xnm = hex_wei_to_token(*xnm_hex);
    std::string addr = address_;
    if (addr.size() >= 2) addr = addr.substr(2);
    while (addr.size() < 64) addr = "0" + addr;
    std::string data = "0x70a08231" + addr;
    auto xuni_hex = rpc_result_url(
        url, "eth_call",
        nlohmann::json::array({nlohmann::json{{"to", XUNI_CONTRACT}, {"data", data}}, "latest"}));
    auto xblk_hex = rpc_result_url(
        url, "eth_call",
        nlohmann::json::array({nlohmann::json{{"to", XBLK_CONTRACT}, {"data", data}}, "latest"}));
    if (xuni_hex) out.xuni = hex_wei_to_token(*xuni_hex);
    if (xblk_hex) out.xblk = hex_wei_to_token(*xblk_hex);
    return true;
}

bool WalletBalanceTracker::fetch(TokenBalances& out, std::string& source) {
    // Leaderboard is strictly holdings. RPC is a fallback if this wallet is not listed.
    if (fetch_leaderboard(out)) {
        source = "leaderboard";
        return true;
    }
    if (fetch_rpc(RPC_PRIMARY, out)) {
        source = "xenblocks.io";
        return true;
    }
    if (fetch_rpc(RPC_FALLBACK, out)) {
        source = "xenblocks.io";
        return true;
    }
    return false;
}

void WalletBalanceTracker::apply_fetch(bool ok, const TokenBalances& bal, const std::string& source) {
    const double now = now_s();
    bool write_history = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (ok) {
            const bool first = !current_.has_value() || last_ok_s_ <= 0.0;
            const bool recovered = fail_streak_ > 0;
            const bool moved = current_.has_value() && balances_changed(*current_, bal);
            if (!baseline_) baseline_ = current_ ? *current_ : bal;
            current_ = bal;
            source_ = source;
            last_ok_s_ = now;
            fail_streak_ = 0;
            if (first || recovered || moved) {
                const TokenBalances base = baseline_.value_or(bal);
                std::ostringstream oss;
                oss << "Holdings " << source << "  XNM " << fmt_tok(bal.xnm);
                auto dx = fmt_delta(bal.xnm - base.xnm);
                if (!dx.empty()) oss << " " << dx << " sess";
                oss << "  XUNI " << fmt_tok(bal.xuni);
                auto du = fmt_delta(bal.xuni - base.xuni);
                if (!du.empty()) oss << " " << du;
                oss << "  XBLK " << fmt_tok(bal.xblk);
                auto db = fmt_delta(bal.xblk - base.xblk);
                if (!db.empty()) oss << " " << db;
                if (moved)
                    oss << "  (xenblocks.io moved)";
                else if (recovered)
                    oss << "  (poll recovered)";
                notice_ = oss.str();
            }
            write_history = true;
        } else {
            ++fail_streak_;
            if (fail_streak_ == 1 || fail_streak_ == 3 || fail_streak_ % 8 == 0) {
                std::ostringstream oss;
                oss << "Holdings poll failed";
                if (last_ok_s_ > 0)
                    oss << " — last " << (source_.empty() ? "xenblocks.io" : source_) << " "
                        << age_txt(now - last_ok_s_) << " ago";
                else
                    oss << " — no xenblocks.io reading yet";
                notice_ = oss.str();
            }
        }
    }
    if (!write_history) return;
    try {
        nlohmann::json j = {{"ts", now_iso_local()},
                            {"ok", true},
                            {"source", source},
                            {"xnm", bal.xnm},
                            {"xuni", bal.xuni},
                            {"xblk", bal.xblk}};
        std::ofstream out(history_path_, std::ios::app);
        out << j.dump() << "\n";
    } catch (...) {
    }
}

void WalletBalanceTracker::loop() {
    try {
        while (running_) {
            const bool forced = force_.exchange(false);
            const double now = now_s();
            const double since = last_attempt_ <= 0.0 ? 1e9 : (now - last_attempt_);
            const double min_gap = forced ? kWalletForceMinIntervalS : kWalletIdleIntervalS;
            bool do_fetch = since >= min_gap;
            if (forced && !do_fetch) {
                // Keep the wake; retry as soon as the 8s floor elapses.
                force_ = true;
            }
            if (!forced && last_attempt_ > 0.0 && since < kWalletIdleIntervalS) {
                do_fetch = false;
            }
            if (do_fetch) {
                last_attempt_ = now_s();
                TokenBalances bal;
                std::string source;
                const bool ok = fetch(bal, source);
                last_attempt_ = now_s();
                apply_fetch(ok, bal, source);
            }
            double wait_s = 1.0;
            if (force_.load()) {
                const double remain = kWalletForceMinIntervalS - (now_s() - last_attempt_);
                if (remain > 0.05 && remain < wait_s) wait_s = remain;
                else if (remain > wait_s) wait_s = std::min(remain, kWalletForceMinIntervalS);
            } else if (last_attempt_ > 0.0) {
                const double remain = kWalletIdleIntervalS - (now_s() - last_attempt_);
                if (remain > 1.0) wait_s = std::min(remain, 5.0);
            }
            if (wait_s < 0.05) wait_s = 0.05;
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::duration<double>(wait_s),
                         [this] { return !running_.load() || force_.load(); });
        }
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lock(mu_);
        notice_ = std::string("Holdings poll crashed: ") + ex.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mu_);
        notice_ = "Holdings poll crashed";
    }
}

std::optional<TokenBalances> WalletBalanceTracker::current() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_;
}

std::optional<std::string> WalletBalanceTracker::take_notice() {
    std::lock_guard<std::mutex> lock(mu_);
    if (notice_.empty()) return std::nullopt;
    std::string out = notice_;
    notice_.clear();
    return out;
}

std::string WalletBalanceTracker::summary_line() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (!current_) return "n/a  polling xenblocks.io";
    const TokenBalances bal = *current_;
    const TokenBalances base = baseline_.value_or(bal);
    const double now = now_s();
    std::ostringstream oss;
    oss << tok_field("XNM", bal.xnm, base.xnm) << "  "
        << tok_field("XUNI", bal.xuni, base.xuni) << "  "
        << tok_field("XBLK", bal.xblk, base.xblk);
    if (last_ok_s_ <= 0.0) {
        oss << "  cached";
        return oss.str();
    }
    const double age = now - last_ok_s_;
    const std::string src = source_.empty() ? "xenblocks.io" : source_;
    if (age >= kWalletStaleS) {
        oss << "  STALE " << age_txt(age);
    } else {
        oss << "  " << src << " " << age_txt(age);
    }
    return oss.str();
}

}  // namespace xn
