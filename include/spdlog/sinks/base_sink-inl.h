// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/sinks/base_sink.h>
#endif

#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>

#include <memory>
#include <mutex>

template <typename Mutex, class Alloc>
SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::base_sink()
    : formatter_{details::make_unique<spdlog::basic_pattern_formatter<Alloc>>()} {}

template <typename Mutex, class Alloc>
SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::base_sink(
    std::unique_ptr<spdlog::basic_formatter<Alloc>> formatter)
    : formatter_{std::move(formatter)} {}

template <typename Mutex, class Alloc>
void SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::log(const details::log_msg &msg) {
    std::lock_guard<Mutex> lock(mutex_);
    sink_it_(msg);
}

template <typename Mutex, class Alloc>
void SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::flush() {
    std::lock_guard<Mutex> lock(mutex_);
    flush_();
}

template <typename Mutex, class Alloc>
void SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::set_pattern(const std::string &pattern) {
    std::lock_guard<Mutex> lock(mutex_);
    set_pattern_(pattern);
}

template <typename Mutex, class Alloc>
void SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::set_formatter(
    std::unique_ptr<spdlog::basic_formatter<Alloc>> sink_formatter) {
    std::lock_guard<Mutex> lock(mutex_);
    set_formatter_(std::move(sink_formatter));
}

template <typename Mutex, class Alloc>
void SPDLOG_INLINE
spdlog::sinks::base_sink<Mutex, Alloc>::set_pattern_(const std::string &pattern) {
    set_formatter_(details::make_unique<spdlog::basic_pattern_formatter<Alloc>>(pattern));
}

template <typename Mutex, class Alloc>
void SPDLOG_INLINE spdlog::sinks::base_sink<Mutex, Alloc>::set_formatter_(
    std::unique_ptr<spdlog::basic_formatter<Alloc>> sink_formatter) {
    formatter_ = std::move(sink_formatter);
}
