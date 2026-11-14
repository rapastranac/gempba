#ifndef GEMPBA_STOP_RELAY_HPP
#define GEMPBA_STOP_RELAY_HPP

#include <array>
#include <gempba/core/stop.hpp>
#include <impl/stop_state.hpp>
#include <mpi.h>
#include <optional>

namespace gempba {

    /**
     * @brief Carries a stop between the ranks of a built-in scheduler: a worker tells the root once, and the root tells every
     * worker once.
     *
     * Task acknowledgements carry this rank's stop too (@ref current, @ref apply). A rank only acknowledges its parent once its
     * subtree is done, so a stop that pruned any subtree reaches the root before the root's deficit can reach zero, and the
     * outcome the root sends with the termination message is the one every rank returns.
     */
    class stop_relay final {
    public:
        using payload = std::array<int, 2>;
        using outcome_payload = std::array<int, 3>;

        stop_relay(const int p_tag, const int p_root) : m_tag(p_tag), m_root(p_root) {}

        /// @return this rank's stop, as acknowledgements and stop messages carry it
        [[nodiscard]] static payload current() {
            const run_outcome v_stop = detail::requested_stop();
            return {static_cast<int>(v_stop.m_trigger), v_stop.m_signal};
        }

        /// @brief Request the stop @p p_payload carries, if any.
        static void apply(const payload& p_payload) {
            if (const auto v_trigger = static_cast<stop_trigger>(p_payload[0]); v_trigger != stop_trigger::NONE) {
                detail::request_stop(v_trigger, p_payload[1]);
            }
        }

        [[nodiscard]] static outcome_payload encode(const run_outcome& p_outcome) {
            return {static_cast<int>(p_outcome.m_reason), static_cast<int>(p_outcome.m_trigger), p_outcome.m_signal};
        }

        [[nodiscard]] static run_outcome decode(const int* p_values) { return {static_cast<end_reason>(p_values[0]), static_cast<stop_trigger>(p_values[1]), p_values[2]}; }

        /// @brief On a worker: send this rank's stop to the root, once, when one was requested.
        void report(MPI_Comm p_communicator) {
            if (m_relayed || !stop_requested()) {
                return;
            }
            const payload v_payload = current();
            MPI_Send(v_payload.data(), static_cast<int>(v_payload.size()), MPI_INT, m_root, m_tag, p_communicator);
            m_relayed = true;
        }

        /// @brief On the root: send its stop to every worker, once, when one was requested.
        void spread(MPI_Comm p_communicator, const int p_world_size) {
            if (m_relayed || !stop_requested()) {
                return;
            }
            const payload v_payload = current();
            for (int v_rank = 0; v_rank < p_world_size; ++v_rank) {
                if (v_rank != m_root) {
                    MPI_Send(v_payload.data(), static_cast<int>(v_payload.size()), MPI_INT, v_rank, m_tag, p_communicator);
                }
            }
            m_relayed = true;
        }

        /// @brief Receive the stop message @p p_status announces and request its stop; the root's leaves nothing to report.
        void receive(const MPI_Status& p_status, MPI_Comm p_communicator) {
            apply(take(p_status, p_communicator));
            if (p_status.MPI_SOURCE == m_root) {
                m_relayed = true;
            }
        }

        /// @brief Receive the stop message @p p_status announces without acting on it, once the run's outcome is settled.
        void discard(const MPI_Status& p_status, MPI_Comm p_communicator) const { (void) take(p_status, p_communicator); }

        /// @brief On a worker busy with a task: receive the root's stop message, if one arrived.
        void receive_from_root(MPI_Comm p_communicator) {
            int v_arrived = 0;
            MPI_Status v_status;
            MPI_Iprobe(m_root, m_tag, p_communicator, &v_arrived, &v_status);
            if (v_arrived) {
                receive(v_status, p_communicator);
            }
        }

    private:
        [[nodiscard]] payload take(const MPI_Status& p_status, MPI_Comm p_communicator) const {
            payload v_payload{};
            MPI_Recv(v_payload.data(), static_cast<int>(v_payload.size()), MPI_INT, p_status.MPI_SOURCE, m_tag, p_communicator, MPI_STATUS_IGNORE);
            return v_payload;
        }

        int m_tag;
        int m_root;
        bool m_relayed = false;
    };

} // namespace gempba

#endif // GEMPBA_STOP_RELAY_HPP
