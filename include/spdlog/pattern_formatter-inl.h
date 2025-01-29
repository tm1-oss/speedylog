// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/pattern_formatter.h>
#endif

#include <spdlog/details/fmt_helper.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>

#ifndef SPDLOG_NO_TLS
#include <spdlog/mdc.h>
#endif

#include <spdlog/fmt/fmt.h>
#include <spdlog/formatter.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace spdlog {
namespace details {

///////////////////////////////////////////////////////////////////////
// name & level pattern appender
///////////////////////////////////////////////////////////////////////

template <class Alloc>
class scoped_padder {
public:
    scoped_padder(size_t wrapped_size, const padding_info &padinfo, basic_memory_buf_t<Alloc> &dest)
        : padinfo_(padinfo),
          dest_(dest) {
        remaining_pad_ = static_cast<long>(padinfo.width_) - static_cast<long>(wrapped_size);
        if (remaining_pad_ <= 0) {
            return;
        }

        if (padinfo_.side_ == padding_info::pad_side::left) {
            pad_it(remaining_pad_);
            remaining_pad_ = 0;
        } else if (padinfo_.side_ == padding_info::pad_side::center) {
            auto half_pad = remaining_pad_ / 2;
            auto reminder = remaining_pad_ & 1;
            pad_it(half_pad);
            remaining_pad_ = half_pad + reminder;  // for the right side
        }
    }

    template <typename T>
    static unsigned int count_digits(T n) {
        return fmt_helper::count_digits(n);
    }

    ~scoped_padder() {
        if (remaining_pad_ >= 0) {
            pad_it(remaining_pad_);
        } else if (padinfo_.truncate_) {
            long new_size = static_cast<long>(dest_.size()) + remaining_pad_;
            if (new_size < 0) {
                new_size = 0;
            }
            dest_.resize(static_cast<size_t>(new_size));
        }
    }

private:
    void pad_it(long count) {
        fmt_helper::append_string_view(string_view_t(spaces_.data(), static_cast<size_t>(count)),
                                       dest_);
    }

    const padding_info &padinfo_;
    basic_memory_buf_t<Alloc> &dest_;
    long remaining_pad_;
    string_view_t spaces_{"                                                                ", 64};
};

template <class Alloc>
struct null_scoped_padder {
    null_scoped_padder(size_t /*wrapped_size*/,
                       const padding_info & /*padinfo*/,
                       basic_memory_buf_t<Alloc> & /*dest*/) {}

    template <typename T>
    static unsigned int count_digits(T /* number */) {
        return 0;
    }
};

template <template <typename> class ScopedPadder, class Alloc>
class name_formatter final : public flag_formatter<Alloc> {
public:
    explicit name_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        ScopedPadder<Alloc> p(msg.logger_name.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(msg.logger_name, dest);
    }
};

// log level appender
template <template <typename> class ScopedPadder, class Alloc>
class level_formatter final : public flag_formatter<Alloc> {
public:
    explicit level_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        const string_view_t &level_name = level::to_string_view(msg.level);
        ScopedPadder<Alloc> p(level_name.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(level_name, dest);
    }
};

// short log level appender
template <template <typename> class ScopedPadder, class Alloc>
class short_level_formatter final : public flag_formatter<Alloc> {
public:
    explicit short_level_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        string_view_t level_name{level::to_short_c_str(msg.level)};
        ScopedPadder<Alloc> p(level_name.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(level_name, dest);
    }
};

///////////////////////////////////////////////////////////////////////
// Date time pattern appenders
///////////////////////////////////////////////////////////////////////

static const char *ampm(const tm &t) { return t.tm_hour >= 12 ? "PM" : "AM"; }

static int to12h(const tm &t) { return t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour; }

// Abbreviated weekday name
static std::array<const char *, 7> days{{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}};

template <template <typename> class ScopedPadder, class Alloc>
class a_formatter final : public flag_formatter<Alloc> {
public:
    explicit a_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        string_view_t field_value{days[static_cast<size_t>(tm_time.tm_wday)]};
        ScopedPadder<Alloc> p(field_value.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(field_value, dest);
    }
};

// Full weekday name
static std::array<const char *, 7> full_days{
    {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}};

template <template <typename> class ScopedPadder, class Alloc>
class A_formatter : public flag_formatter<Alloc> {
public:
    explicit A_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        string_view_t field_value{full_days[static_cast<size_t>(tm_time.tm_wday)]};
        ScopedPadder<Alloc> p(field_value.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(field_value, dest);
    }
};

// Abbreviated month
static const std::array<const char *, 12> months{
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}};

template <template <typename> class ScopedPadder, class Alloc>
class b_formatter final : public flag_formatter<Alloc> {
public:
    explicit b_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        string_view_t field_value{months[static_cast<size_t>(tm_time.tm_mon)]};
        ScopedPadder<Alloc> p(field_value.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(field_value, dest);
    }
};

// Full month name
static const std::array<const char *, 12> full_months{{"January", "February", "March", "April",
                                                       "May", "June", "July", "August", "September",
                                                       "October", "November", "December"}};

template <template <typename> class ScopedPadder, class Alloc>
class B_formatter final : public flag_formatter<Alloc> {
public:
    explicit B_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        string_view_t field_value{full_months[static_cast<size_t>(tm_time.tm_mon)]};
        ScopedPadder<Alloc> p(field_value.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(field_value, dest);
    }
};

// Date and time representation (Thu Aug 23 15:35:46 2014)
template <template <typename> class ScopedPadder, class Alloc>
class c_formatter final : public flag_formatter<Alloc> {
public:
    explicit c_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 24;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);

        fmt_helper::append_string_view(days[static_cast<size_t>(tm_time.tm_wday)], dest);
        dest.push_back(' ');
        fmt_helper::append_string_view(months[static_cast<size_t>(tm_time.tm_mon)], dest);
        dest.push_back(' ');
        fmt_helper::append_int(tm_time.tm_mday, dest);
        dest.push_back(' ');
        // time

        fmt_helper::pad2(tm_time.tm_hour, dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_min, dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_sec, dest);
        dest.push_back(' ');
        fmt_helper::append_int(tm_time.tm_year + 1900, dest);
    }
};

// year - 2 digit
template <template <typename> class ScopedPadder, class Alloc>
class C_formatter final : public flag_formatter<Alloc> {
public:
    explicit C_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(tm_time.tm_year % 100, dest);
    }
};

// Short MM/DD/YY date, equivalent to %m/%d/%y 08/23/01
template <template <typename> class ScopedPadder, class Alloc>
class D_formatter final : public flag_formatter<Alloc> {
public:
    explicit D_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 10;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);

        fmt_helper::pad2(tm_time.tm_mon + 1, dest);
        dest.push_back('/');
        fmt_helper::pad2(tm_time.tm_mday, dest);
        dest.push_back('/');
        fmt_helper::pad2(tm_time.tm_year % 100, dest);
    }
};

// year - 4 digit
template <template <typename> class ScopedPadder, class Alloc>
class Y_formatter final : public flag_formatter<Alloc> {
public:
    explicit Y_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 4;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::append_int(tm_time.tm_year + 1900, dest);
    }
};

// month 1-12
template <template <typename> class ScopedPadder, class Alloc>
class m_formatter final : public flag_formatter<Alloc> {
public:
    explicit m_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(tm_time.tm_mon + 1, dest);
    }
};

// day of month 1-31
template <template <typename> class ScopedPadder, class Alloc>
class d_formatter final : public flag_formatter<Alloc> {
public:
    explicit d_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(tm_time.tm_mday, dest);
    }
};

// hours in 24 format 0-23
template <template <typename> class ScopedPadder, class Alloc>
class H_formatter final : public flag_formatter<Alloc> {
public:
    explicit H_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(tm_time.tm_hour, dest);
    }
};

// hours in 12 format 1-12
template <template <typename> class ScopedPadder, class Alloc>
class I_formatter final : public flag_formatter<Alloc> {
public:
    explicit I_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(to12h(tm_time), dest);
    }
};

// minutes 0-59
template <template <typename> class ScopedPadder, class Alloc>
class M_formatter final : public flag_formatter<Alloc> {
public:
    explicit M_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(tm_time.tm_min, dest);
    }
};

// seconds 0-59
template <template <typename> class ScopedPadder, class Alloc>
class S_formatter final : public flag_formatter<Alloc> {
public:
    explicit S_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad2(tm_time.tm_sec, dest);
    }
};

// milliseconds
template <template <typename> class ScopedPadder, class Alloc>
class e_formatter final : public flag_formatter<Alloc> {
public:
    explicit e_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        auto millis = fmt_helper::time_fraction<std::chrono::milliseconds>(msg.time);
        const size_t field_size = 3;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad3(static_cast<uint32_t>(millis.count()), dest);
    }
};

// microseconds
template <template <typename> class ScopedPadder, class Alloc>
class f_formatter final : public flag_formatter<Alloc> {
public:
    explicit f_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        auto micros = fmt_helper::time_fraction<std::chrono::microseconds>(msg.time);

        const size_t field_size = 6;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad6(static_cast<size_t>(micros.count()), dest);
    }
};

// nanoseconds
template <template <typename> class ScopedPadder, class Alloc>
class F_formatter final : public flag_formatter<Alloc> {
public:
    explicit F_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        auto ns = fmt_helper::time_fraction<std::chrono::nanoseconds>(msg.time);
        const size_t field_size = 9;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::pad9(static_cast<size_t>(ns.count()), dest);
    }
};

// seconds since epoch
template <template <typename> class ScopedPadder, class Alloc>
class E_formatter final : public flag_formatter<Alloc> {
public:
    explicit E_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 10;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        auto duration = msg.time.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        fmt_helper::append_int(seconds, dest);
    }
};

// AM/PM
template <template <typename> class ScopedPadder, class Alloc>
class p_formatter final : public flag_formatter<Alloc> {
public:
    explicit p_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 2;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::append_string_view(ampm(tm_time), dest);
    }
};

// 12 hour clock 02:55:02 pm
template <template <typename> class ScopedPadder, class Alloc>
class r_formatter final : public flag_formatter<Alloc> {
public:
    explicit r_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 11;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);

        fmt_helper::pad2(to12h(tm_time), dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_min, dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_sec, dest);
        dest.push_back(' ');
        fmt_helper::append_string_view(ampm(tm_time), dest);
    }
};

// 24-hour HH:MM time, equivalent to %H:%M
template <template <typename> class ScopedPadder, class Alloc>
class R_formatter final : public flag_formatter<Alloc> {
public:
    explicit R_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 5;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);

        fmt_helper::pad2(tm_time.tm_hour, dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_min, dest);
    }
};

// ISO 8601 time format (HH:MM:SS), equivalent to %H:%M:%S
template <template <typename> class ScopedPadder, class Alloc>
class T_formatter final : public flag_formatter<Alloc> {
public:
    explicit T_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 8;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);

        fmt_helper::pad2(tm_time.tm_hour, dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_min, dest);
        dest.push_back(':');
        fmt_helper::pad2(tm_time.tm_sec, dest);
    }
};

// ISO 8601 offset from UTC in timezone (+-HH:MM)
template <template <typename> class ScopedPadder, class Alloc>
class z_formatter final : public flag_formatter<Alloc> {
public:
    explicit z_formatter(padding_info padinfo, pattern_time_type time_type)
        : flag_formatter<Alloc>(padinfo),
          time_type_(time_type) {}

    z_formatter() = default;
    z_formatter(const z_formatter &) = delete;
    z_formatter &operator=(const z_formatter &) = delete;

    void format(const details::log_msg &msg,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        const size_t field_size = 6;
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
#ifdef SPDLOG_NO_TZ_OFFSET
        (void)msg;
        (void)tm_time;
        const char *str = "+??:??";
        dest.append(str, str + 6);
#else
        if (time_type_ == pattern_time_type::utc) {
            const char *zeroes = "+00:00";
            dest.append(zeroes, zeroes + 6);
            return;
        }
        auto total_minutes = get_cached_offset(msg, tm_time);
        bool is_negative = total_minutes < 0;
        if (is_negative) {
            total_minutes = -total_minutes;
            dest.push_back('-');
        } else {
            dest.push_back('+');
        }

        fmt_helper::pad2(total_minutes / 60, dest);  // hours
        dest.push_back(':');
        fmt_helper::pad2(total_minutes % 60, dest);  // minutes
#endif  // SPDLOG_NO_TZ_OFFSET
    }

private:
    pattern_time_type time_type_;
    log_clock::time_point last_update_{std::chrono::seconds(0)};
    int offset_minutes_{0};

    int get_cached_offset(const log_msg &msg, const std::tm &tm_time) {
        // refresh every 10 seconds
        if (msg.time - last_update_ >= std::chrono::seconds(10)) {
            offset_minutes_ = os::utc_minutes_offset(tm_time);
            last_update_ = msg.time;
        }
        return offset_minutes_;
    }
};

// Thread id
template <template <typename> class ScopedPadder, class Alloc>
class t_formatter final : public flag_formatter<Alloc> {
public:
    explicit t_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        const auto field_size = ScopedPadder<Alloc>::count_digits(msg.thread_id);
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::append_int(msg.thread_id, dest);
    }
};

// Current pid
template <template <typename> class ScopedPadder, class Alloc>
class pid_formatter final : public flag_formatter<Alloc> {
public:
    explicit pid_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        const auto pid = static_cast<uint32_t>(details::os::pid());
        auto field_size = ScopedPadder<Alloc>::count_digits(pid);
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::append_int(pid, dest);
    }
};

template <template <typename> class ScopedPadder, class Alloc>
class v_formatter final : public flag_formatter<Alloc> {
public:
    explicit v_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        ScopedPadder<Alloc> p(msg.payload.size(), this->padinfo_, dest);
        fmt_helper::append_string_view(msg.payload, dest);
    }
};

template <class Alloc>
class ch_formatter final : public flag_formatter<Alloc> {
public:
    explicit ch_formatter(char ch)
        : ch_(ch) {}

    void format(const details::log_msg &,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        dest.push_back(ch_);
    }

private:
    char ch_;
};

// aggregate user chars to display as is
template <class Alloc>
class aggregate_formatter final : public flag_formatter<Alloc> {
public:
    aggregate_formatter() = default;

    void add_ch(char ch) { str_ += ch; }
    void format(const details::log_msg &,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        fmt_helper::append_string_view(str_, dest);
    }

private:
    std::string str_;
};

// mark the color range. expect it to be in the form of "%^colored text%$"
template <class Alloc>
class color_start_formatter final : public flag_formatter<Alloc> {
public:
    explicit color_start_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        msg.color_range_start = dest.size();
    }
};

template <class Alloc>
class color_stop_formatter final : public flag_formatter<Alloc> {
public:
    explicit color_stop_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        msg.color_range_end = dest.size();
    }
};

// print source location
template <template <typename> class ScopedPadder, class Alloc>
class source_location_formatter final : public flag_formatter<Alloc> {
public:
    explicit source_location_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        if (msg.source.empty()) {
            ScopedPadder<Alloc> p(0, this->padinfo_, dest);
            return;
        }

        size_t text_size;
        if (this->padinfo_.enabled()) {
            // calc text size for padding based on "filename:line"
            text_size = std::char_traits<char>::length(msg.source.filename) +
                        ScopedPadder<Alloc>::count_digits(msg.source.line) + 1;
        } else {
            text_size = 0;
        }

        ScopedPadder<Alloc> p(text_size, this->padinfo_, dest);
        fmt_helper::append_string_view(msg.source.filename, dest);
        dest.push_back(':');
        fmt_helper::append_int(msg.source.line, dest);
    }
};

// print source filename
template <template <typename> class ScopedPadder, class Alloc>
class source_filename_formatter final : public flag_formatter<Alloc> {
public:
    explicit source_filename_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        if (msg.source.empty()) {
            ScopedPadder<Alloc> p(0, this->padinfo_, dest);
            return;
        }
        size_t text_size =
            this->padinfo_.enabled() ? std::char_traits<char>::length(msg.source.filename) : 0;
        ScopedPadder<Alloc> p(text_size, this->padinfo_, dest);
        fmt_helper::append_string_view(msg.source.filename, dest);
    }
};

template <template <typename> class ScopedPadder, class Alloc>
class short_filename_formatter final : public flag_formatter<Alloc> {
public:
    explicit short_filename_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)  // consider using 'if constexpr' instead
#endif                           // _MSC_VER
    static const char *basename(const char *filename) {
        // if the size is 2 (1 character + null terminator) we can use the more efficient strrchr
        // the branch will be elided by optimizations
        if (sizeof(os::folder_seps) == 2) {
            const char *rv = std::strrchr(filename, os::folder_seps[0]);
            return rv != nullptr ? rv + 1 : filename;
        } else {
            const std::reverse_iterator<const char *> begin(filename + std::strlen(filename));
            const std::reverse_iterator<const char *> end(filename);

            const auto it = std::find_first_of(begin, end, std::begin(os::folder_seps),
                                               std::end(os::folder_seps) - 1);
            return it != end ? it.base() : filename;
        }
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif  // _MSC_VER

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        if (msg.source.empty()) {
            ScopedPadder<Alloc> p(0, this->padinfo_, dest);
            return;
        }
        auto filename = basename(msg.source.filename);
        size_t text_size = this->padinfo_.enabled() ? std::char_traits<char>::length(filename) : 0;
        ScopedPadder<Alloc> p(text_size, this->padinfo_, dest);
        fmt_helper::append_string_view(filename, dest);
    }
};

template <template <typename> class ScopedPadder, class Alloc>
class source_linenum_formatter final : public flag_formatter<Alloc> {
public:
    explicit source_linenum_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        if (msg.source.empty()) {
            ScopedPadder<Alloc> p(0, this->padinfo_, dest);
            return;
        }

        auto field_size = ScopedPadder<Alloc>::count_digits(msg.source.line);
        ScopedPadder<Alloc> p(field_size, this->padinfo_, dest);
        fmt_helper::append_int(msg.source.line, dest);
    }
};

// print source funcname
template <template <typename> class ScopedPadder, class Alloc>
class source_funcname_formatter final : public flag_formatter<Alloc> {
public:
    explicit source_funcname_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        if (msg.source.empty()) {
            ScopedPadder<Alloc> p(0, this->padinfo_, dest);
            return;
        }
        size_t text_size =
            this->padinfo_.enabled() ? std::char_traits<char>::length(msg.source.funcname) : 0;
        ScopedPadder<Alloc> p(text_size, this->padinfo_, dest);
        fmt_helper::append_string_view(msg.source.funcname, dest);
    }
};

// print elapsed time since last message
template <template <typename> class ScopedPadder, typename Units, class Alloc>
class elapsed_formatter final : public flag_formatter<Alloc> {
public:
    using DurationUnits = Units;

    explicit elapsed_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo),
          last_message_time_(log_clock::now()) {}

    void format(const details::log_msg &msg,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        auto delta = (std::max)(msg.time - last_message_time_, log_clock::duration::zero());
        auto delta_units = std::chrono::duration_cast<DurationUnits>(delta);
        last_message_time_ = msg.time;
        auto delta_count = static_cast<size_t>(delta_units.count());
        auto n_digits = static_cast<size_t>(ScopedPadder<Alloc>::count_digits(delta_count));
        ScopedPadder<Alloc> p(n_digits, this->padinfo_, dest);
        fmt_helper::append_int(delta_count, dest);
    }

private:
    log_clock::time_point last_message_time_;
};

// Class for formatting Mapped Diagnostic Context (MDC) in log messages.
// Example: [logger-name] [info] [mdc_key_1:mdc_value_1 mdc_key_2:mdc_value_2] some message
#ifndef SPDLOG_NO_TLS
template <template <typename> class ScopedPadder, class Alloc>
class mdc_formatter : public flag_formatter<Alloc> {
public:
    explicit mdc_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &,
                const std::tm &,
                basic_memory_buf_t<Alloc> &dest) override {
        auto &mdc_map = mdc::get_context();
        if (mdc_map.empty()) {
            ScopedPadder<Alloc> p(0, this->padinfo_, dest);
            return;
        } else {
            format_mdc(mdc_map, dest);
        }
    }

    void format_mdc(const mdc::mdc_map_t &mdc_map, basic_memory_buf_t<Alloc> &dest) {
        auto last_element = --mdc_map.end();
        for (auto it = mdc_map.begin(); it != mdc_map.end(); ++it) {
            auto &pair = *it;
            const auto &key = pair.first;
            const auto &value = pair.second;
            size_t content_size = key.size() + value.size() + 1;  // 1 for ':'

            if (it != last_element) {
                content_size++;  // 1 for ' '
            }

            ScopedPadder<Alloc> p(content_size, this->padinfo_, dest);
            fmt_helper::append_string_view(key, dest);
            fmt_helper::append_string_view(":", dest);
            fmt_helper::append_string_view(value, dest);
            if (it != last_element) {
                fmt_helper::append_string_view(" ", dest);
            }
        }
    }
};
#endif

// Full info formatter
// pattern: [%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v
template <class Alloc>
class full_formatter final : public flag_formatter<Alloc> {
public:
    explicit full_formatter(padding_info padinfo)
        : flag_formatter<Alloc>(padinfo) {}

    void format(const details::log_msg &msg,
                const std::tm &tm_time,
                basic_memory_buf_t<Alloc> &dest) override {
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;
        using std::chrono::seconds;

        // cache the date/time part for the next second.
        auto duration = msg.time.time_since_epoch();
        auto secs = duration_cast<seconds>(duration);

        if (cache_timestamp_ != secs || cached_datetime_.size() == 0) {
            cached_datetime_.clear();
            cached_datetime_.push_back('[');
            fmt_helper::append_int(tm_time.tm_year + 1900, cached_datetime_);
            cached_datetime_.push_back('-');

            fmt_helper::pad2(tm_time.tm_mon + 1, cached_datetime_);
            cached_datetime_.push_back('-');

            fmt_helper::pad2(tm_time.tm_mday, cached_datetime_);
            cached_datetime_.push_back(' ');

            fmt_helper::pad2(tm_time.tm_hour, cached_datetime_);
            cached_datetime_.push_back(':');

            fmt_helper::pad2(tm_time.tm_min, cached_datetime_);
            cached_datetime_.push_back(':');

            fmt_helper::pad2(tm_time.tm_sec, cached_datetime_);
            cached_datetime_.push_back('.');

            cache_timestamp_ = secs;
        }
        dest.append(cached_datetime_.begin(), cached_datetime_.end());

        auto millis = fmt_helper::time_fraction<milliseconds>(msg.time);
        fmt_helper::pad3(static_cast<uint32_t>(millis.count()), dest);
        dest.push_back(']');
        dest.push_back(' ');

        // append logger name if exists
        if (msg.logger_name.size() > 0) {
            dest.push_back('[');
            fmt_helper::append_string_view(msg.logger_name, dest);
            dest.push_back(']');
            dest.push_back(' ');
        }

        dest.push_back('[');
        // wrap the level name with color
        msg.color_range_start = dest.size();
        // fmt_helper::append_string_view(level::to_c_str(msg.level), dest);
        fmt_helper::append_string_view(level::to_string_view(msg.level), dest);
        msg.color_range_end = dest.size();
        dest.push_back(']');
        dest.push_back(' ');

        // add source location if present
        if (!msg.source.empty()) {
            dest.push_back('[');
            const char *filename =
                details::short_filename_formatter<details::null_scoped_padder, Alloc>::basename(
                    msg.source.filename);
            fmt_helper::append_string_view(filename, dest);
            dest.push_back(':');
            fmt_helper::append_int(msg.source.line, dest);
            dest.push_back(']');
            dest.push_back(' ');
        }

#ifndef SPDLOG_NO_TLS
        // add mdc if present
        auto &mdc_map = mdc::get_context();
        if (!mdc_map.empty()) {
            dest.push_back('[');
            mdc_formatter_.format_mdc(mdc_map, dest);
            dest.push_back(']');
            dest.push_back(' ');
        }
#endif
        // fmt_helper::append_string_view(msg.msg(), dest);
        fmt_helper::append_string_view(msg.payload, dest);
    }

private:
    std::chrono::seconds cache_timestamp_{0};
    basic_memory_buf_t<Alloc> cached_datetime_;

#ifndef SPDLOG_NO_TLS
    mdc_formatter<null_scoped_padder, Alloc> mdc_formatter_{padding_info {}};
#endif
};

}  // namespace details

template <class Alloc>
SPDLOG_INLINE basic_pattern_formatter<Alloc>::basic_pattern_formatter(
    std::string pattern,
    pattern_time_type time_type,
    std::string eol,
    custom_flags custom_user_flags)
    : pattern_(std::move(pattern)),
      eol_(std::move(eol)),
      pattern_time_type_(time_type),
      need_localtime_(false),
      last_log_secs_(0),
      custom_handlers_(std::move(custom_user_flags)) {
    std::memset(&cached_tm_, 0, sizeof(cached_tm_));
    compile_pattern_(pattern_);
}

// use by default full formatter for if pattern is not given
template <class Alloc>
SPDLOG_INLINE basic_pattern_formatter<Alloc>::basic_pattern_formatter(pattern_time_type time_type,
                                                                      std::string eol)
    : pattern_("%+"),
      eol_(std::move(eol)),
      pattern_time_type_(time_type),
      need_localtime_(true),
      last_log_secs_(0) {
    std::memset(&cached_tm_, 0, sizeof(cached_tm_));
    formatters_.push_back(
        details::make_unique<details::full_formatter<Alloc>>(details::padding_info{}));
}

template <class Alloc>
SPDLOG_INLINE std::unique_ptr<basic_formatter<Alloc>> basic_pattern_formatter<Alloc>::clone()
    const {
    custom_flags cloned_custom_formatters;
    for (auto &it : custom_handlers_) {
        cloned_custom_formatters[it.first] = it.second->clone();
    }
    auto cloned = details::make_unique<basic_pattern_formatter>(
        pattern_, pattern_time_type_, eol_, std::move(cloned_custom_formatters));
    cloned->need_localtime(need_localtime_);
#if defined(__GNUC__) && __GNUC__ < 5
    return std::move(cloned);
#else
    return cloned;
#endif
}

template <class Alloc>
SPDLOG_INLINE void basic_pattern_formatter<Alloc>::format(const details::log_msg &msg,
                                                          basic_memory_buf_t<Alloc> &dest) {
    if (need_localtime_) {
        const auto secs =
            std::chrono::duration_cast<std::chrono::seconds>(msg.time.time_since_epoch());
        if (secs != last_log_secs_) {
            cached_tm_ = get_time_(msg);
            last_log_secs_ = secs;
        }
    }

    for (auto &f : formatters_) {
        f->format(msg, cached_tm_, dest);
    }
    // write eol
    details::fmt_helper::append_string_view(eol_, dest);
}

template <class Alloc>
SPDLOG_INLINE void basic_pattern_formatter<Alloc>::set_pattern(std::string pattern) {
    pattern_ = std::move(pattern);
    need_localtime_ = false;
    compile_pattern_(pattern_);
}

template <class Alloc>
SPDLOG_INLINE void basic_pattern_formatter<Alloc>::need_localtime(bool need) {
    need_localtime_ = need;
}

template <class Alloc>
SPDLOG_INLINE std::tm basic_pattern_formatter<Alloc>::get_time_(const details::log_msg &msg) {
    if (pattern_time_type_ == pattern_time_type::local) {
        return details::os::localtime(log_clock::to_time_t(msg.time));
    }
    return details::os::gmtime(log_clock::to_time_t(msg.time));
}

template <class Alloc>
template <template <typename> class Padder>
SPDLOG_INLINE void basic_pattern_formatter<Alloc>::handle_flag_(char flag,
                                                                details::padding_info padding) {
    // process custom flags
    auto it = custom_handlers_.find(flag);
    if (it != custom_handlers_.end()) {
        auto custom_handler = it->second->clone();
        custom_handler->set_padding_info(padding);
        formatters_.push_back(std::move(custom_handler));
        return;
    }

    // process built-in flags
    switch (flag) {
        case ('+'):  // default formatter
            formatters_.push_back(details::make_unique<details::full_formatter<Alloc>>(padding));
            need_localtime_ = true;
            break;

        case 'n':  // logger name
            formatters_.push_back(
                details::make_unique<details::name_formatter<Padder, Alloc>>(padding));
            break;

        case 'l':  // level
            formatters_.push_back(
                details::make_unique<details::level_formatter<Padder, Alloc>>(padding));
            break;

        case 'L':  // short level
            formatters_.push_back(
                details::make_unique<details::short_level_formatter<Padder, Alloc>>(padding));
            break;

        case ('t'):  // thread id
            formatters_.push_back(
                details::make_unique<details::t_formatter<Padder, Alloc>>(padding));
            break;

        case ('v'):  // the message text
            formatters_.push_back(
                details::make_unique<details::v_formatter<Padder, Alloc>>(padding));
            break;

        case ('a'):  // weekday
            formatters_.push_back(
                details::make_unique<details::a_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('A'):  // short weekday
            formatters_.push_back(
                details::make_unique<details::A_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('b'):
        case ('h'):  // month
            formatters_.push_back(
                details::make_unique<details::b_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('B'):  // short month
            formatters_.push_back(
                details::make_unique<details::B_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('c'):  // datetime
            formatters_.push_back(
                details::make_unique<details::c_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('C'):  // year 2 digits
            formatters_.push_back(
                details::make_unique<details::C_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('Y'):  // year 4 digits
            formatters_.push_back(
                details::make_unique<details::Y_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('D'):
        case ('x'):  // datetime MM/DD/YY
            formatters_.push_back(
                details::make_unique<details::D_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('m'):  // month 1-12
            formatters_.push_back(
                details::make_unique<details::m_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('d'):  // day of month 1-31
            formatters_.push_back(
                details::make_unique<details::d_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('H'):  // hours 24
            formatters_.push_back(
                details::make_unique<details::H_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('I'):  // hours 12
            formatters_.push_back(
                details::make_unique<details::I_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('M'):  // minutes
            formatters_.push_back(
                details::make_unique<details::M_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('S'):  // seconds
            formatters_.push_back(
                details::make_unique<details::S_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('e'):  // milliseconds
            formatters_.push_back(
                details::make_unique<details::e_formatter<Padder, Alloc>>(padding));
            break;

        case ('f'):  // microseconds
            formatters_.push_back(
                details::make_unique<details::f_formatter<Padder, Alloc>>(padding));
            break;

        case ('F'):  // nanoseconds
            formatters_.push_back(
                details::make_unique<details::F_formatter<Padder, Alloc>>(padding));
            break;

        case ('E'):  // seconds since epoch
            formatters_.push_back(
                details::make_unique<details::E_formatter<Padder, Alloc>>(padding));
            break;

        case ('p'):  // am/pm
            formatters_.push_back(
                details::make_unique<details::p_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('r'):  // 12 hour clock 02:55:02 pm
            formatters_.push_back(
                details::make_unique<details::r_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('R'):  // 24-hour HH:MM time
            formatters_.push_back(
                details::make_unique<details::R_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;

        case ('T'):
        case ('X'):  // ISO 8601 time format (HH:MM:SS)
            formatters_.push_back(
                details::make_unique<details::T_formatter<Padder, Alloc>>(padding));
            need_localtime_ = true;
            break;
        case ('z'):  // timezone
            formatters_.push_back(details::make_unique<details::z_formatter<Padder, Alloc>>(
                padding, pattern_time_type_));
            need_localtime_ = true;
            break;
        case ('P'):  // pid
            formatters_.push_back(
                details::make_unique<details::pid_formatter<Padder, Alloc>>(padding));
            break;

        case ('^'):  // color range start
            formatters_.push_back(
                details::make_unique<details::color_start_formatter<Alloc>>(padding));
            break;

        case ('$'):  // color range end
            formatters_.push_back(
                details::make_unique<details::color_stop_formatter<Alloc>>(padding));
            break;

        case ('@'):  // source location (filename:filenumber)
            formatters_.push_back(
                details::make_unique<details::source_location_formatter<Padder, Alloc>>(padding));
            break;

        case ('s'):  // short source filename - without directory name
            formatters_.push_back(
                details::make_unique<details::short_filename_formatter<Padder, Alloc>>(padding));
            break;

        case ('g'):  // full source filename
            formatters_.push_back(
                details::make_unique<details::source_filename_formatter<Padder, Alloc>>(padding));
            break;

        case ('#'):  // source line number
            formatters_.push_back(
                details::make_unique<details::source_linenum_formatter<Padder, Alloc>>(padding));
            break;

        case ('!'):  // source funcname
            formatters_.push_back(
                details::make_unique<details::source_funcname_formatter<Padder, Alloc>>(padding));
            break;

        case ('%'):  // % char
            formatters_.push_back(details::make_unique<details::ch_formatter<Alloc>>('%'));
            break;

        case ('u'):  // elapsed time since last log message in nanos
            formatters_.push_back(
                details::make_unique<
                    details::elapsed_formatter<Padder, std::chrono::nanoseconds, Alloc>>(padding));
            break;

        case ('i'):  // elapsed time since last log message in micros
            formatters_.push_back(
                details::make_unique<
                    details::elapsed_formatter<Padder, std::chrono::microseconds, Alloc>>(padding));
            break;

        case ('o'):  // elapsed time since last log message in millis
            formatters_.push_back(
                details::make_unique<
                    details::elapsed_formatter<Padder, std::chrono::milliseconds, Alloc>>(padding));
            break;

        case ('O'):  // elapsed time since last log message in seconds
            formatters_.push_back(
                details::make_unique<
                    details::elapsed_formatter<Padder, std::chrono::seconds, Alloc>>(padding));
            break;

#ifndef SPDLOG_NO_TLS  // mdc formatter requires TLS support
        case ('&'):
            formatters_.push_back(
                details::make_unique<details::mdc_formatter<Padder, Alloc>>(padding));
            break;
#endif

        default:  // Unknown flag appears as is
            auto unknown_flag = details::make_unique<details::aggregate_formatter<Alloc>>();

            if (!padding.truncate_) {
                unknown_flag->add_ch('%');
                unknown_flag->add_ch(flag);
                formatters_.push_back((std::move(unknown_flag)));
            }
            // fix issue #1617 (prev char was '!' and should have been treated as funcname flag
            // instead of truncating flag) spdlog::set_pattern("[%10!] %v") => "[      main] some
            // message" spdlog::set_pattern("[%3!!] %v") => "[mai] some message"
            else {
                padding.truncate_ = false;
                formatters_.push_back(
                    details::make_unique<details::source_funcname_formatter<Padder, Alloc>>(
                        padding));
                unknown_flag->add_ch(flag);
                formatters_.push_back((std::move(unknown_flag)));
            }

            break;
    }
}

// Extract given pad spec (e.g. %8X, %=8X, %-8!X, %8!X, %=8!X, %-8!X, %+8!X)
// Advance the given it pass the end of the padding spec found (if any)
// Return padding.
template <class Alloc>
SPDLOG_INLINE details::padding_info basic_pattern_formatter<Alloc>::handle_padspec_(
    std::string::const_iterator &it, std::string::const_iterator end) {
    using details::padding_info;
    using details::scoped_padder;
    const size_t max_width = 64;
    if (it == end) {
        return padding_info{};
    }

    padding_info::pad_side side;
    switch (*it) {
        case '-':
            side = padding_info::pad_side::right;
            ++it;
            break;
        case '=':
            side = padding_info::pad_side::center;
            ++it;
            break;
        default:
            side = details::padding_info::pad_side::left;
            break;
    }

    if (it == end || !std::isdigit(static_cast<unsigned char>(*it))) {
        return padding_info{};  // no padding if no digit found here
    }

    auto width = static_cast<size_t>(*it) - '0';
    for (++it; it != end && std::isdigit(static_cast<unsigned char>(*it)); ++it) {
        auto digit = static_cast<size_t>(*it) - '0';
        width = width * 10 + digit;
    }

    // search for the optional truncate marker '!'
    bool truncate;
    if (it != end && *it == '!') {
        truncate = true;
        ++it;
    } else {
        truncate = false;
    }
    return details::padding_info{std::min<size_t>(width, max_width), side, truncate};
}

template <class Alloc>
SPDLOG_INLINE void basic_pattern_formatter<Alloc>::compile_pattern_(const std::string &pattern) {
    auto end = pattern.end();
    std::unique_ptr<details::aggregate_formatter<Alloc>> user_chars;
    formatters_.clear();
    for (auto it = pattern.begin(); it != end; ++it) {
        if (*it == '%') {
            if (user_chars)  // append user chars found so far
            {
                formatters_.push_back(std::move(user_chars));
            }

            auto padding = handle_padspec_(++it, end);

            if (it != end) {
                if (padding.enabled()) {
                    handle_flag_<details::scoped_padder>(*it, padding);
                } else {
                    handle_flag_<details::null_scoped_padder>(*it, padding);
                }
            } else {
                break;
            }
        } else  // chars not following the % sign should be displayed as is
        {
            if (!user_chars) {
                user_chars = details::make_unique<details::aggregate_formatter<Alloc>>();
            }
            user_chars->add_ch(*it);
        }
    }
    if (user_chars)  // append raw chars found so far
    {
        formatters_.push_back(std::move(user_chars));
    }
}
}  // namespace spdlog
