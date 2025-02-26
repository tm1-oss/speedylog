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
        return dynamic_cast<const test_mem_resource *>(&other) != nullptr;
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

#endif
