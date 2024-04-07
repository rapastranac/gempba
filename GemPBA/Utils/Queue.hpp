#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <queue>
#include <mutex>
#include <utility>

/*
    from https://github.com/vit-vit/CTPL

    thread safe queue, it stores a pointer of data such that copying data is avoided
    Thus, to enqueue tasks, task is allocated in heap, then a pointer of it is passed
    to this queue to be retrieved later in the order it is spawned.
 */

template<class T>
class Queue {
public:
    bool push(T const &value) {
        std::unique_lock<std::mutex> lock(this->mtx);
        this->q.push(value);
        return true;
    }

    // deletes the retrieved element, do not use for non integral types
    bool pop(T &v) {
        std::unique_lock<std::mutex> lock(this->mtx);
        if (this->q.empty())
            return false;
        v = this->q.front();
        this->q.pop();
        return true;
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->q.empty();
    }

private:
    std::queue<T> q;
    std::mutex mtx;
};

#endif