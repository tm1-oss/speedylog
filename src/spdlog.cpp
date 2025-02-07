// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifndef SPDLOG_COMPILED_LIB
#error Please define SPDLOG_COMPILED_LIB to compile this file.
#endif

#include <spdlog/common-inl.h>
#include <spdlog/details/backtracer-inl.h>
#include <spdlog/details/log_msg-inl.h>
#include <spdlog/details/log_msg_buffer-inl.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os-inl.h>
#include <spdlog/details/registry-inl.h>
#include <spdlog/logger-inl.h>
#include <spdlog/pattern_formatter-inl.h>
#include <spdlog/sinks/base_sink-inl.h>
#include <spdlog/sinks/sink-inl.h>
#include <spdlog/spdlog-inl.h>

#include <mutex>

// template instantiate logger constructor with sinks init list
template SPDLOG_API spdlog::basic_logger<spdlog::default_allocator_t>::basic_logger(
    std::string name,
    sinks_init_list<default_allocator_t>::iterator begin,
    sinks_init_list<default_allocator_t>::iterator end,
    spdlog::default_allocator_t);
template class SPDLOG_API spdlog::sinks::base_sink<std::mutex, spdlog::default_allocator_t>;
template class SPDLOG_API
    spdlog::sinks::base_sink<spdlog::details::null_mutex, spdlog::default_allocator_t>;
template class SPDLOG_API spdlog::details::log_msg_buffer<spdlog::default_allocator_t>;
template SPDLOG_API void spdlog::swap<spdlog::default_allocator_t>(
    spdlog::basic_logger<spdlog::default_allocator_t> &,
    spdlog::basic_logger<spdlog::default_allocator_t> &);

// These classes are all implicitly instantiated by the registry<Alloc> constructor.
// The lines below are kept without SPDLOG_API to force emission of all method bodies into this TU;
// the visibility("default") from SPDLOG_API on each class definition in the header is sufficient
// to export them from the shared library.
template class spdlog::sinks::sink<spdlog::default_allocator_t>;
template class spdlog::details::registry<spdlog::default_allocator_t>;
template class spdlog::basic_pattern_formatter<spdlog::default_allocator_t>;
template class spdlog::basic_logger<spdlog::default_allocator_t>;
template class spdlog::details::backtracer<spdlog::default_allocator_t>;

#ifdef SPDLOG_POLYMORPHIC_ALLOCATORS
#include <memory_resource>

template class SPDLOG_API spdlog::sinks::sink<std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API
    spdlog::sinks::base_sink<std::mutex, std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API
    spdlog::sinks::base_sink<spdlog::details::null_mutex, std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API spdlog::details::registry<std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API spdlog::details::backtracer<std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API spdlog::details::log_msg_buffer<std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API spdlog::basic_pattern_formatter<std::pmr::polymorphic_allocator<char>>;
template class SPDLOG_API spdlog::basic_logger<std::pmr::polymorphic_allocator<char>>;
#endif
