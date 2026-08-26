#include "monitoring/dashboard.hpp"

#include "common.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace xn {
namespace {

constexpr const char* RST = "\x1b[0m";
constexpr const char* DIM = "\x1b[90m";
constexpr const char* BOLD = "\x1b[1m";
constexpr const char* CYAN = "\x1b[36m";
constexpr const char* GREEN = "\x1b[32m";
constexpr const char* YELLOW = "\x1b[33m";
constexpr const char* RED = "\x1b[31m";
constexpr const char* WHITE = "\x1b[37m";
constexpr const char* PURPLE = "\x1b[35m";
constexpr const char* CLR_EOL = "\x1b[K";

constexpr int kWidth = 78;
constexpr int kLabel = 12;
constexpr int kHalf = 38;

std::string ascii_clean(std::string s) {
    for (char& c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || u > 126) c = '?';
    }
    return s;
}

int vis_len(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && (s[i] < '@' || s[i] > '~')) ++i;
            if (i < s.size()) ++i;
            continue;
        }
        ++n;
        ++i;
    }
    return n;
}

std::string pad_vis(std::string s, int width, bool right = true) {
    int n = vis_len(s);
    if (n < width) {
        s.insert(right ? s.end() : s.begin(), static_cast<size_t>(width - n), ' ');
    }
    return s;
}

std::string clip_vis(std::string s, int width) {
    int n = 0;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && (s[j] < '@' || s[j] > '~')) ++j;
            if (j < s.size()) ++j;
            out.append(s, i, j - i);
            i = j;
            continue;
        }
        if (n >= width) break;
        out.push_back(s[i]);
        ++n;
        ++i;
    }
    return out;
}

std::string fmt_hps(double hps) {
    std::ostringstream oss;
    oss << std::fixed;
    if (hps >= 1e6) {
        oss << std::setprecision(2) << (hps / 1e6) << " MH/s";
    } else if (hps >= 1e3) {
        oss << std::setprecision(1) << (hps / 1e3) << " kH/s";
    } else {
        oss << std::setprecision(0) << hps << " H/s";
    }
    return oss.str();
}

std::string fmt_hashes(int64_t n) {
    std::ostringstream oss;
    if (n >= 1'000'000'000) {
        oss << std::fixed << std::setprecision(2) << (n / 1e9) << "B";
    } else if (n >= 1'000'000) {
        oss << std::fixed << std::setprecision(2) << (n / 1e6) << "M";
    } else if (n >= 1'000) {
        oss << std::fixed << std::setprecision(1) << (n / 1e3) << "k";
    } else {
        oss << n;
    }
    return oss.str();
}

std::string fmt_int(int n) {
    const bool neg = n < 0;
    unsigned int v = static_cast<unsigned int>(neg ? -n : n);
    std::string digits = std::to_string(v);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    int lead = static_cast<int>(digits.size() % 3);
    if (lead == 0) lead = 3;
    out.append(digits, 0, static_cast<size_t>(lead));
    for (size_t i = static_cast<size_t>(lead); i < digits.size(); i += 3) {
        out.push_back(',');
        out.append(digits, i, 3);
    }
    if (neg) out.insert(out.begin(), '-');
    return out;
}

std::string fmt_uptime(int s) {
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int sec = s % 60;
    std::ostringstream oss;
    if (h > 0)
        oss << h << "h " << m << "m";
    else if (m > 0)
        oss << m << "m " << sec << "s";
    else
        oss << sec << "s";
    return oss.str();
}

std::string short_addr(const std::string& a) {
    if (a.size() < 12) return a;
    return a.substr(0, 6) + "..." + a.substr(a.size() - 4);
}

const char* temp_color(int c, int warn, int max) {
    if (c >= max) return RED;
    if (c >= warn) return YELLOW;
    return GREEN;
}

void row(std::ostringstream& oss, const std::string& body) {
    std::string inner = clip_vis(body, kWidth);
    int pad = kWidth - vis_len(inner);
    if (pad < 0) pad = 0;
    oss << DIM << "|" << RST << inner << std::string(static_cast<size_t>(pad), ' ') << DIM << "|"
        << RST << CLR_EOL << "\n";
}

void rule(std::ostringstream& oss) {
    oss << DIM << "+" << std::string(static_cast<size_t>(kWidth), '-') << "+" << RST << CLR_EOL
        << "\n";
}

std::string label(const char* name) {
    std::string s = name;
    if (static_cast<int>(s.size()) < kLabel) s.append(static_cast<size_t>(kLabel - s.size()), ' ');
    if (static_cast<int>(s.size()) > kLabel) s.resize(static_cast<size_t>(kLabel));
    return std::string("  ") + DIM + s + RST + " ";
}

std::string cell(const char* name, const std::string& value, int width = kHalf) {
    return pad_vis(label(name) + value, width);
}

const char* kind_color(const std::string& kind) {
    if (kind == "XNM") return GREEN;
    if (kind == "XBLK") return RED;
    if (kind == "XUNI") return YELLOW;
    return WHITE;
}

std::string paint_kind(const std::string& kind) {
    return std::string(kind_color(kind)) + kind + RST;
}

std::string color_kinds(std::string s) {
    auto repl = [&](const char* k) {
        const std::string painted = paint_kind(k);
        const std::size_t klen = std::char_traits<char>::length(k);
        std::size_t pos = 0;
        while ((pos = s.find(k, pos)) != std::string::npos) {
            s.replace(pos, klen, painted);
            pos += painted.size();
        }
    };
    repl("XBLK");
    repl("XUNI");
    repl("XNM");
    return s;
}

std::string layman_event(const std::string& action, const std::string& block,
                         const std::string& detail) {
    if (action == "FOUND") return "Found " + block;
    if (action == "QUEUED") {
        if (detail.find("network") != std::string::npos) return "Saved " + block + " - net down";
        if (detail.find("pool takes") != std::string::npos) return "Saved " + block + " - pool busy";
        if (detail.find("difficulty") != std::string::npos)
            return "Saved " + block + " - wait for match";
        if (detail.find("XUNI") != std::string::npos) return "Saved " + block + " - wait :55";
        return "Saved " + block + " in bag";
    }
    if (action == "ACCEPTED") {
        if (block == "QUEUE") {
            auto p = detail.rfind(' ');
            std::string n = (p != std::string::npos) ? detail.substr(p + 1) : detail;
            return "Pool paid bag x" + n;
        }
        return "Pool paid " + block;
    }
    if (action == "RESUBMIT") return "Retry " + block;
    std::string e = action + " " + block;
    if (!detail.empty()) e += " " + detail;
    return e;
}

std::vector<std::string> split_holdings(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    int spaces = 0;
    for (char c : line) {
        if (c == ' ') {
            ++spaces;
            continue;
        }
        if (spaces >= 2 && !cur.empty()) {
            parts.push_back(cur);
            cur.clear();
        } else if (spaces == 1 && !cur.empty()) {
            cur.push_back(' ');
        }
        spaces = 0;
        cur.push_back(c);
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

}  // namespace

MinerDashboard::MinerDashboard(const Settings& settings) : settings_(settings) {}

void MinerDashboard::start() {
    active_ = true;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        SetConsoleOutputCP(65001);
    }
#endif
    std::cout << "\x1b[?1049h\x1b[?25l\x1b[H\x1b[2J" << std::flush;
}

void MinerDashboard::stop() {
    if (!active_) return;
    active_ = false;
    std::cout << "\x1b[?25h\x1b[?1049l" << RST << std::flush;
}

void MinerDashboard::set_status(const std::string& status) {
    std::lock_guard<std::mutex> lock(mu_);
    status_ = ascii_clean(status);
}

void MinerDashboard::set_network(bool ok, std::optional<int> difficulty, bool stale) {
    std::lock_guard<std::mutex> lock(mu_);
    network_ok_ = ok;
    network_stale_ = stale;
    difficulty_ = difficulty;
}

void MinerDashboard::set_mining_m(int mining_m, bool force_hybrid) {
    std::lock_guard<std::mutex> lock(mu_);
    mining_m_ = mining_m;
    force_hybrid_ = force_hybrid;
}

void MinerDashboard::set_cuda_batch(int batch, int lanes, double thermal_scale) {
    std::lock_guard<std::mutex> lock(mu_);
    cuda_batch_ = batch;
    cuda_lanes_ = lanes;
    thermal_scale_ = thermal_scale;
}

void MinerDashboard::set_wallet_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    wallet_line_ = ascii_clean(line);
}

void MinerDashboard::set_uptime_s(int uptime_s) {
    std::lock_guard<std::mutex> lock(mu_);
    uptime_s_ = uptime_s;
}

void MinerDashboard::event(const std::string& action, const std::string& block,
                           const std::string& detail) {
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back(ascii_clean(layman_event(action, block, detail)));
    if (events_.size() > 4) events_.erase(events_.begin());
}

void MinerDashboard::update(const MiningStats& stats, const GpuSnapshot* gpu,
                            const std::unordered_map<std::string, int>& pending_by_type,
                            const std::unordered_map<std::string, int>& /*resubmission*/) {
    std::lock_guard<std::mutex> lock(mu_);
    stats_ = stats;
    if (gpu) gpu_ = *gpu;
    pending_xuni_ = 0;
    pending_xnm_ = 0;
    pending_xblk_ = 0;
    int q = 0;
    for (const auto& kv : pending_by_type) {
        q += kv.second;
        if (kv.first == "XUNI") pending_xuni_ = kv.second;
        else if (kv.first == "XBLK") pending_xblk_ = kv.second;
        else if (kv.first == "XNM") pending_xnm_ = kv.second;
    }
    stats_.queued = q;
}

void MinerDashboard::render() {
    if (!active_) return;
    std::lock_guard<std::mutex> lock(mu_);
    try {

    std::ostringstream oss;
    oss << "\x1b[H";

    rule(oss);
    {
        std::string left = std::string("  ") + BOLD + CYAN + kAppName + RST;
        std::string right = std::string(DIM) + "v" + kMinerVersion + RST + "  ";
        int gap = kWidth - vis_len(left) - vis_len(right);
        if (gap < 1) gap = 1;
        row(oss, left + std::string(static_cast<size_t>(gap), ' ') + right);
    }
    row(oss, std::string("  ") + DIM + kAppTagline + RST);
    rule(oss);

    const bool flushing = status_.rfind("FLUSH", 0) == 0;
    row(oss, cell("Uptime", std::string(PURPLE) + fmt_uptime(uptime_s_) + RST));
    if (flushing) {
        // "FLUSH 15/16 in flight · cleared 1188 (new 1171) · bag 92021"
        std::string rest = status_.size() > 6 ? status_.substr(6) : status_;
        std::string inflight, cleared, bag;
        auto cut = rest.find(" · ");
        if (cut != std::string::npos) {
            inflight = rest.substr(0, cut);
            rest = rest.substr(cut + 3);
            cut = rest.find(" · ");
            if (cut != std::string::npos) {
                cleared = rest.substr(0, cut);
                bag = rest.substr(cut + 3);
            } else {
                cleared = rest;
            }
        } else {
            inflight = rest;
        }
        if (!inflight.empty())
            row(oss, cell("In flight", std::string(YELLOW) + inflight + RST));
        if (!cleared.empty()) row(oss, cell("Cleared", cleared));
        if (!bag.empty()) row(oss, cell("Bag", bag));
    }
    row(oss, cell("Wallet", std::string(CYAN) + short_addr(settings_.address) + RST) +
                 cell("Worker", std::string(CYAN) + settings_.worker + RST));

    {
        std::string net;
        if (network_ok_ && !network_stale_)
            net = std::string(GREEN) + "ONLINE" + RST;
        else if (network_ok_ && network_stale_)
            net = std::string(YELLOW) + "STALE" + RST;
        else
            net = std::string(RED) + "DOWN" + RST;
        std::string mine;
        if (force_hybrid_ && mining_m_ > 0) {
            mine = std::string(GREEN) + "m=" + std::to_string(mining_m_) + RST +
                   std::string(DIM) + " Fix" + RST;
        } else {
            mine = difficulty_ ? ("m=" + std::to_string(*difficulty_)) : "-";
        }
        row(oss, cell("Network", net) + cell("Mining", mine));
    }
    {
        std::string netm = difficulty_ ? ("m=" + std::to_string(*difficulty_)) : "-";
        std::string match;
        if (force_hybrid_ && difficulty_ && *difficulty_ == mining_m_)
            match = std::string(GREEN) + "MATCH" + RST;
        else if (difficulty_)
            match = std::string(YELLOW) + "waiting for match" + RST;
        else
            match = "-";
        row(oss, cell("Difficulty", std::string(WHITE) + netm + RST) + cell("Window", match));
        row(oss, cell("CUDA", std::to_string(cuda_lanes_) + " lane" +
                                 (cuda_lanes_ == 1 ? "" : "s") + " x " + fmt_int(cuda_batch_)) +
                     cell("", ""));
    }

    rule(oss);
    row(oss, cell("Hashrate", std::string(BOLD) + WHITE + fmt_hps(stats_.hps_ema) + RST) +
                 cell("Hashes", fmt_hashes(stats_.total_hashes)));
    row(oss, cell("Rejected", (stats_.rejected_total() ? std::string(RED) : std::string(DIM)) +
                                  fmt_int(stats_.rejected_total()) + RST) +
                 cell("Queued", (stats_.queued ? std::string(YELLOW) : std::string(DIM)) +
                                    fmt_int(stats_.queued) + RST));
    {
        const std::string sub = std::string(DIM) + "F/A" + RST;
        int left = (kWidth - vis_len(sub)) / 2;
        if (left < 0) left = 0;
        row(oss, std::string(static_cast<size_t>(left), ' ') + sub);
        int ax = stats_.accepted_live_xuni + stats_.accepted_flush_xuni;
        int an = stats_.accepted_live_xnm + stats_.accepted_flush_xnm;
        int ab = stats_.accepted_live_xblk + stats_.accepted_flush_xblk;
        std::ostringstream b;
        b << paint_kind("XNM") << " " << fmt_int(stats_.found_xnm) << "/" << fmt_int(an)
          << "   " << paint_kind("XBLK") << " " << fmt_int(stats_.found_xblk) << "/" << fmt_int(ab)
          << "   " << paint_kind("XUNI") << " " << fmt_int(stats_.found_xuni) << "/" << fmt_int(ax);
        row(oss, cell("Blocks", b.str()));
    }
    {
        std::ostringstream b;
        b << paint_kind("XNM") << " " << fmt_int(pending_xnm_) << "   " << paint_kind("XBLK")
          << " " << fmt_int(pending_xblk_) << "   " << paint_kind("XUNI") << " "
          << fmt_int(pending_xuni_);
        row(oss, cell("Bag split", b.str()));
    }

    rule(oss);
    if (gpu_) {
        row(oss, cell("GPU", ascii_clean(gpu_->name)));
        double vram_pct = gpu_->total_mib > 0 ? (100.0 * gpu_->used_mib / gpu_->total_mib) : 0.0;
        std::ostringstream vram;
        vram << std::fixed << std::setprecision(1) << (gpu_->used_mib / 1024.0) << "/"
             << std::setprecision(1) << (gpu_->total_mib / 1024.0) << " GB  "
             << std::setprecision(0) << vram_pct << "%";
        row(oss, cell("Util", std::to_string(gpu_->util_pct) + " %") + cell("VRAM", vram.str()));
        const char* tc =
            temp_color(gpu_->temperature_c, settings_.warn_gpu_temp_c, settings_.max_gpu_temp_c);
        std::ostringstream core;
        core << tc << gpu_->temperature_c << " C" << RST << DIM << "  cap "
             << settings_.max_gpu_temp_c << " C" << RST;
        std::string pwr = gpu_->power_w >= 0
                              ? (std::to_string(static_cast<int>(gpu_->power_w)) + " W")
                              : std::string("-");
        row(oss, cell("Core", core.str()) + cell("Power", pwr));
        if (gpu_->has_memory_junction()) {
            const char* mc = temp_color(gpu_->memory_junction_c, settings_.warn_mem_temp_c,
                                        settings_.max_mem_temp_c);
            std::ostringstream j;
            j << mc << gpu_->memory_junction_c << " C" << RST << DIM << "  cap "
              << settings_.max_mem_temp_c << " C" << RST;
            row(oss, cell("Mem junc", j.str()));
        }
    } else {
        row(oss, cell("GPU", std::string(DIM) + "-" + RST));
    }

    rule(oss);
    {
        auto parts = split_holdings(wallet_line_);
        if (parts.empty()) {
            row(oss, cell("Holdings", std::string(DIM) + "-" + RST));
        } else {
            const bool stale = wallet_line_.find("STALE") != std::string::npos;
            const char* hc = stale ? YELLOW : WHITE;
            row(oss, cell("Holdings", std::string(hc) + parts[0] + RST));
            for (size_t i = 1; i < parts.size(); ++i) {
                row(oss, cell("", std::string(hc) + parts[i] + RST));
            }
        }
    }

    rule(oss);
    row(oss, std::string("  ") + DIM + "Recent" + RST);
    std::vector<std::string> recent;
    for (auto it = events_.rbegin(); it != events_.rend() && recent.size() < 4; ++it) {
        recent.push_back(*it);
    }
    while (recent.size() < 4) recent.push_back("");
    for (const auto& ev : recent) {
        if (ev.empty()) {
            row(oss, std::string("  ") + DIM + "-" + RST);
            continue;
        }
        const char* ec = WHITE;
        if (ev.find("Paid") != std::string::npos || ev.find("Found") != std::string::npos)
            ec = GREEN;
        else if (ev.find("Saved") != std::string::npos || ev.find("wait") != std::string::npos)
            ec = YELLOW;
        else if (ev.find("Retry") != std::string::npos || ev.find("fail") != std::string::npos ||
                 ev.find("ERROR") != std::string::npos)
            ec = RED;
        row(oss, std::string("  ") + ec + color_kinds(clip_vis(ev, kWidth - 4)) + RST);
    }

    rule(oss);
    row(oss, std::string(DIM) +
                 "  Ctrl+B D detach (keeps running)   Ctrl+C = stop and bag queue" + RST);
    rule(oss);
    oss << "\x1b[J";
    std::cout << oss.str() << std::flush;
    } catch (const std::exception& ex) {
        std::cerr << "dashboard render failed: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "dashboard render failed\n";
    }
}

}  // namespace xn
