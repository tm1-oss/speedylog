// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#include <memory>

namespace spdlog {

using default_allocator_t = std::allocator<char>;

template <class Alloc>
class basic_logger;
using logger = basic_logger<default_allocator_t>;

template <class Alloc>
class basic_formatter;
using formatter = basic_formatter<default_allocator_t>;

namespace sinks {
template <class Alloc>
class sink;
}

namespace level {
enum level_enum : int;
}

}  // namespace spdlog
