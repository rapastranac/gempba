/*
 * MIT License
 *
 * Copyright (c) 2026. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef GEMPBA_TERMINATION_DETECTOR_HPP
#define GEMPBA_TERMINATION_DETECTOR_HPP

#include <optional>
#include <utility>

/**
 * @author Andres Pastrana
 * @date 2026-11-01
 */

namespace gempba {

    /**
     * @brief Dijkstra–Scholten termination detection for one rank.
     *
     * A task received while disengaged makes its sender this rank's parent, and that acknowledgement is deferred
     * until the rank is passive and every task it sent has been acknowledged. Any other task is acknowledged on
     * receipt. The root never takes a parent, so the whole computation has terminated exactly when the root is
     * passive and its deficit is zero.
     */
    class termination_detector final {
    public:
        static constexpr int NO_PARENT = -1;

        explicit termination_detector(const bool p_is_root) : m_is_root(p_is_root) {}

        void on_task_sent() { ++m_deficit; }

        /**
         * @return the rank to acknowledge now, or std::nullopt when the acknowledgement is deferred to on_passive()
         */
        [[nodiscard]] std::optional<int> on_task_received(const int p_sender) {
            if (m_is_root || m_parent != NO_PARENT) {
                return p_sender;
            }
            m_parent = p_sender;
            return std::nullopt;
        }

        /**
         * @return false when no sent task is awaiting an acknowledgement
         */
        [[nodiscard]] bool on_ack_received() {
            if (m_deficit == 0) {
                return false;
            }
            --m_deficit;
            return true;
        }

        /**
         * @brief Call whenever the rank has no local work left.
         * @return the parent to acknowledge now, which disengages this rank, or std::nullopt
         */
        [[nodiscard]] std::optional<int> on_passive() {
            if (m_deficit != 0 || m_parent == NO_PARENT) {
                return std::nullopt;
            }
            return std::exchange(m_parent, NO_PARENT);
        }

        [[nodiscard]] int deficit() const { return m_deficit; }

        [[nodiscard]] int parent() const { return m_parent; }

        [[nodiscard]] bool is_quiescent() const { return m_deficit == 0 && m_parent == NO_PARENT; }

    private:
        bool m_is_root;
        int m_deficit = 0;
        int m_parent = NO_PARENT;
    };

} // namespace gempba

#endif // GEMPBA_TERMINATION_DETECTOR_HPP
