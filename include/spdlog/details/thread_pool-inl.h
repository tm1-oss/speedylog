// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/details/thread_pool.h>
#endif

#include <cassert>
#include <spdlog/common.h>

namespace spdlog {
namespace details {

template <class Alloc>
SPDLOG_INLINE async_msg<Alloc>::async_msg(Alloc alloc)
    : log_msg_buffer<Alloc>(alloc) {}

template <class Alloc>
SPDLOG_INLINE basic_thread_pool<Alloc>::basic_thread_pool(size_t q_max_items,
                                                          size_t threads_n,
                                                          std::function<void()> on_thread_start,
                                                          std::function<void()> on_thread_stop,
                                                          Alloc alloc)
    : Alloc(alloc),
      q_(q_max_items) {
    if (threads_n == 0 || threads_n > 1000) {
        throw_spdlog_ex(
            "spdlog::basic_thread_pool(): invalid threads_n param (valid "
            "range is 1-1000)");
    }
    for (size_t i = 0; i < threads_n; i++) {
        threads_.emplace_back([this, on_thread_start, on_thread_stop] {
            on_thread_start();
            this->basic_thread_pool::worker_loop_();
            on_thread_stop();
        });
    }
}

template <class Alloc>
SPDLOG_INLINE basic_thread_pool<Alloc>::basic_thread_pool(size_t q_max_items,
                                                          size_t threads_n,
                                                          std::function<void()> on_thread_start,
                                                          Alloc alloc)
    : basic_thread_pool(q_max_items, threads_n, on_thread_start, [] {}, alloc) {}

template <class Alloc>
SPDLOG_INLINE basic_thread_pool<Alloc>::basic_thread_pool(size_t q_max_items,
                                                          size_t threads_n,
                                                          Alloc alloc)
    : basic_thread_pool(q_max_items, threads_n, [] {}, alloc) {}

// message all threads to terminate gracefully join them
template <class Alloc>
SPDLOG_INLINE basic_thread_pool<Alloc>::~basic_thread_pool() {
    SPDLOG_TRY {
        for (size_t i = 0; i < threads_.size(); i++) {
            post_async_msg_(async_msg<Alloc>(async_msg_type::terminate),
                            async_overflow_policy::block);
        }

        for (auto &t : threads_) {
            t.join();
        }
    }
    SPDLOG_CATCH_STD
}

template <class Alloc>
void SPDLOG_INLINE basic_thread_pool<Alloc>::post_log(async_logger_ptr<Alloc> &&worker_ptr,
                                                      const details::log_msg &msg,
                                                      async_overflow_policy overflow_policy) {
    async_msg<Alloc> async_m(std::move(worker_ptr), async_msg_type::log, msg);
    post_async_msg_(std::move(async_m), overflow_policy);
}

template <class Alloc>
void SPDLOG_INLINE basic_thread_pool<Alloc>::post_flush(async_logger_ptr<Alloc> &&worker_ptr,
                                                        async_overflow_policy overflow_policy) {
    post_async_msg_(async_msg<Alloc>(std::move(worker_ptr), async_msg_type::flush),
                    overflow_policy);
}

template <class Alloc>
size_t SPDLOG_INLINE basic_thread_pool<Alloc>::overrun_counter() {
    return q_.overrun_counter();
}

template <class Alloc>
void SPDLOG_INLINE basic_thread_pool<Alloc>::reset_overrun_counter() {
    q_.reset_overrun_counter();
}

template <class Alloc>
size_t SPDLOG_INLINE basic_thread_pool<Alloc>::discard_counter() {
    return q_.discard_counter();
}

template <class Alloc>
void SPDLOG_INLINE basic_thread_pool<Alloc>::reset_discard_counter() {
    q_.reset_discard_counter();
}

template <class Alloc>
size_t SPDLOG_INLINE basic_thread_pool<Alloc>::queue_size() {
    return q_.size();
}

template <class Alloc>
void SPDLOG_INLINE basic_thread_pool<Alloc>::post_async_msg_(
    async_msg<Alloc> &&new_msg, async_overflow_policy overflow_policy) {
    if (overflow_policy == async_overflow_policy::block) {
        q_.enqueue(std::move(new_msg));
    } else if (overflow_policy == async_overflow_policy::overrun_oldest) {
        q_.enqueue_nowait(std::move(new_msg));
    } else {
        assert(overflow_policy == async_overflow_policy::discard_new);
        q_.enqueue_if_have_room(std::move(new_msg));
    }
}

template <class Alloc>
void SPDLOG_INLINE basic_thread_pool<Alloc>::worker_loop_() {
    while (process_next_msg_()) {
    }
}

// process next message in the queue
// returns true if this thread should still be active (while no terminated msg was received)
template <class Alloc>
bool SPDLOG_INLINE basic_thread_pool<Alloc>::process_next_msg_() {
    async_msg<Alloc> incoming_async_msg(*this);
    q_.dequeue(incoming_async_msg);

    switch (incoming_async_msg.msg_type) {
        case async_msg_type::log: {
            incoming_async_msg.worker_ptr->backend_sink_it_(incoming_async_msg);
            return true;
        }
        case async_msg_type::flush: {
            incoming_async_msg.worker_ptr->backend_flush_();
            return true;
        }

        case async_msg_type::terminate: {
            return false;
        }

        default: {
            assert(false);
        }
    }

    return true;
}

}  // namespace details
}  // namespace spdlog
