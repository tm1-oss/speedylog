// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#include "base_sink.h"
#include <spdlog/details/log_msg.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/pattern_formatter.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

// Distribution sink (mux). Stores a vector of sinks which get called when log
// is called

namespace spdlog {
namespace sinks {

template <typename Mutex, class Alloc>
class dist_sink : public base_sink<Mutex, Alloc> {
public:
    using allocator_type = Alloc;

    explicit dist_sink(Alloc alloc = Alloc())
        : base_sink<Mutex, Alloc>(alloc) {}
    explicit dist_sink(std::vector<std::shared_ptr<sink<Alloc>>> sinks, Alloc alloc = Alloc())
        : base_sink<Mutex, Alloc>(alloc),
          sinks_(sinks) {}

    dist_sink(const dist_sink &) = delete;
    dist_sink &operator=(const dist_sink &) = delete;

    void add_sink(std::shared_ptr<sink<Alloc>> sub_sink) {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        sinks_.push_back(sub_sink);
    }

    void remove_sink(std::shared_ptr<sink<Alloc>> sub_sink) {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sub_sink), sinks_.end());
    }

    void set_sinks(std::vector<std::shared_ptr<sink<Alloc>>> sinks) {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        sinks_ = std::move(sinks);
    }

    std::vector<std::shared_ptr<sink<Alloc>>> &sinks() { return sinks_; }

protected:
    void sink_it_(const details::log_msg &msg) override {
        for (auto &sub_sink : sinks_) {
            if (sub_sink->should_log(msg.level)) {
                sub_sink->log(msg);
            }
        }
    }

    void flush_() override {
        for (auto &sub_sink : sinks_) {
            sub_sink->flush();
        }
    }

    void set_pattern_(const std::string &pattern) override {
        set_formatter_(details::make_unique<spdlog::basic_pattern_formatter<Alloc>>(pattern));
    }

    void set_formatter_(std::unique_ptr<spdlog::basic_formatter<Alloc>> sink_formatter) override {
        base_sink<Mutex, Alloc>::formatter_ = std::move(sink_formatter);
        for (auto &sub_sink : sinks_) {
            sub_sink->set_formatter(base_sink<Mutex, Alloc>::formatter_->clone());
        }
    }
    std::vector<std::shared_ptr<sink<Alloc>>> sinks_;
};

using dist_sink_mt = dist_sink<std::mutex, default_allocator_t>;
using dist_sink_st = dist_sink<details::null_mutex, default_allocator_t>;

}  // namespace sinks
}  // namespace spdlog
