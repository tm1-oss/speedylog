// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/async_logger.h>
#endif

#include <spdlog/logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/sink.h>

#include <memory>
#include <string>

template <class Alloc>
SPDLOG_INLINE spdlog::basic_async_logger<Alloc>::basic_async_logger(
    string_type logger_name,
    sinks_init_list<Alloc> sinks_list,
    std::weak_ptr<details::basic_thread_pool<Alloc>> tp,
    async_overflow_policy overflow_policy)
    : basic_async_logger(std::move(logger_name),
                         sinks_list.begin(),
                         sinks_list.end(),
                         std::move(tp),
                         overflow_policy) {}

template <class Alloc>
SPDLOG_INLINE spdlog::basic_async_logger<Alloc>::basic_async_logger(
    string_type logger_name,
    sink_ptr<Alloc> single_sink,
    std::weak_ptr<details::basic_thread_pool<Alloc>> tp,
    async_overflow_policy overflow_policy)
    : basic_async_logger(
          std::move(logger_name), {std::move(single_sink)}, std::move(tp), overflow_policy) {}

// send the log message to the thread pool
template <class Alloc>
SPDLOG_INLINE void spdlog::basic_async_logger<Alloc>::sink_it_(const details::log_msg &msg) {
    SPDLOG_TRY {
        if (auto pool_ptr = thread_pool_.lock()) {
            pool_ptr->post_log(this->shared_from_this(), msg, overflow_policy_);
        } else {
            throw_spdlog_ex("async log: thread pool doesn't exist anymore");
        }
    }
    SPDLOG_LOGGER_CATCH(msg.source)
}

// send flush request to the thread pool
template <class Alloc>
SPDLOG_INLINE void spdlog::basic_async_logger<Alloc>::flush_() {
    SPDLOG_TRY {
        if (auto pool_ptr = thread_pool_.lock()) {
            pool_ptr->post_flush(this->shared_from_this(), overflow_policy_);
        } else {
            throw_spdlog_ex("async flush: thread pool doesn't exist anymore");
        }
    }
    SPDLOG_LOGGER_CATCH(source_loc())
}

//
// backend functions - called from the thread pool to do the actual job
//
template <class Alloc>
SPDLOG_INLINE void spdlog::basic_async_logger<Alloc>::backend_sink_it_(
    const details::log_msg &msg) {
    for (auto &sink : this->sinks_) {
        if (sink->should_log(msg.level)) {
            SPDLOG_TRY { sink->log(msg); }
            SPDLOG_LOGGER_CATCH(msg.source)
        }
    }

    if (this->should_flush_(msg)) {
        backend_flush_();
    }
}

template <class Alloc>
SPDLOG_INLINE void spdlog::basic_async_logger<Alloc>::backend_flush_() {
    for (auto &sink : this->sinks_) {
        SPDLOG_TRY { sink->flush(); }
        SPDLOG_LOGGER_CATCH(source_loc())
    }
}

template <class Alloc>
SPDLOG_INLINE std::shared_ptr<spdlog::basic_logger<Alloc>> spdlog::basic_async_logger<Alloc>::clone(
    string_type new_name) {
    auto cloned = std::make_shared<spdlog::basic_async_logger<Alloc>>(*this);
    cloned->name_ = std::move(new_name);
    return cloned;
}
