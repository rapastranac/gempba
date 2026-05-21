#include <impl/telemetry/mpi_transport.hpp>

#include <cstring>
#include <deque>
#include <mpi.h>
#include <mutex>
#include <utility>
#include <vector>

namespace gempba::telemetry {

    namespace {

        // Tag space is private to the dup'd communicator, but distinct values
        // help when reading wireshark/printf debugging output. Far enough from
        // the scheduler's 0..14 range to avoid confusion.
        constexpr int k_tag_worker_frame = 9001;
        constexpr int k_tag_node_frame = 9002;
        constexpr int k_tag_control = 9003;
        constexpr int k_center_rank = 0;

    } // namespace

    struct mpi_transport::impl {
        MPI_Comm m_comm = MPI_COMM_NULL;
        int m_rank = -1;
        int m_world_size = 0;

        // In-flight Isend bookkeeping. Each send keeps its buffer alive until
        // MPI_Test reaps it. publish_* and the destructor walk this list.
        struct pending_send {
            MPI_Request m_request{};
            std::vector<std::byte> m_buffer;
        };

        std::mutex m_sends_mtx;
        std::deque<pending_send> m_sends;

        // Inbound control queue, populated by try_recv_control's Iprobe. We
        // buffer in case the caller pulls more than one per tick.
        std::deque<control_msg> m_inbound_controls;

        void reap_completed_sends() {
            const std::scoped_lock v_lock(m_sends_mtx);
            for (auto v_it = m_sends.begin(); v_it != m_sends.end();) {
                int v_flag = 0;
                MPI_Test(&v_it->m_request, &v_flag, MPI_STATUS_IGNORE);
                if (v_flag) {
                    v_it = m_sends.erase(v_it);
                } else {
                    ++v_it;
                }
            }
        }

        // dst_rank == k_center_rank for worker frames, an explicit rank for
        // controls. Buffer is moved in so it stays alive until reaped.
        void post_isend(std::vector<std::byte>&& p_buffer, int p_dst_rank, int p_tag) {
            const std::scoped_lock v_lock(m_sends_mtx);
            m_sends.emplace_back();
            pending_send& v_slot = m_sends.back();
            v_slot.m_buffer = std::move(p_buffer);
            MPI_Isend(v_slot.m_buffer.data(), static_cast<int>(v_slot.m_buffer.size()), MPI_BYTE, p_dst_rank, p_tag, m_comm, &v_slot.m_request);
        }
    };

    mpi_transport::mpi_transport() : m_impl(std::make_unique<impl>()) {
        int v_initialized = 0;
        MPI_Initialized(&v_initialized);
        if (!v_initialized) {
            // Caller forgot MPI_Init or we are in a non-MPI build path. The
            // transport stays inert; publish/poll/send_control all early-out.
            return;
        }
        MPI_Comm_dup(MPI_COMM_WORLD, &m_impl->m_comm);
        MPI_Comm_rank(m_impl->m_comm, &m_impl->m_rank);
        MPI_Comm_size(m_impl->m_comm, &m_impl->m_world_size);
    }

    mpi_transport::~mpi_transport() {
        if (m_impl->m_comm == MPI_COMM_NULL)
            return;

        // Wait for in-flight sends so MPI_Comm_free does not race with active
        // operations. Holding the lock here is safe because no other thread
        // touches the comm by the time the destructor fires.
        {
            const std::scoped_lock v_lock(m_impl->m_sends_mtx);
            for (auto& v_send: m_impl->m_sends) {
                MPI_Wait(&v_send.m_request, MPI_STATUS_IGNORE);
            }
            m_impl->m_sends.clear();
        }

        // Drain any inbound messages still on the wire so MPI_Comm_free does
        // not complain about pending receives. Best-effort: if peers have
        // already exited, Iprobe simply returns no message.
        int v_finalized = 0;
        MPI_Finalized(&v_finalized);
        if (!v_finalized) {
            int v_flag = 0;
            MPI_Status v_status;
            do {
                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_impl->m_comm, &v_flag, &v_status);
                if (v_flag) {
                    int v_count = 0;
                    MPI_Get_count(&v_status, MPI_BYTE, &v_count);
                    std::vector<std::byte> v_drop(static_cast<std::size_t>(v_count));
                    MPI_Recv(v_drop.data(), v_count, MPI_BYTE, v_status.MPI_SOURCE, v_status.MPI_TAG, m_impl->m_comm, MPI_STATUS_IGNORE);
                }
            } while (v_flag);

            MPI_Comm_free(&m_impl->m_comm);
        }
    }

    void mpi_transport::publish_worker(const worker_frame& p_frame) {
        if (m_impl->m_comm == MPI_COMM_NULL)
            return;

        m_impl->reap_completed_sends();

        std::vector<std::byte> v_buffer(sizeof(worker_frame));
        std::memcpy(v_buffer.data(), &p_frame, sizeof(worker_frame));
        m_impl->post_isend(std::move(v_buffer), k_center_rank, k_tag_worker_frame);
    }

    void mpi_transport::publish_node(const node_frame& p_frame) {
        if (m_impl->m_comm == MPI_COMM_NULL)
            return;

        m_impl->reap_completed_sends();

        std::vector<std::byte> v_buffer(sizeof(node_frame));
        std::memcpy(v_buffer.data(), &p_frame, sizeof(node_frame));
        m_impl->post_isend(std::move(v_buffer), k_center_rank, k_tag_node_frame);
    }

    void mpi_transport::poll(const worker_frame_sink& p_worker_sink, const node_frame_sink& p_node_sink) {
        if (m_impl->m_comm == MPI_COMM_NULL)
            return;
        if (m_impl->m_rank != k_center_rank)
            return;

        // Drain worker frames addressed to the center.
        while (true) {
            int v_flag = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, k_tag_worker_frame, m_impl->m_comm, &v_flag, &v_status);
            if (!v_flag)
                break;
            worker_frame v_frame{};
            MPI_Recv(&v_frame, sizeof(worker_frame), MPI_BYTE, v_status.MPI_SOURCE, k_tag_worker_frame, m_impl->m_comm, MPI_STATUS_IGNORE);
            if (p_worker_sink) {
                p_worker_sink(static_cast<std::uint32_t>(v_status.MPI_SOURCE), v_frame);
            }
        }

        // Drain node frames from sentinels.
        while (true) {
            int v_flag = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, k_tag_node_frame, m_impl->m_comm, &v_flag, &v_status);
            if (!v_flag)
                break;
            node_frame v_frame{};
            MPI_Recv(&v_frame, sizeof(node_frame), MPI_BYTE, v_status.MPI_SOURCE, k_tag_node_frame, m_impl->m_comm, MPI_STATUS_IGNORE);
            if (p_node_sink) {
                p_node_sink(static_cast<std::uint32_t>(v_status.MPI_SOURCE), v_frame);
            }
        }

        m_impl->reap_completed_sends();
    }

    void mpi_transport::send_control(const std::uint32_t p_dst_worker_id, const control_msg& p_msg) {
        if (m_impl->m_comm == MPI_COMM_NULL)
            return;
        if (std::cmp_greater_equal(p_dst_worker_id, m_impl->m_world_size))
            return;

        m_impl->reap_completed_sends();

        std::vector<std::byte> v_buffer(sizeof(control_msg));
        std::memcpy(v_buffer.data(), &p_msg, sizeof(control_msg));
        m_impl->post_isend(std::move(v_buffer), static_cast<int>(p_dst_worker_id), k_tag_control);
    }

    std::optional<control_msg> mpi_transport::try_recv_control() {
        if (m_impl->m_comm == MPI_COMM_NULL)
            return std::nullopt;

        // Drain any pending control messages addressed to this rank.
        while (true) {
            int v_flag = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, k_tag_control, m_impl->m_comm, &v_flag, &v_status);
            if (!v_flag)
                break;
            control_msg v_msg{};
            MPI_Recv(&v_msg, sizeof(control_msg), MPI_BYTE, v_status.MPI_SOURCE, k_tag_control, m_impl->m_comm, MPI_STATUS_IGNORE);
            m_impl->m_inbound_controls.push_back(v_msg);
        }

        if (m_impl->m_inbound_controls.empty())
            return std::nullopt;
        const control_msg v_msg = m_impl->m_inbound_controls.front();
        m_impl->m_inbound_controls.pop_front();
        return v_msg;
    }

} // namespace gempba::telemetry
