#pragma once
#ifndef MPI_SCHEDULER_CENTRALIZED_HPP
#define MPI_SCHEDULER_CENTRALIZED_HPP

#include "centralized_utils.hpp"
#include "scheduler_parent.hpp"
#include "Resultholder/ResultHolderParent.hpp"
#include "utils/Queue.hpp"
#include "utils/tree.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib> /* srand, rand */
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mpi.h>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <spdlog/spdlog.h>

#include "utils/gempba_utils.hpp"
#include "utils/ipc/result.hpp"
#include "utils/ipc/task_packet.hpp"

// max memory is in mb, e.g. 1024 * 10 = 10 GB
#define MAX_MEMORY_MB (1024 * 10)

#define CENTER 0

#define STATE_RUNNING 1
#define STATE_ASSIGNED 2
#define STATE_AVAILABLE 3

#define TERMINATION_TAG 6
#define REFVAL_UPDATE_TAG 9

#define TASK_FROM_CENTER_TAG 12

#define HAS_RESULT_TAG 13
#define NO_RESULT_TAG 14

#define TASK_FOR_CENTER 15

#define CENTER_IS_FULL_TAG 16
#define CENTER_IS_FREE_TAG 17

#define TIMEOUT_TIME 2


#define CENTER_NBSTORED_TASKS_PER_PROCESS 1000

namespace gempba {

    class branch_handler;

    // inter process communication handler
    class mpi_centralized_scheduler final : public scheduler_parent {

        std::priority_queue<task_packet, std::vector<task_packet>, TaskComparator> m_center_queue; //message, size
        //std::vector<task_packet> center_queue;

        int m_max_queue_size;
        bool m_center_last_full_status = false;

        double m_time_centerfull_sent = 0;


        std::vector<task_packet> m_local_outqueue;
        std::vector<task_packet> m_local_inqueue;

    public:
        ~mpi_centralized_scheduler() override {
            finalize();
        }

        static mpi_centralized_scheduler &get_instance() {
            static mpi_centralized_scheduler instance;
            return instance;
        }

        [[nodiscard]] int rank_me() const override {
            return m_world_rank;
        }

        task_packet fetch_solution() override {
            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_best_results[rank].get_reference_value() == m_ref_value_global) {
                    return m_best_results[rank].get_task_packet();
                }
            }
            return task_packet::EMPTY; // no solution found
        }

        std::vector<result> fetch_result_vector() override {
            return m_best_results;
        }


        void print_stats() override {
            spdlog::debug("\n \n \n");
            spdlog::debug("*****************************************************\n");
            spdlog::debug("Elapsed time : {:4.3f} \n", elapsed_time());
            spdlog::debug("Total number of requests : {} \n", m_total_requests);
            spdlog::debug("*****************************************************\n");
            spdlog::debug("\n \n \n");
        }

        [[nodiscard]] size_t get_total_requests() const override {
            return m_total_requests;
        }

        void set_custom_initial_topology(tree &&p_tree) override {
            m_process_tree = std::move(p_tree);
            m_custom_initial_topology = true;
        }

        [[nodiscard]] double elapsed_time() const override {
            return (m_end_time - m_start_time) - static_cast<double>(TIMEOUT_TIME);
        }

        [[nodiscard]] int next_process() const override {
            return 0;
        }

        void allgather(void *p_recvbuf, void *p_sendbuf, MPI_Datatype p_mpi_datatype) override {
            MPI_Allgather(p_sendbuf, 1, p_mpi_datatype, p_recvbuf, 1, p_mpi_datatype, m_world_comm);
            MPI_Barrier(m_world_comm);
        }

        void gather(void *p_sendbuf, int p_sendcount, MPI_Datatype p_sendtype, void *p_recvbuf, int p_recvcount, MPI_Datatype p_recvtype, int p_root) override {
            MPI_Gather(p_sendbuf, p_sendcount, p_sendtype, p_recvbuf, p_recvcount, p_recvtype, p_root, m_world_comm);
        }

        [[nodiscard]] int get_world_size() const override {
            return m_world_size;
        }

        [[nodiscard]] int tasks_recvd() const override {
            return m_number_tasks_received;
        }

        [[nodiscard]] int tasks_sent() const override {
            return m_number_tasks_sent;
        }

        void barrier() override {
            if (m_world_comm != MPI_COMM_NULL)
                MPI_Barrier(m_world_comm);
        }

        bool open_sending_channel() override {
            if (m_mutex.try_lock()) // acquires mutex
            {
                if (!m_transmitting.load()) // check if transmission in progress
                {
                    if (!m_is_center_full) // check if center is actually waiting for a task
                    {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                m_mutex.unlock();
            }
            return false;
        }

        /* this should be invoked only if channel is open*/
        void close_sending_channel() override {
            m_mutex.unlock();
        }

        void set_goal(goal p_goal) override {
            this->m_goal = p_goal;

            if (p_goal == MINIMISE) // minimisation
                m_ref_value_global = INT_MAX;
        }


        void run_node(branch_handler &p_branch_handler, std::function<std::shared_ptr<ResultHolderParent>(task_packet)> &p_buffer_decoder,
                      std::function<result()> &p_result_fetcher) override {
            MPI_Barrier(m_world_comm);

            while (true) {
                MPI_Status status;
                int count; // count to be received
                int flag = 0;

                while (!flag) // this allows  to receive refValue or nextProcess even if this process has turned into waiting mode
                {
                    if (probe_reference_value()) // different communicator
                        continue; // center might update this value even if this process is idle

                    if (probe_center_request()) // different communicator
                        continue; // center might update this value even if this process is idle

                    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_comm, &flag, &status); // for regular messages
                    if (flag)
                        break;
                }
                MPI_Get_count(&status, MPI_BYTE, &count); // receives total number of datatype elements of the message

                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("rank {}, received message from rank {}, tag {}, count : {}\n", m_world_rank, status.MPI_SOURCE, status.MPI_TAG, count);
                #endif
                task_packet v_task_packet(count);
                MPI_Recv(v_task_packet.data(), count, MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_comm, &status);

                if (is_terminated(status.MPI_TAG)) {
                    break;
                }

                if (status.MPI_TAG == TASK_FROM_CENTER_TAG) {

                    notify_running_state();
                    m_number_tasks_received++;

                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("rank {}, pushing buffer to thread pool", m_world_rank, status.MPI_SOURCE);
                    #endif
                    //  push to the thread pool *********************************************************************
                    std::shared_ptr<ResultHolderParent> v_holder = p_buffer_decoder(v_task_packet); // holder might be useful for non-void functions
                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("... DONE\n", m_world_rank, status.MPI_SOURCE);
                    #endif
                    // **********************************************************************************************

                    task_funneling(p_branch_handler);
                    notify_available_state();

                    // TODO: refVal
                }
            }
            /**
             * TODO.. send results to the rank the task was sent from
             * this applies only when parallelising non-void functions
             */

            send_solution(p_result_fetcher);
        }

        /* enqueue a message which will be sent to the center
         */
        void push(task_packet &&p_task_packet) override {
            if (p_task_packet.empty()) {
                throw std::runtime_error(fmt::format("rank {}, attempted to send empty buffer \n", m_world_rank));
            }

            m_transmitting = true;

            const auto v_pck = std::make_shared<task_packet>(std::forward<task_packet &&>(p_task_packet));
            const auto v_message = new task_packet(*v_pck);

            if (!m_task_queue.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            m_task_queue.push(v_message);

            close_sending_channel();
        }

    private:
        // when a node is working, it loops through here
        void task_funneling(branch_handler &p_branch_handler);

        // checks for a ref value update from center
        int probe_reference_value() {
            int flag = 0;
            MPI_Status status;
            MPI_Iprobe(CENTER, REFVAL_UPDATE_TAG, m_ref_value_global_communicator, &flag, &status);

            if (flag) {
                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("rank {}, about to receive refValue from Center\n", m_world_rank);
                #endif

                MPI_Recv(&m_ref_value_global, 1, MPI_INT, CENTER, REFVAL_UPDATE_TAG, m_ref_value_global_communicator, &status);

                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("rank {}, received refValue: {} from Center\n", m_world_rank, m_ref_value_global);
                #endif
            }

            return flag;
        }

        // checks for a new assigned process from center
        bool probe_center_request() {
            int flag1 = 0;
            int flag2 = 0;
            MPI_Status status;
            MPI_Iprobe(CENTER, CENTER_IS_FULL_TAG, m_center_fullness_communicator, &flag1, &status);

            if (flag1) {
                task_packet v_buf(1); // buffer to receive the message
                MPI_Recv(v_buf.data(), 1, MPI_BYTE, CENTER, CENTER_IS_FULL_TAG, m_center_fullness_communicator, &status);
                m_is_center_full = true;
                #if GEMPBA_DEBUG_COMMENTS
                std::cout << "Node " << rank_me() << " received full center" << std::endl;
                #endif
            }

            MPI_Iprobe(CENTER, CENTER_IS_FREE_TAG, m_center_fullness_communicator, &flag2, &status);

            if (flag2) {
                task_packet v_buf(1); // buffer to receive the message
                MPI_Recv(v_buf.data(), 1, MPI_BYTE, CENTER, CENTER_IS_FREE_TAG, m_center_fullness_communicator, &status);
                m_is_center_full = false;
                #if GEMPBA_DEBUG_COMMENTS
                std::cout << "Node " << rank_me() << " received free center" << std::endl;
                #endif
            }

            return (flag1 || flag2);
        }

        // if ref value received, it attempts updating local value
        // if local value is better than the one in center, then local best value is sent to center
        void update_ref_value(branch_handler &p_branch_handler);

        bool is_terminated(int TAG) {
            if (TAG == TERMINATION_TAG) {
                spdlog::debug("rank {} exited\n", m_world_rank);
                MPI_Barrier(m_world_comm);
                return true;
            }
            return false;
        }

        void notify_available_state() {
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {} entered notifyAvailableState()\n", m_world_rank);
            #endif

            constexpr int v_buffer = 0;
            MPI_Send(&v_buffer, 1, MPI_INT, CENTER, STATE_AVAILABLE, m_world_comm);
        }

        void notify_running_state() {
            int v_buffer = 0;
            MPI_Send(&v_buffer, 1, MPI_INT, CENTER, STATE_RUNNING, m_world_comm);
        }

        void send_task_to_center(task_packet &p_task_packet) {
            MPI_Send(p_task_packet.data(), static_cast<int>(p_task_packet.size()), MPI_BYTE, CENTER, TASK_FOR_CENTER, m_world_comm);
        }

    public:
    private:
        /*	send solution attained from node to the center node */
        void send_solution(const std::function<result()> &p_result_fetcher) {
            const result v_result = p_result_fetcher();
            const int v_ref_val = v_result.get_reference_value();
            task_packet v_task_packet = v_result.get_task_packet();

            if (v_result == result::EMPTY) {
                MPI_Send(nullptr, 0, MPI_BYTE, CENTER, NO_RESULT_TAG, m_world_comm);
            } else {
                MPI_Send(v_task_packet.data(), static_cast<int>(v_task_packet.size()), MPI_BYTE, CENTER, HAS_RESULT_TAG, m_world_comm);
                MPI_Send(&v_ref_val, 1, MPI_INT, 0, HAS_RESULT_TAG, m_world_comm);
            }
        }

        /* it returns the substraction between end and start*/
        static double difftime(const double p_start, const double p_end) {
            return p_end - p_start;
        }

    public:
        void clear_buffer() {
            if (m_center_queue.empty())
                return;

            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_process_state[rank] == STATE_AVAILABLE) {
                    //pair<char *, size_t> msg = center_queue.back();
                    //center_queue.pop_back();
                    task_packet v_msg = m_center_queue.top();
                    m_center_queue.pop();

                    MPI_Send(v_msg.data(), static_cast<int>(v_msg.size()), MPI_BYTE, rank, TASK_FROM_CENTER_TAG, m_world_comm);
                    m_process_state[rank] = STATE_ASSIGNED;

                    if (m_center_queue.empty())
                        return;
                }
            }
        }

        void handle_full_messaging() {
            const size_t v_current_memory = getCurrentRSS() / (1024 * 1024); // ram usage in megabytes


            if (!m_center_last_full_status) {
                // last iter, center wasn't full but now it is => warn nodes to stop sending
                if (v_current_memory > MAX_MEMORY_MB || m_center_queue.size() > CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size) {
                    for (int rank = 1; rank < m_world_size; rank++) {
                        std::byte v_tmp{0};
                        MPI_Send(&v_tmp, 1, MPI_BYTE, rank, CENTER_IS_FULL_TAG, m_center_fullness_communicator);
                    }
                    m_center_last_full_status = true;
                    m_time_centerfull_sent = MPI_Wtime();

                    //cout << "CENTER IS FULL" << endl;
                }
            } else {
                // last iter, center was full but now it has space => warn others it's ok
                if (v_current_memory <= 0.9 * MAX_MEMORY_MB && m_center_queue.size() < CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size * 0.8) {
                    for (int rank = 1; rank < m_world_size; rank++) {
                        std::byte v_tmp{0};
                        MPI_Send(&v_tmp, 1, MPI_BYTE, rank, CENTER_IS_FREE_TAG, m_center_fullness_communicator);
                    }
                    m_center_last_full_status = false;

                    //cout << "CENTER IS NOT FULL ANYMORE" << endl;
                }
            }
        }

        /*	run the center node */
        void run_center(task_packet &p_seed) override {
            task_packet v_task_packet = p_seed;
            std::cout << "Starting centralized scheduler" << std::endl;
            MPI_Barrier(m_world_comm);
            m_start_time = MPI_Wtime();

            send_seed(v_task_packet);

            int v_number_loops = 0;
            while (true) {
                v_number_loops++;
                int v_buffer;
                int v_buffer_char_count = 0;
                task_packet *v_buffer_packet = nullptr;
                MPI_Status status;
                MPI_Request request;
                int ready;

                int flag;
                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_comm, &flag, &status);

                /*if (nbloops % 100 == 0)
                {
                    cout<<"CENTER nb loops = "<<nbloops<<" nrunning="<<nRunning<<endl;
                    cout<<"STATES=";
                    for (int i = 1; i < world_size;++i)
                    {
                        cout<<processState[i]<<"  ";
                    }
                    cout<<endl;
                    cout<<"WARNED=";
                    for (int i = 1; i < world_size;++i)
                    {
                        cout<<processes_center_asked[i]<<"  ";
                    }
                    cout<<endl;
                }*/

                if (!flag) {
                    const double v_begin = MPI_Wtime();

                    while (!flag && (difftime(v_begin, MPI_Wtime()) < TIMEOUT_TIME)) {
                        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_comm, &flag, &status);

                        if (!flag) {
                            clear_buffer();
                            handle_full_messaging();
                        }
                    }

                    if (!flag) {
                        if (m_nodes_running == 0) {
                            break; // Cancellation due to TIMEOUT
                        }
                    }
                }

                if (!flag) {
                    clear_buffer();
                    handle_full_messaging();
                    continue;
                }

                // at this point, probe succeeded => there is something to receive
                if (status.MPI_TAG == TASK_FOR_CENTER) {

                    MPI_Get_count(&status, MPI_BYTE, &v_buffer_char_count);
                    v_buffer_packet = new task_packet(v_buffer_char_count);
                    MPI_Recv(v_buffer_packet->data(), v_buffer_char_count, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG, m_world_comm, &status);
                } else {
                    MPI_Recv(&v_buffer, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, m_world_comm, &status);
                }

                switch (status.MPI_TAG) {
                    case STATE_RUNNING: // received if and only if a worker receives from other but center
                    {
                        m_process_state[status.MPI_SOURCE] = STATE_RUNNING; // node was assigned, now it's running
                        ++m_nodes_running;

                        ++m_total_requests;
                    }
                    break;
                    case STATE_AVAILABLE: {
                        #ifdef GEMPBA_DEBUG_COMMENTS
                        spdlog::debug("center received state_available from rank {}\n", status.MPI_SOURCE);
                        #endif
                        m_process_state[status.MPI_SOURCE] = STATE_AVAILABLE;
                        ++m_nodes_available;
                        --m_nodes_running;
                        ++m_total_requests;
                    }
                    break;
                    case REFVAL_UPDATE_TAG: {
                        /* if center reaches this point, for sure nodes have attained a better reference value
                                or they are not up-to-date, thus it is required to broadcast it whether this value
                                changes or not  */
                        #ifdef GEMPBA_DEBUG_COMMENTS
                        spdlog::debug("center received refValue {} from rank {}\n", v_buffer, status.MPI_SOURCE);
                        #endif
                        bool signal = false;

                        if ((m_goal == MAXIMISE && v_buffer > m_ref_value_global) || (m_goal == MINIMISE && v_buffer < m_ref_value_global)) {
                            // refValueGlobal[0] = buffer;
                            m_ref_value_global = v_buffer;
                            signal = true;
                            for (int rank = 1; rank < m_world_size; rank++) {
                                MPI_Send(&m_ref_value_global, 1, MPI_INT, rank, REFVAL_UPDATE_TAG, m_ref_value_global_communicator);
                            }

                            // bcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal);
                        }

                        if (signal) {
                            static int success = 0;
                            success++;
                            spdlog::debug("refValueGlobal updated to : {} by rank {}\n", m_ref_value_global,
                                          status.MPI_SOURCE);
                        } else {
                            static int failures = 0;
                            failures++;
                            spdlog::debug("FAILED updates : {}, refValueGlobal : {} by rank {}\n", failures,
                                          m_ref_value_global, status.MPI_SOURCE);
                        }
                        ++m_total_requests;
                    }
                    break;
                    case TASK_FOR_CENTER: {

                        if (v_buffer_packet == nullptr) {
                            throw std::runtime_error("v_buffer_packet is nullptr, this should not happen");
                        }

                        task_packet msg{*v_buffer_packet}; //copy
                        //center_queue.push_back(msg);
                        m_center_queue.push(msg);
                        delete v_buffer_packet; // free memory

                        if (m_center_queue.size() > m_max_queue_size) {
                            if (m_center_queue.size() % 10000 == 0)
                                std::cout << "CENTER queue size reached " << m_center_queue.size() << std::endl;
                            m_max_queue_size = m_center_queue.size();
                        }

                        ++m_total_requests;


                        if (m_center_queue.size() > 2 * CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size) {
                            if (difftime(m_time_centerfull_sent, MPI_Wtime() > 1)) {
                                spdlog::debug(
                                        "Center queue size is twice the limit.  Contacting workers to let them know.\n");
                                m_center_last_full_status = false; //handleFullMessaging will see this and reontact workers
                            }
                        }

                        #ifdef GEMPBA_DEBUG_COMMENTS
                        spdlog::debug("center received task from {}, current queue size is {}\n", status.MPI_SOURCE, m_center_queue.size());
                        #endif
                    }
                    break;
                }

                clear_buffer();
                handle_full_messaging();
            }

            std::cout << "CENTER HAS TERMINATED" << std::endl;
            std::cout << "Max queue size = " << m_max_queue_size << ",   Peak memory (MB) = " << getPeakRSS() / (1024 * 1024)
                    << std::endl;

            /*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
            notify_termination();

            // receive solution from other processes
            receive_solution();

            m_end_time = MPI_Wtime();
        }

    private:
        /* return false if message not received, which is signal of termination
            all workers report (with a message) to center process when about to run a task or when becoming available
            if no message is received within a TIMEOUT window, then all processes will have finished
        */
        bool await_message(int p_buffer, int &p_ready, double p_begin, MPI_Status &p_status, MPI_Request &p_request) const {
            int cycles = 0;
            while (true) {
                MPI_Test(&p_request, &p_ready, &p_status);
                // Check whether the underlying communication had already taken place
                while (!p_ready && (difftime(p_begin, MPI_Wtime()) < TIMEOUT_TIME)) {
                    MPI_Test(&p_request, &p_ready, &p_status);
                    cycles++;
                }

                if (!p_ready) {
                    if (m_nodes_running == 0) {
                        // Cancellation due to TIMEOUT
                        MPI_Cancel(&p_request);
                        MPI_Request_free(&p_request);
                        printf("rank %d: receiving TIMEOUT, buffer : %d, cycles : %d\n", m_world_rank, p_buffer, cycles);
                        return false;
                    }
                } else
                    return true;
            }
        }

        void notify_termination() {
            for (int rank = 1; rank < m_world_size; rank++) {
                char v_buffer[] = "exit signal";
                int v_count = sizeof(v_buffer);
                task_packet v_task_packet(v_buffer, v_count);
                MPI_Send(v_task_packet.data(), v_count, MPI_BYTE, rank, TERMINATION_TAG, m_world_comm); // send positive signal
            }
            MPI_Barrier(m_world_comm);
        }

        int get_available() {
            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_process_state[rank] == STATE_AVAILABLE)
                    return rank;
            }
            return -1; // all nodes are running
        }

        /*	receive solution from nodes */
        void receive_solution() {
            for (int rank = 1; rank < m_world_size; rank++) {

                MPI_Status status;
                int count;
                // sender would not need to send data size before hand **********************************************
                MPI_Probe(rank, MPI_ANY_TAG, m_world_comm, &status); // receives status before receiving the message
                MPI_Get_count(&status, MPI_BYTE, &count); // receives total number of datatype elements of the message
                //***************************************************************************************************

                task_packet v_task_packet(count);
                MPI_Recv(v_task_packet.data(), count, MPI_BYTE, rank, MPI_ANY_TAG, m_world_comm, &status);

                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("fetching result from rank {} \n", rank);
                #endif

                switch (status.MPI_TAG) {
                    case HAS_RESULT_TAG: {

                        int v_reference_value;
                        MPI_Recv(&v_reference_value, 1, MPI_INT, rank, HAS_RESULT_TAG, m_world_comm, &status);

                        m_best_results[rank] = result{v_reference_value, v_task_packet};

                        spdlog::debug("solution received from rank {}, count : {}, refVal {} \n", rank, count, v_reference_value);
                    }
                    break;

                    case NO_RESULT_TAG: {
                        spdlog::debug("solution NOT received from rank {}\n", rank);
                    }
                    break;
                }
            }
        }

        void send_seed(task_packet &p_packet) {
            constexpr int v_dest = 1;
            // global synchronisation **********************
            --m_nodes_available;
            m_process_state[v_dest] = STATE_RUNNING;
            // *********************************************

            int err = MPI_Ssend(p_packet.data(), static_cast<int>(p_packet.size()), MPI_BYTE, v_dest, TASK_FROM_CENTER_TAG, m_world_comm); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
        }

        void create_communicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &m_world_comm); // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD, &m_ref_value_global_communicator); // exclusive communicator for reference value - one-sided comm

            MPI_Comm_dup(MPI_COMM_WORLD, &m_center_fullness_communicator);

            MPI_Comm_size(m_world_comm, &this->m_world_size);
            MPI_Comm_rank(m_world_comm, &this->m_world_rank);

            /*if (world_size < 2)
            {
                spdlog::debug("At least two processes required !!\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }*/
        }

        void allocate_mpi() {
            MPI_Barrier(m_world_comm);
            init();
            MPI_Barrier(m_world_comm);
        }

        void deallocate_mpi() {
            MPI_Comm_free(&m_ref_value_global_communicator);
            MPI_Comm_free(&m_center_fullness_communicator);

            MPI_Comm_free(&m_world_comm);
        }

        void init() {
            m_process_state.resize(m_world_size, STATE_AVAILABLE);
            m_process_tree.resize(m_world_size);
            m_max_queue_size = 0;

            m_ref_value_global = INT_MIN;

            if (m_world_rank == 0)
                m_best_results.resize(m_world_size, result::EMPTY);

            m_transmitting = false;
        }

    private:
        int m_argc;
        char **m_argv;
        int m_world_rank; // get the rank of the process
        int m_world_size; // get the number of processes/nodes
        char m_processor_name[128]; // name of the node

        int m_number_tasks_received = 0;
        int m_number_tasks_sent = 0;
        int m_nodes_running = 0;
        int m_nodes_available = 0;
        std::vector<int> m_process_state; // state of the nodes : running, assigned or available
        bool m_custom_initial_topology = false; // true if the user has set a custom topology
        tree m_process_tree;

        std::mutex m_mutex;
        std::atomic<bool> m_transmitting;
        int m_dest_rank_tmp = -1;

        Queue<task_packet *> m_task_queue;
        bool m_exit = false;

        // MPI_Group world_group;		  // all ranks belong to this group
        MPI_Comm m_ref_value_global_communicator; // attached to win_refValueGlobal
        MPI_Comm m_center_fullness_communicator;

        MPI_Comm m_world_comm; // world communicator


        int m_ref_value_global;

        bool m_is_center_full = false;

        goal m_goal = MAXIMISE;

        std::vector<result> m_best_results;

        size_t m_threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        // statistics
        size_t m_total_requests = 0;
        double m_start_time = 0;
        double m_end_time = 0;

        /* singleton*/
        mpi_centralized_scheduler() {
            init(NULL, NULL);
        }

        void init(int *p_argc, char *p_argv[]) {
            // Initialise MPI and ask for thread support
            int v_provided;
            MPI_Init_thread(p_argc, &p_argv, MPI_THREAD_FUNNELED, &v_provided);

            if (v_provided < MPI_THREAD_FUNNELED) {
                spdlog::debug("The threading support level is lesser than that demanded.\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }

            create_communicators();

            int v_namelen;
            MPI_Get_processor_name(m_processor_name, &v_namelen);
            spdlog::debug("Process {} of {} is on {}\n", m_world_rank, m_world_size, m_processor_name);
            allocate_mpi();
        }

        void finalize() {
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {}, before deallocate \n", m_world_rank);
            #endif
            deallocate_mpi();
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {}, after deallocate \n", m_world_rank);
            #endif
            MPI_Finalize();
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {}, after MPI_Finalize() \n", m_world_rank);
            #endif
        }

        /* ---------------------------------------------------------------------------------
         * utility functions to determine whether to update global or local reference values
         * ---------------------------------------------------------------------------------*/

        static bool should_update_global(const goal p_goal, const int p_global_reference_value, const int p_local_reference_value) {
            const bool local_max_is_better = p_goal == MAXIMISE && p_local_reference_value > p_global_reference_value;
            const bool local_min_is_better = p_goal == MINIMISE && p_local_reference_value < p_global_reference_value;
            return local_max_is_better || local_min_is_better;
        }

        static bool should_update_local(const goal p_goal, const int p_global_reference_value, const int p_local_reference_value) {
            const bool global_max_is_better = p_goal == MAXIMISE && p_global_reference_value > p_local_reference_value;
            const bool global_min_is_better = p_goal == MINIMISE && p_global_reference_value < p_local_reference_value;
            return global_max_is_better || global_min_is_better;
        }
    };

}

#endif
