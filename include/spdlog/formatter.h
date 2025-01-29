// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#include <spdlog/details/log_msg.h>
#include <spdlog/fmt/fmt.h>

namespace spdlog {

template <class Alloc = default_allocator_t>
class basic_formatter {
public:
    virtual ~basic_formatter() = default;
    virtual void format(const details::log_msg &msg, basic_memory_buf_t<Alloc> &dest) = 0;
    virtual std::unique_ptr<basic_formatter> clone() const = 0;
};

using formatter = basic_formatter<default_allocator_t>;

}  // namespace spdlog
