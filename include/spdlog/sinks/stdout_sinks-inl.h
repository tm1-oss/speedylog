// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/sinks/stdout_sinks.h>
#endif

#include <memory>
#include <spdlog/details/console_globals.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/details/os.h>

#ifdef _WIN32
// under windows using fwrite to non-binary stream results in \r\r\n (see issue #1675)
// so instead we use ::FileWrite
#include <spdlog/details/windows_include.h>

#ifndef _USING_V110_SDK71_  // fileapi.h doesn't exist in winxp
#include <fileapi.h>        // WriteFile (..)
#endif

#include <io.h>     // _get_osfhandle(..)
#include <stdio.h>  // _fileno(..)
#endif              // _WIN32

namespace spdlog {

namespace sinks {

template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE stdout_sink_base<ConsoleMutex, Alloc>::stdout_sink_base(FILE *file, Alloc alloc)
    : sink<Alloc>(alloc),
      mutex_(ConsoleMutex::mutex()),
      file_(file),
      formatter_(details::make_unique<spdlog::basic_pattern_formatter<Alloc>>()) {
#ifdef _WIN32
    // get windows handle from the FILE* object

    handle_ = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file_)));

    // don't throw to support cases where no console is attached,
    // and let the log method to do nothing if (handle_ == INVALID_HANDLE_VALUE).
    // throw only if non stdout/stderr target is requested (probably regular file and not console).
    if (handle_ == INVALID_HANDLE_VALUE && file != stdout && file != stderr) {
        throw_spdlog_ex("spdlog::stdout_sink_base: _get_osfhandle() failed", errno);
    }
#endif  // _WIN32
}

template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE void stdout_sink_base<ConsoleMutex, Alloc>::log(const details::log_msg &msg) {
#ifdef _WIN32
    if (handle_ == INVALID_HANDLE_VALUE) {
        return;
    }
    std::lock_guard<mutex_t> lock(mutex_);
    memory_buf_t formatted;
    formatter_->format(msg, formatted);
    auto size = static_cast<DWORD>(formatted.size());
    DWORD bytes_written = 0;
    bool ok = ::WriteFile(handle_, formatted.data(), size, &bytes_written, nullptr) != 0;
    if (!ok) {
        throw_spdlog_ex("stdout_sink_base: WriteFile() failed. GetLastError(): " +
                        std::to_string(::GetLastError()));
    }
#else
    std::lock_guard<mutex_t> lock(mutex_);
    basic_memory_buf_t<Alloc> formatted(this->get_allocator());
    formatter_->format(msg, formatted);
    details::os::fwrite_bytes(formatted.data(), formatted.size(), file_);
#endif                // _WIN32
    ::fflush(file_);  // flush every line to terminal
}

template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE void stdout_sink_base<ConsoleMutex, Alloc>::flush() {
    std::lock_guard<mutex_t> lock(mutex_);
    fflush(file_);
}

template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE void stdout_sink_base<ConsoleMutex, Alloc>::set_pattern(const std::string &pattern) {
    std::lock_guard<mutex_t> lock(mutex_);
    formatter_ = std::unique_ptr<spdlog::basic_formatter<Alloc>>(
        new basic_pattern_formatter<Alloc>(pattern));
}

template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE void stdout_sink_base<ConsoleMutex, Alloc>::set_formatter(
    std::unique_ptr<spdlog::basic_formatter<Alloc>> sink_formatter) {
    std::lock_guard<mutex_t> lock(mutex_);
    formatter_ = std::move(sink_formatter);
}

// stdout sink
template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE stdout_sink<ConsoleMutex, Alloc>::stdout_sink()
    : stdout_sink_base<ConsoleMutex, Alloc>(stdout) {}

// stderr sink
template <typename ConsoleMutex, class Alloc>
SPDLOG_INLINE stderr_sink<ConsoleMutex, Alloc>::stderr_sink()
    : stdout_sink_base<ConsoleMutex, Alloc>(stderr) {}

}  // namespace sinks

// factory methods
template <typename Factory>
SPDLOG_INLINE std::shared_ptr<logger> stdout_logger_mt(const std::string &logger_name) {
    return Factory::template create<sinks::stdout_sink_mt>(logger_name);
}

template <typename Factory>
SPDLOG_INLINE std::shared_ptr<logger> stdout_logger_st(const std::string &logger_name) {
    return Factory::template create<sinks::stdout_sink_st>(logger_name);
}

template <typename Factory>
SPDLOG_INLINE std::shared_ptr<logger> stderr_logger_mt(const std::string &logger_name) {
    return Factory::template create<sinks::stderr_sink_mt>(logger_name);
}

template <typename Factory>
SPDLOG_INLINE std::shared_ptr<logger> stderr_logger_st(const std::string &logger_name) {
    return Factory::template create<sinks::stderr_sink_st>(logger_name);
}
}  // namespace spdlog
