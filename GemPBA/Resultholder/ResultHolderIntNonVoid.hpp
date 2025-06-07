#ifndef NONVOIDINTERMEDIATE_HPP
#define NONVOIDINTERMEDIATE_HPP

/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */

#include "ResultHolderBase.hpp"

namespace gempba {

    template <typename Ret, typename... Args>
    class ResultHolderInt<Ret, typename std::enable_if<!std::is_void<Ret>::value>::type, Args...> : virtual public ResultHolderBase<Args...> {
        friend class DLB_Handler;

    protected:
        std::future<Ret> expectedFut;
        Ret expected;

    public:
        explicit ResultHolderInt(DLB_Handler& dlb) :
            ResultHolderBase<Args...>(dlb) {
        }

        ~ResultHolderInt() override = default;

        void hold_future(std::future<Ret>&& expectedFut) {
            this->expectedFut = std::move(expectedFut);
        }

        void hold_actual_result(Ret& expected) {
            this->expected = std::move(expected);
        }

        Ret get() {
            if (this->isPushed) {
                auto begin = std::chrono::steady_clock::now();
                this->expected = expectedFut.get();
                auto end = std::chrono::steady_clock::now();
                this->dlb.add_on_idle_time(begin, end);
            }

            this->isRetrieved = true;
            return this->expected; // returns empty object of type _Ret,
        }

        #ifdef MULTIPROCESSING_ENABLED

        // in construction
        template<typename F_deser>
        Ret get(F_deser &&f_deser) {
            if (this->isPushed) {
                std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
                this->expected = this->expectedFut.get();
                std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
                /*If a thread comes in this scope, then it is clear that numThread
                            must be decremented in one, also it should be locked before another thread
                            changes it, since it is atomic, this operation is already well-defined*/

                this->dlb.add_on_idle_time(begin, end);
            } else if (this->isMPISent) {
                this->branchHandler.lock_mpi(); /* this blocks any other thread to use an MPI function since MPI_Recv is blocking
                                                        thus, mpi_thread_serialized is guaranteed */

                // printf("rank %d entered get() to retrieve from %d! \n", branchHandler.world_rank, dest_rank);
                utils::print_mpi_debug_comments("rank {} entered get() to retrieve from {}! \n", this->branchHandler.world_rank, this->dest_rank);

                MPI_Status status;
                int Bytes;

                MPI_Probe(this->dest_rank, MPI_ANY_TAG, this->branchHandler.getCommunicator(), &status); // receives status before receiving the message
                MPI_Get_count(&status, MPI_CHAR, &Bytes);                                                // receives total number of datatype elements of the message

                char *in_buffer = new char[Bytes];
                MPI_Recv(in_buffer, Bytes, MPI_CHAR, this->dest_rank, MPI_ANY_TAG,
                         this->branchHandler.getCommunicator(), &status);

                // printf("rank %d received %d Bytes from %d! \n", branchHandler.world_rank, Bytes, dest_rank);
                utils::print_mpi_debug_comments("rank {} received {} Bytes from {}! \n", this->branchHandler.world_rank, Bytes, this->dest_rank);

                std::stringstream ss;
                for (int i = 0; i < Bytes; i++)
                    ss << in_buffer[i];

                Ret temp;
                f_deser(ss, temp);
                delete[] in_buffer;

                this->isRetrieved = true;

                this->branchHandler.unlock_mpi(); /* release mpi mutex, thus, other threads are able to push to other nodes*/
                return temp;
            }
            /*	This condition is relevant due to some functions might return empty values
                        which are not stored in std::any types	*/
            this->isRetrieved = true;
            return expected; // returns empty object of type _Ret,
        }

        #endif
    };
}
#endif
