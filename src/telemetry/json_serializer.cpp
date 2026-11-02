#include <impl/telemetry/json_serializer.hpp>

#include <cstdio>

namespace gempba::telemetry {

    namespace {

        void escape_quoted(std::string& p_out, const char* p_str) {
            p_out += '"';
            for (const char* v_p = p_str; *v_p; ++v_p) {
                const char v_c = *v_p;
                switch (v_c) {
                    case '"':
                        p_out += "\\\"";
                        break;
                    case '\\':
                        p_out += "\\\\";
                        break;
                    case '\n':
                        p_out += "\\n";
                        break;
                    case '\r':
                        p_out += "\\r";
                        break;
                    case '\t':
                        p_out += "\\t";
                        break;
                    default:
                        if (static_cast<unsigned char>(v_c) < 0x20) {
                            char v_buf[8];
                            std::snprintf(v_buf, sizeof(v_buf), "\\u%04x", static_cast<unsigned>(v_c));
                            p_out += v_buf;
                        } else {
                            p_out += v_c;
                        }
                }
            }
            p_out += '"';
        }

        void escape_quoted(std::string& p_out, const std::string& p_str) { escape_quoted(p_out, p_str.c_str()); }

        void emit_u64(std::string& p_out, std::uint64_t p_v) {
            char v_buf[32];
            std::snprintf(v_buf, sizeof(v_buf), "%llu", static_cast<unsigned long long>(p_v));
            p_out += v_buf;
        }

        void emit_u32(std::string& p_out, std::uint32_t p_v) { emit_u64(p_out, p_v); }

        void emit_float(std::string& p_out, float p_v) {
            char v_buf[32];
            std::snprintf(v_buf, sizeof(v_buf), "%.3f", static_cast<double>(p_v));
            p_out += v_buf;
        }

    } // namespace

    void serialize_worker_frame(std::string& p_out, const worker_frame& p_frame) {
        p_out += "{\"worker_id\":";
        emit_u32(p_out, p_frame.m_worker_id);
        p_out += ",\"seq_no\":";
        emit_u32(p_out, p_frame.m_seq_no);
        p_out += ",\"worker_local_ms\":";
        emit_u64(p_out, p_frame.m_worker_local_ms);
        p_out += ",\"tasks_started_total\":";
        emit_u64(p_out, p_frame.m_tasks_started_total);
        p_out += ",\"tasks_sent_total\":";
        emit_u64(p_out, p_frame.m_tasks_sent_total);
        p_out += ",\"tasks_recv_total\":";
        emit_u64(p_out, p_frame.m_tasks_recv_total);
        p_out += ",\"bytes_sent_total\":";
        emit_u64(p_out, p_frame.m_bytes_sent_total);
        p_out += ",\"bytes_recv_total\":";
        emit_u64(p_out, p_frame.m_bytes_recv_total);
        p_out += ",\"send_microseconds_total\":";
        emit_u64(p_out, p_frame.m_send_microseconds_total);
        p_out += ",\"recv_microseconds_total\":";
        emit_u64(p_out, p_frame.m_recv_microseconds_total);
        p_out += ",\"spawn_rejected_pool_full\":";
        emit_u64(p_out, p_frame.m_spawn_rejected_pool_full);
        p_out += ",\"spawn_remote_rejected\":";
        emit_u64(p_out, p_frame.m_spawn_remote_rejected);
        p_out += ",\"tasks_running\":";
        emit_u32(p_out, p_frame.m_tasks_running);
        p_out += ",\"scheduler_pending_count\":";
        emit_u32(p_out, p_frame.m_scheduler_pending_count);
        p_out += ",\"idle_microseconds_per_worker\":";
        emit_u64(p_out, p_frame.m_idle_microseconds_per_worker);
        p_out += ",\"idle_microseconds_starved\":";
        emit_u64(p_out, p_frame.m_idle_microseconds_starved);
        p_out += ",\"idle_microseconds_imbalanced\":";
        emit_u64(p_out, p_frame.m_idle_microseconds_imbalanced);
        p_out += ",\"sched_running_microseconds\":";
        emit_u64(p_out, p_frame.m_sched_running_microseconds);
        p_out += ",\"sched_waiting_microseconds\":";
        emit_u64(p_out, p_frame.m_sched_waiting_microseconds);
        p_out += ",\"first_task_ms\":";
        emit_u64(p_out, p_frame.m_first_task_ms);
        p_out += ",\"last_task_ms\":";
        emit_u64(p_out, p_frame.m_last_task_ms);
        p_out += ",\"assign_latency_microseconds_total\":";
        emit_u64(p_out, p_frame.m_assign_latency_microseconds_total);
        p_out += ",\"assign_count\":";
        emit_u64(p_out, p_frame.m_assign_count);
        p_out += ",\"assign_latency_microseconds_max\":";
        emit_u64(p_out, p_frame.m_assign_latency_microseconds_max);
        p_out += ",\"available_microseconds_queue_empty\":";
        emit_u64(p_out, p_frame.m_available_microseconds_queue_empty);
        p_out += ",\"center_state_msgs_total\":";
        emit_u64(p_out, p_frame.m_center_state_msgs_total);
        p_out += ",\"center_score_proposals_total\":";
        emit_u64(p_out, p_frame.m_center_score_proposals_total);
        p_out += ",\"center_score_broadcasts_total\":";
        emit_u64(p_out, p_frame.m_center_score_broadcasts_total);
        p_out += ",\"score_proposals_sent\":";
        emit_u64(p_out, p_frame.m_score_proposals_sent);
        p_out += ",\"last_score_improvement_ms\":";
        emit_u64(p_out, p_frame.m_last_score_improvement_ms);
        p_out += ",\"center_full_microseconds\":";
        emit_u64(p_out, p_frame.m_center_full_microseconds);
        p_out += ",\"telemetry_microseconds_total\":";
        emit_u64(p_out, p_frame.m_telemetry_microseconds_total);
        p_out += ",\"process_cpu_pct\":";
        emit_float(p_out, p_frame.m_process_cpu_pct);
        p_out += ",\"process_rss_bytes\":";
        emit_u64(p_out, p_frame.m_process_rss_bytes);
        p_out += ",\"process_threads\":";
        emit_u32(p_out, p_frame.m_process_threads);

        // Sparse: only non-zero entries are emitted.
        p_out += ",\"edges_out\":[";
        bool v_first = true;
        for (std::uint32_t v_i = 0; v_i < MAX_WORKERS; ++v_i) {
            if (p_frame.m_edges_out[v_i].m_count == 0)
                continue;
            if (!v_first)
                p_out += ',';
            p_out += "{\"to\":";
            emit_u32(p_out, v_i);
            p_out += ",\"bytes\":";
            emit_u64(p_out, p_frame.m_edges_out[v_i].m_bytes);
            p_out += ",\"count\":";
            emit_u64(p_out, p_frame.m_edges_out[v_i].m_count);
            p_out += '}';
            v_first = false;
        }
        p_out += "]}";
    }

    void serialize_node_frame(std::string& p_out, const node_frame& p_frame) {
        p_out += "{\"sentinel_worker_id\":";
        emit_u32(p_out, p_frame.m_sentinel_worker_id);
        p_out += ",\"sentinel_local_ms\":";
        emit_u64(p_out, p_frame.m_sentinel_local_ms);
        p_out += ",\"hostname\":";
        escape_quoted(p_out, p_frame.m_hostname);
        p_out += ",\"socket_count\":";
        emit_u32(p_out, p_frame.m_socket_count);
        p_out += ",\"logical_cores\":";
        emit_u32(p_out, p_frame.m_logical_cores);
        p_out += ",\"mem_total_bytes\":";
        emit_u64(p_out, p_frame.m_mem_total_bytes);
        p_out += ",\"mem_avail_bytes\":";
        emit_u64(p_out, p_frame.m_mem_avail_bytes);
        p_out += ",\"cgroup_mem_used_bytes\":";
        emit_u64(p_out, p_frame.m_cgroup_mem_used_bytes);
        p_out += ",\"cgroup_mem_limit_bytes\":";
        emit_u64(p_out, p_frame.m_cgroup_mem_limit_bytes);
        p_out += ",\"sockets\":[";
        for (std::uint8_t v_i = 0; v_i < p_frame.m_socket_count && v_i < MAX_SOCKETS; ++v_i) {
            if (v_i > 0)
                p_out += ',';
            const auto& v_s = p_frame.m_sockets[v_i];
            p_out += "{\"socket_id\":";
            emit_u32(p_out, v_s.m_socket_id);
            p_out += ",\"cpu_pct\":";
            emit_float(p_out, v_s.m_cpu_pct);
            p_out += ",\"mem_total_bytes\":";
            emit_u64(p_out, v_s.m_mem_total_bytes);
            p_out += ",\"mem_used_bytes\":";
            emit_u64(p_out, v_s.m_mem_used_bytes);
            p_out += '}';
        }
        p_out += "]}";
    }

    void serialize_topology(std::string& p_out, const topology_snapshot& p_topology) {
        p_out += "{\"nodes\":[";
        for (std::size_t v_i = 0; v_i < p_topology.m_nodes.size(); ++v_i) {
            if (v_i > 0)
                p_out += ',';
            const auto& v_n = p_topology.m_nodes[v_i];
            p_out += "{\"hostname\":";
            escape_quoted(p_out, v_n.m_hostname);
            p_out += ",\"sentinel_worker_id\":";
            emit_u32(p_out, v_n.m_sentinel_worker_id);
            p_out += ",\"total_physical_cores\":";
            emit_u32(p_out, v_n.m_total_physical_cores);
            p_out += ",\"total_logical_cores\":";
            emit_u32(p_out, v_n.m_total_logical_cores);
            p_out += ",\"mem_total_bytes\":";
            emit_u64(p_out, v_n.m_mem_total_bytes);

            p_out += ",\"worker_ids\":[";
            for (std::size_t v_j = 0; v_j < v_n.m_worker_ids.size(); ++v_j) {
                if (v_j > 0)
                    p_out += ',';
                emit_u32(p_out, v_n.m_worker_ids[v_j]);
            }
            p_out += "],\"sockets\":[";
            for (std::size_t v_s = 0; v_s < v_n.m_sockets.size(); ++v_s) {
                if (v_s > 0)
                    p_out += ',';
                const auto& v_sock = v_n.m_sockets[v_s];
                p_out += "{\"socket_id\":";
                emit_u32(p_out, v_sock.m_socket_id);
                p_out += ",\"name\":";
                escape_quoted(p_out, v_sock.m_name);
                p_out += ",\"physical_cores\":";
                emit_u32(p_out, v_sock.m_physical_cores);
                p_out += ",\"logical_cores\":";
                emit_u32(p_out, v_sock.m_logical_cores);
                p_out += ",\"cpu_ids\":[";
                for (std::size_t v_c = 0; v_c < v_sock.m_cpu_ids.size(); ++v_c) {
                    if (v_c > 0)
                        p_out += ',';
                    emit_u32(p_out, v_sock.m_cpu_ids[v_c]);
                }
                p_out += "]}";
            }
            p_out += "]}";
        }
        p_out += "],\"identities\":[";
        for (std::size_t v_i = 0; v_i < p_topology.m_identities.size(); ++v_i) {
            if (v_i > 0)
                p_out += ',';
            const auto& v_id = p_topology.m_identities[v_i];
            p_out += "{\"worker_id\":";
            emit_u32(p_out, v_id.m_worker_id);
            p_out += ",\"hostname\":";
            escape_quoted(p_out, v_id.m_hostname);
            p_out += ",\"pid\":";
            emit_u32(p_out, v_id.m_pid);
            p_out += ",\"primary_socket\":";
            emit_u32(p_out, v_id.m_primary_socket);
            p_out += ",\"allowed_cpu_ids\":[";
            bool v_first_cpu = true;
            for (unsigned v_word = 0; v_word < CPU_MASK_WORDS; ++v_word) {
                for (unsigned v_bit = 0; v_bit < 64u; ++v_bit) {
                    if (v_id.m_allowed_cpu_mask[v_word] & (1ULL << v_bit)) {
                        if (!v_first_cpu)
                            p_out += ',';
                        emit_u32(p_out, v_word * 64u + v_bit);
                        v_first_cpu = false;
                    }
                }
            }
            p_out += "]}";
        }
        p_out += "]}";
    }

    void serialize_broadcast(std::string& p_out, const broadcast_payload& p_payload) {
        p_out += "{\"version\":";
        emit_u32(p_out, TELEMETRY_SCHEMA_VERSION);
        p_out += ",\"ts\":";
        emit_u64(p_out, p_payload.m_now_ms);
        p_out += ",\"elapsed_seconds\":";
        emit_u64(p_out, p_payload.m_elapsed_seconds);

        p_out += ",\"topology\":";
        if (p_payload.m_topology) {
            serialize_topology(p_out, *p_payload.m_topology);
        } else {
            p_out += "null";
        }

        p_out += ",\"workers\":[";
        if (p_payload.m_workers) {
            for (std::size_t v_i = 0; v_i < p_payload.m_workers->size(); ++v_i) {
                if (v_i > 0)
                    p_out += ',';
                serialize_worker_frame(p_out, (*p_payload.m_workers)[v_i]);
            }
        }
        p_out += "],\"nodes\":[";
        if (p_payload.m_nodes) {
            for (std::size_t v_i = 0; v_i < p_payload.m_nodes->size(); ++v_i) {
                if (v_i > 0)
                    p_out += ',';
                serialize_node_frame(p_out, (*p_payload.m_nodes)[v_i]);
            }
        }
        p_out += "]}";
    }

} // namespace gempba::telemetry
