#include <atomic>
#include <cstdint>
#include <gempba/core/stop.hpp>
#include <impl/stop_state.hpp>

namespace {
    constexpr std::uint32_t TRIGGER_SHIFT = 24U;
    constexpr std::uint32_t SIGNAL_MASK = (1U << TRIGGER_SHIFT) - 1;

    std::atomic<std::uint32_t> g_requested_stop{0};
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    constexpr std::uint32_t encode(const gempba::stop_trigger p_trigger, const int p_signal) {
        return static_cast<std::uint32_t>(p_trigger) << TRIGGER_SHIFT | (static_cast<std::uint32_t>(p_signal) & SIGNAL_MASK);
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

void gempba::request_stop() noexcept { detail::request_stop(stop_trigger::CODE); }

bool gempba::stop_requested() noexcept { return g_requested_stop.load(std::memory_order_relaxed) != 0; }
