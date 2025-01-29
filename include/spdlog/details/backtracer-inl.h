// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/details/backtracer.h>
#endif
namespace spdlog {
namespace details {
template <class Alloc>
SPDLOG_INLINE backtracer<Alloc>::backtracer(const backtracer &other) {
    std::lock_guard<std::mutex> lock(other.mutex_);
    enabled_ = other.enabled();
    messages_ = other.messages_;
}

template <class Alloc>
SPDLOG_INLINE backtracer<Alloc>::backtracer(backtracer &&other) SPDLOG_NOEXCEPT {
    std::lock_guard<std::mutex> lock(other.mutex_);
    enabled_ = other.enabled();
    messages_ = std::move(other.messages_);
}

template <class Alloc>
SPDLOG_INLINE backtracer<Alloc> &backtracer<Alloc>::operator=(backtracer other) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = other.enabled();
    messages_ = std::move(other.messages_);
    return *this;
}

template <class Alloc>
SPDLOG_INLINE void backtracer<Alloc>::enable(size_t size) {
    std::lock_guard<std::mutex> lock{mutex_};
    enabled_.store(true, std::memory_order_relaxed);
    messages_ = circular_q<log_msg_buffer<Alloc>>{size};
}

template <class Alloc>
SPDLOG_INLINE void backtracer<Alloc>::disable() {
    std::lock_guard<std::mutex> lock{mutex_};
    enabled_.store(false, std::memory_order_relaxed);
}

template <class Alloc>
SPDLOG_INLINE bool backtracer<Alloc>::enabled() const {
    return enabled_.load(std::memory_order_relaxed);
}

template <class Alloc>
SPDLOG_INLINE void backtracer<Alloc>::push_back(const log_msg &msg) {
    std::lock_guard<std::mutex> lock{mutex_};
    messages_.push_back(log_msg_buffer<Alloc>{msg});
}

template <class Alloc>
SPDLOG_INLINE bool backtracer<Alloc>::empty() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return messages_.empty();
}

// pop all items in the q and apply the given fun on each of them.
template <class Alloc>
SPDLOG_INLINE void backtracer<Alloc>::foreach_pop(
    std::function<void(const details::log_msg &)> fun) {
    std::lock_guard<std::mutex> lock{mutex_};
    while (!messages_.empty()) {
        auto &front_msg = messages_.front();
        fun(front_msg);
        messages_.pop_front();
    }
}
}  // namespace details
}  // namespace spdlog
