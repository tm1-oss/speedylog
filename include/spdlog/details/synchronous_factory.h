// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#include "registry.h"
#include <memory>

namespace spdlog {

// Default logger factory-  creates synchronous loggers

template <class Alloc = default_allocator_t>
struct basic_synchronous_factory {
    template <typename Sink, typename... SinkArgs>
    static std::shared_ptr<spdlog::basic_logger<Alloc>> create(std::string logger_name,
                                                               SinkArgs &&...args) {
        auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
        auto new_logger =
            std::make_shared<spdlog::basic_logger<Alloc>>(std::move(logger_name), std::move(sink));
        details::registry<Alloc>::instance().initialize_logger(new_logger);
        return new_logger;
    }
};

using synchronous_factory = basic_synchronous_factory<default_allocator_t>;

}  // namespace spdlog
