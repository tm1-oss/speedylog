// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/sinks/basic_file_sink.h>
#endif

#include <spdlog/common.h>
#include <spdlog/details/os.h>

namespace spdlog {
namespace sinks {

template <typename Mutex, class Alloc>
SPDLOG_INLINE basic_file_sink<Mutex, Alloc>::basic_file_sink(
    const filename_t &filename,
    bool truncate,
    const file_event_handlers &event_handlers,
    Alloc alloc)
    : base_sink<Mutex, Alloc>(alloc),
      file_helper_{event_handlers} {
    file_helper_.open(filename, truncate);
}

template <typename Mutex, class Alloc>
SPDLOG_INLINE basic_file_sink<Mutex, Alloc>::basic_file_sink(const filename_t &filename,
                                                             bool truncate,
                                                             Alloc alloc)
    : basic_file_sink<Mutex, Alloc>(filename, truncate, {}, alloc) {}

template <typename Mutex, class Alloc>
SPDLOG_INLINE basic_file_sink<Mutex, Alloc>::basic_file_sink(const filename_t &filename,
                                                             Alloc alloc)
    : basic_file_sink<Mutex, Alloc>(filename, false, alloc) {}

template <typename Mutex, class Alloc>
SPDLOG_INLINE const filename_t &basic_file_sink<Mutex, Alloc>::filename() const {
    return file_helper_.filename();
}

template <typename Mutex, class Alloc>
SPDLOG_INLINE void basic_file_sink<Mutex, Alloc>::truncate() {
    std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
    file_helper_.reopen(true);
}

template <typename Mutex, class Alloc>
SPDLOG_INLINE void basic_file_sink<Mutex, Alloc>::sink_it_(const details::log_msg &msg) {
    basic_memory_buf_t<Alloc> formatted(this->get_allocator());
    base_sink<Mutex, Alloc>::formatter_->format(msg, formatted);
    file_helper_.write(details::to_string_view(formatted));
}

template <typename Mutex, class Alloc>
SPDLOG_INLINE void basic_file_sink<Mutex, Alloc>::flush_() {
    file_helper_.flush();
}

}  // namespace sinks
}  // namespace spdlog
