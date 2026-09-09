#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "includes.h"
#include "test_sink.h"

#ifdef SPDLOG_POLYMORPHIC_ALLOCATORS

#include <memory_resource>

static_assert(sizeof(spdlog::logger) + sizeof(std::pmr::polymorphic_allocator<char>) <=
                  sizeof(spdlog::basic_logger<std::pmr::polymorphic_allocator<char>>),
              "Stateless allocator should not consume extra class space");
static_assert(sizeof(spdlog::details::thread_pool) +
                      sizeof(std::pmr::polymorphic_allocator<char>) <=
                  sizeof(spdlog::details::basic_thread_pool<std::pmr::polymorphic_allocator<char>>),
              "Stateless allocator should not consume extra class space");
static_assert(sizeof(spdlog::sinks::sink<spdlog::default_allocator_t>) +
                      sizeof(std::pmr::polymorphic_allocator<char>) <=
                  sizeof(spdlog::sinks::sink<std::pmr::polymorphic_allocator<char>>),
              "Stateless allocator should not consume extra class space");

struct test_mem_resource : std::pmr::memory_resource {
    size_t allocations = 0;
    size_t alloc_bytes = 0;
    std::function<void(size_t bytes, size_t alignment)> on_allocate;

private:
    void *do_allocate(size_t bytes, size_t alignment) override {
        allocations += 1;
        alloc_bytes += bytes;
        if (on_allocate) on_allocate(bytes, alignment);
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void *p, size_t bytes, size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(const memory_resource &other) const noexcept override {
        return this == &other;
    }
};

using allocator = std::pmr::polymorphic_allocator<char>;
using test_sink_pmr = spdlog::sinks::test_sink<std::mutex, allocator>;

void just_log(test_sink_pmr &test_sink, spdlog::basic_logger<allocator> &logger) {
    logger.set_pattern("%v");

    const char *some_message_1 = "Some message 1";
    logger.info(some_message_1);
    auto *disable_mem_check = std::pmr::set_default_resource(nullptr);
    CHECK(test_sink.lines().back() == some_message_1);
    std::pmr::set_default_resource(disable_mem_check);

    const char *some_message_2 =
        "with a long text argument to defeat small buffer optimization"
        "........................................................................";
    logger.info("Some message {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", some_message_2);
    disable_mem_check = std::pmr::set_default_resource(nullptr);
    CHECK(test_sink.lines().back().find(some_message_2) != std::string::npos);
    std::pmr::set_default_resource(disable_mem_check);
}

TEST_CASE("polymorphic allocators") {
    test_mem_resource mem_res_buf_fmt;
    test_mem_resource mem_res_data;
    allocator alloc_buf_fmt(&mem_res_buf_fmt);
    allocator alloc_data(&mem_res_data);
    mem_res_buf_fmt.on_allocate = [](size_t bytes, size_t alignment) {
        UNSCOPED_INFO("Buffer format alloc: " << bytes << ", " << alignment);
    };
    mem_res_data.on_allocate = [](size_t bytes, size_t alignment) {
        UNSCOPED_INFO("Data alloc: " << bytes << ", " << alignment);
    };

    auto test_sink = std::make_shared<test_sink_pmr>();
    auto logger = spdlog::basic_logger<allocator>(std::pmr::string("orig", alloc_data), test_sink,
                                                  alloc_buf_fmt, alloc_data);

    // control default mem res allocations
    test_mem_resource mem_res_default;

    SECTION("just log") {
        std::pmr::set_default_resource(&mem_res_default);
        just_log(*test_sink, logger);
        std::pmr::set_default_resource(nullptr);
    }

    SECTION("many sinks") {
        auto test_sink_2 = std::make_shared<test_sink_pmr>();
        std::pmr::set_default_resource(&mem_res_default);

        SECTION("create logger with initializer list of sinks") {
            const auto allocs_at_start = mem_res_data.allocations;
            auto logger_2 = spdlog::basic_logger<allocator>(
                std::pmr::string(
                    "a very long logger name to defeat small buffer optimization in std::string",
                    alloc_data),
                {test_sink, test_sink_2}, alloc_buf_fmt, alloc_data);
            CHECK(mem_res_data.allocations ==
                  2 + allocs_at_start);  // name once and vector of sinks once
            just_log(*test_sink_2, logger_2);
        }

        SECTION("create logger with vector of sinks") {
            const auto allocs_at_start = mem_res_data.allocations;
            auto logger_2 = spdlog::basic_logger<allocator>(
                std::pmr::string(
                    "a very long logger name to defeat small buffer optimization in std::string",
                    alloc_data),
                std::pmr::vector<spdlog::sink_ptr<allocator>>{{test_sink, test_sink_2}, alloc_data},
                alloc_buf_fmt, alloc_data);
            CHECK(mem_res_data.allocations ==
                  2 + allocs_at_start);  // name once and vector of sinks once
            just_log(*test_sink_2, logger_2);
        }

        std::pmr::set_default_resource(nullptr);
    }

    CHECK(mem_res_default.allocations == 0);
    CHECK(mem_res_default.alloc_bytes == 0);
    CHECK(mem_res_buf_fmt.allocations >= 2);  // in logger and in formatter
    CHECK(mem_res_buf_fmt.alloc_bytes > 0);
    CHECK(mem_res_data.allocations >= 1);
    CHECK(mem_res_data.alloc_bytes > 0);
}

TEST_CASE("polymorphic allocator copy-assign", "[pmr][assign]") {
    // destination keeps its own allocator after copy-assign
    test_mem_resource mem_res_src, mem_res_dst;
    allocator alloc_src(&mem_res_src), alloc_dst(&mem_res_dst);

    auto test_sink = std::make_shared<test_sink_pmr>();
    auto src = spdlog::basic_logger<allocator>(std::pmr::string("src", alloc_src), test_sink,
                                               alloc_src, alloc_src);
    src.set_level(spdlog::level::warn);
    src.flush_on(spdlog::level::err);

    auto dst =
        spdlog::basic_logger<allocator>(std::pmr::string("dst", alloc_dst), alloc_dst, alloc_dst);
    dst = src;

    // Allocator must NOT have propagated (POCCA=false)
    CHECK(dst.get_fmt_buf_allocator() == alloc_dst);
    CHECK(dst.get_fmt_buf_allocator() != alloc_src);

    // Data must be copied
    CHECK(dst.name() == "src");
    CHECK(dst.level() == spdlog::level::warn);
    CHECK(dst.flush_level() == spdlog::level::err);
    CHECK(dst.sinks() == src.sinks());
}

TEST_CASE("polymorphic allocator move-assign", "[pmr][assign]") {
    // destination keeps its own allocator after move-assign
    test_mem_resource mem_res_src, mem_res_dst;
    allocator alloc_src(&mem_res_src), alloc_dst(&mem_res_dst);

    auto test_sink = std::make_shared<test_sink_pmr>();
    auto src = spdlog::basic_logger<allocator>(std::pmr::string("src", alloc_src), test_sink,
                                               alloc_src, alloc_src);
    src.set_level(spdlog::level::warn);
    src.flush_on(spdlog::level::err);
    // Capture the sinks vector (same allocator type) before the move
    spdlog::basic_logger<allocator>::vector_type<spdlog::sink_ptr<allocator>> src_sinks =
        src.sinks();

    auto dst =
        spdlog::basic_logger<allocator>(std::pmr::string("dst", alloc_dst), alloc_dst, alloc_dst);
    dst = std::move(src);

    // Allocator must NOT have propagated (POCMA=false)
    CHECK(dst.get_fmt_buf_allocator() == alloc_dst);
    CHECK(dst.get_fmt_buf_allocator() != alloc_src);

    // Data must be moved
    CHECK(dst.name() == "src");
    CHECK(dst.level() == spdlog::level::warn);
    CHECK(dst.flush_level() == spdlog::level::err);
    CHECK(dst.sinks() == src_sinks);
}

TEST_CASE("polymorphic allocator swap equal allocators", "[pmr][swap]") {
    // Swap is only safe when allocators are equal; use the same memory resource for both loggers
    test_mem_resource mem_res;
    allocator alloc(&mem_res);

    auto test_sink_a = std::make_shared<test_sink_pmr>();
    auto test_sink_b = std::make_shared<test_sink_pmr>();
    auto a =
        spdlog::basic_logger<allocator>(std::pmr::string("a", alloc), test_sink_a, alloc, alloc);
    auto b =
        spdlog::basic_logger<allocator>(std::pmr::string("b", alloc), test_sink_b, alloc, alloc);
    a.set_level(spdlog::level::warn);
    b.set_level(spdlog::level::debug);

    a.swap(b);

    CHECK(a.name() == "b");
    CHECK(b.name() == "a");
    CHECK(a.level() == spdlog::level::debug);
    CHECK(b.level() == spdlog::level::warn);
    using sink_vec = spdlog::basic_logger<allocator>::vector_type<spdlog::sink_ptr<allocator>>;
    sink_vec expected_a(alloc), expected_b(alloc);
    expected_a.push_back(test_sink_b);
    expected_b.push_back(test_sink_a);
    CHECK(a.sinks() == expected_a);
    CHECK(b.sinks() == expected_b);
}

#endif
