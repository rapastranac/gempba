/*********************************************************
 *
 *  Copyright (C) 2014 by Vitaliy Vitsentiy
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *********************************************************/

#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <mutex>
#include <queue>

/*
    from https://github.com/vit-vit/CTPL

    thread safe queue, it stores a pointer of data such that copying data is avoided
    Thus, to enqueue tasks, a task is allocated in heap; then a pointer of it is passed
    to this queue to be retrieved later in the order it is spawned.
 */

template <class T>
class Queue {
public:
    bool push(T const& p_value) {
        std::unique_lock<std::mutex> lock(this->m_mtx);
        this->m_q.push(p_value);
        return true;
    }

    // deletes the retrieved element, do not use for non-integral types
    bool pop(T& p_v) {
        std::unique_lock<std::mutex> lock(this->m_mtx);
        if (this->m_q.empty())
            return false;
        p_v = this->m_q.front();
        this->m_q.pop();
        return true;
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(this->m_mtx);
        return this->m_q.empty();
    }

private:
    std::queue<T> m_q;
    std::mutex m_mtx;
};

#endif
