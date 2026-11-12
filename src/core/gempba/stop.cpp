#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <gempba/core/stop.hpp>
#include <impl/stop_state.hpp>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>

extern "C" {
    static void gempba_stop_on_signal(const int p_signal) {
        gempba::detail::request_stop(gempba::stop_trigger::SIGNAL, p_signal);
#if defined(_WIN32)
        if (p_signal == SIGTERM) {
            std::signal(SIGTERM, gempba_stop_on_signal);
        }
#endif
    }
}

namespace {
    constexpr std::uint32_t TRIGGER_SHIFT = 24U;
    constexpr std::uint32_t SIGNAL_MASK = (1U << TRIGGER_SHIFT) - 1;

    std::atomic<std::uint32_t> g_requested_stop{0};
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    constexpr std::uint32_t encode(const gempba::stop_trigger p_trigger, const int p_signal) {
        return static_cast<std::uint32_t>(p_trigger) << TRIGGER_SHIFT | (static_cast<std::uint32_t>(p_signal) & SIGNAL_MASK);
    }

    struct stop_signal {
        int m_signal = 0;
        bool m_once = false;
        bool m_taken = false;
#if !defined(_WIN32)
        struct sigaction m_previous{};
#endif
    };

    std::mutex g_signals_mutex;
    bool g_signals_enabled = true;
    bool g_signals_wanted = false;
    std::array<stop_signal, 2> g_stop_signals{{{SIGINT, true}, {SIGTERM, false}}};

    void leave_to_its_handler(const stop_signal& p_signal) {
        spdlog::debug("gempba leaves {} to the handler already set; it won't stop the run gracefully", gempba::detail::signal_name(p_signal.m_signal));
    }

#if defined(_WIN32)
    void take(stop_signal& p_signal) {
        const auto v_previous = std::signal(p_signal.m_signal, gempba_stop_on_signal);
        if (v_previous == SIG_ERR) {
            return;
        }
        if (v_previous != SIG_DFL) {
            std::signal(p_signal.m_signal, v_previous);
            leave_to_its_handler(p_signal);
            return;
        }
        p_signal.m_taken = true;
    }

    void give_back(stop_signal& p_signal) {
        const auto v_current = std::signal(p_signal.m_signal, SIG_DFL);
        if (v_current != gempba_stop_on_signal && v_current != SIG_DFL && v_current != SIG_ERR) {
            std::signal(p_signal.m_signal, v_current);
        }
        p_signal.m_taken = false;
    }
#else
    void take(stop_signal& p_signal) {
        struct sigaction v_current{};
        if (sigaction(p_signal.m_signal, nullptr, &v_current) != 0) {
            return;
        }
        if ((v_current.sa_flags & SA_SIGINFO) != 0 || v_current.sa_handler != SIG_DFL) {
            leave_to_its_handler(p_signal);
            return;
        }
        struct sigaction v_ours{};
        v_ours.sa_handler = gempba_stop_on_signal;
        sigemptyset(&v_ours.sa_mask);
        v_ours.sa_flags = SA_RESTART | (p_signal.m_once ? SA_RESETHAND : 0);
        if (sigaction(p_signal.m_signal, &v_ours, &p_signal.m_previous) == 0) {
            p_signal.m_taken = true;
        }
    }

    void give_back(stop_signal& p_signal) {
        struct sigaction v_current{};
        if (sigaction(p_signal.m_signal, nullptr, &v_current) == 0 && (v_current.sa_flags & SA_SIGINFO) == 0 &&
            (v_current.sa_handler == gempba_stop_on_signal || v_current.sa_handler == SIG_DFL)) {
            sigaction(p_signal.m_signal, &p_signal.m_previous, nullptr);
        }
        p_signal.m_taken = false;
    }
#endif

    void take_all() {
        for (stop_signal& v_signal: g_stop_signals) {
            if (!v_signal.m_taken) {
                take(v_signal);
            }
        }
    }

    void give_back_all() {
        for (stop_signal& v_signal: g_stop_signals) {
            if (v_signal.m_taken) {
                give_back(v_signal);
            }
        }
    }
} // namespace

void gempba::detail::request_stop(const stop_trigger p_trigger, const int p_signal) noexcept {
    std::uint32_t v_none = 0;
    g_requested_stop.compare_exchange_strong(v_none, encode(p_trigger, p_signal), std::memory_order_acq_rel);
}

gempba::run_outcome gempba::detail::requested_stop() noexcept {
    const std::uint32_t v_encoded = g_requested_stop.load(std::memory_order_acquire);
    if (v_encoded == 0) {
        return {};
    }
    return {end_reason::INTERRUPTED, static_cast<stop_trigger>(v_encoded >> TRIGGER_SHIFT), static_cast<int>(v_encoded & SIGNAL_MASK)};
}

void gempba::detail::clear_stop() noexcept { g_requested_stop.store(0, std::memory_order_release); }

void gempba::detail::take_stop_signals() {
    const std::scoped_lock v_lock(g_signals_mutex);
    g_signals_wanted = true;
    if (g_signals_enabled) {
        take_all();
    }
}

void gempba::detail::release_stop_signals() {
    const std::scoped_lock v_lock(g_signals_mutex);
    g_signals_wanted = false;
    give_back_all();
}

std::string gempba::detail::signal_name(const int p_signal) {
    if (p_signal == SIGINT) {
        return "SIGINT";
    }
    if (p_signal == SIGTERM) {
        return "SIGTERM";
    }
    return std::to_string(p_signal);
}

void gempba::request_stop() noexcept { detail::request_stop(stop_trigger::CODE); }

bool gempba::stop_requested() noexcept { return g_requested_stop.load(std::memory_order_relaxed) != 0; }

void gempba::stop_on_signals(const bool p_enabled) {
    const std::scoped_lock v_lock(g_signals_mutex);
    g_signals_enabled = p_enabled;
    if (!p_enabled) {
        give_back_all();
    } else if (g_signals_wanted) {
        take_all();
    }
}
