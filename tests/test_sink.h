//
// Copyright(c) 2018 Gabi Melman.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

#pragma once

#include "spdlog/details/null_mutex.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/fmt/fmt.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spdlog {
namespace sinks {

template <class Mutex, class Alloc = default_allocator_t>
class test_sink : public base_sink<Mutex, Alloc> {
    const size_t lines_to_save = 100;

public:
    using string = std::basic_string<char, std::char_traits<char>, Alloc>;
    using string_alloc = typename std::allocator_traits<Alloc>::template rebind_alloc<string>;
    using allocator_type = Alloc;

    explicit test_sink(Alloc alloc = Alloc())
        : base_sink<Mutex, Alloc>(alloc) {}

    size_t msg_counter() {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        return msg_counter_;
    }

    size_t flush_counter() {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        return flush_counter_;
    }

    void set_delay(std::chrono::milliseconds delay) {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        delay_ = delay;
    }

    // return last output without the eol
    std::vector<string, string_alloc> lines() {
        std::lock_guard<Mutex> lock(base_sink<Mutex, Alloc>::mutex_);
        return lines_;
    }

protected:
    void sink_it_(const details::log_msg &msg) override {
        basic_memory_buf_t<Alloc> formatted(this->get_allocator());
        base_sink<Mutex, Alloc>::formatter_->format(msg, formatted);
        // save the line without the eol
        auto eol_len = strlen(details::os::default_eol);
        using diff_t = typename std::iterator_traits<decltype(formatted.end())>::difference_type;
        if (lines_.size() < lines_to_save) {
            lines_.emplace_back(formatted.begin(), formatted.end() - static_cast<diff_t>(eol_len));
        }
        msg_counter_++;
        std::this_thread::sleep_for(delay_);
    }

    void flush_() override { flush_counter_++; }

    size_t msg_counter_{0};
    size_t flush_counter_{0};
    std::chrono::milliseconds delay_{std::chrono::milliseconds::zero()};
    std::vector<string, string_alloc> lines_;
};

using test_sink_mt = test_sink<std::mutex>;
using test_sink_st = test_sink<details::null_mutex>;

}  // namespace sinks
}  // namespace spdlog
