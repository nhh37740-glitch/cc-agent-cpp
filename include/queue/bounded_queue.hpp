#pragma once

// 有界线程安全队列。
// - 容量固定；满时执行 drop_oldest（淘汰最旧元素，保留最新）；
// - pop 支持 std::stop_token：请求停止时立即唤醒并返回 false；
// - close() 之后不再接受 push；pop 把剩余元素排空后返回 false。
// 计数器：dropped_full（因队满被淘汰）、received（成功交付）。

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace queue {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("BoundedQueue capacity must be greater than zero");
        }
    }

    // 非阻塞入队。队列已关闭则丢弃；满则淘汰最旧。返回是否真正入队。
    bool push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;
        if (items_.size() >= capacity_) {
            items_.pop_front();
            ++dropped_full_;
        }
        items_.push_back(std::move(item));
        ++pushed_;
        cv_.notify_one();
        return true;
    }

    // 阻塞出队。st 缺省为空 token（永不停止，仅受 close 控制）。
    // 返回 false 的条件：请求了 stop，或已 close 且排空。
    bool pop(T& out, std::stop_token st = std::stop_token()) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, st, [&] { return !items_.empty() || closed_; });
        if (st.stop_requested()) return false;
        if (items_.empty()) return false;  // closed 且排空
        out = std::move(items_.front());
        items_.pop_front();
        ++received_;
        return true;
    }

    // 关闭入口；已在等待的消费者将排空剩余元素后得到 false。
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    std::size_t capacity() const { return capacity_; }
    uint64_t dropped_full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_full_;
    }
    uint64_t pushed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pushed_;
    }
    uint64_t received() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<T> items_;
    std::size_t capacity_;
    bool closed_ = false;
    uint64_t dropped_full_ = 0;
    uint64_t pushed_ = 0;
    uint64_t received_ = 0;
};

}  // namespace queue
