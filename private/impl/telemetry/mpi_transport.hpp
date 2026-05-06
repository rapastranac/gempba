#ifndef GEMPBA_TELEMETRY_MPI_TRANSPORT_HPP
#define GEMPBA_TELEMETRY_MPI_TRANSPORT_HPP

#include <gempba/telemetry/telemetry_transport.hpp>
#include <memory>

namespace gempba::telemetry {

    // MPI-backed transport. Uses MPI_Comm_dup of MPI_COMM_WORLD so the
    // telemetry tag space is fully isolated from task-message traffic and
    // cannot accidentally match a task receive. All ranks Isend their frames
    // to rank 0; rank 0 Iprobe + Recv drains them in poll(). Control messages
    // travel center → workers on the same dup'd communicator.
    //
    // Single-threaded: every MPI call originates from the scheduler's polling
    // loop on the rank that owns it. MPI_THREAD_SERIALIZED is sufficient.
    class mpi_transport final : public telemetry_transport {
    public:
        mpi_transport();
        ~mpi_transport() override;

        mpi_transport(const mpi_transport&) = delete;
        mpi_transport& operator=(const mpi_transport&) = delete;

        void publish_worker(const worker_frame& p_frame) override;
        void publish_node(const node_frame& p_frame) override;

        void poll(const worker_frame_sink& p_worker_sink, const node_frame_sink& p_node_sink) override;

        void send_control(std::uint32_t p_dst_worker_id, const control_msg& p_msg) override;
        std::optional<control_msg> try_recv_control() override;

    private:
        struct impl;
        std::unique_ptr<impl> m_impl;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_MPI_TRANSPORT_HPP
