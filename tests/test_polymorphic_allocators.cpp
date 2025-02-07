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

private:
    void *do_allocate(size_t bytes, size_t alignment) override {
        allocations += 1;
        alloc_bytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void *p, size_t bytes, size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(const memory_resource &other) const noexcept override {
        return dynamic_cast<const test_mem_resource *>(&other) != nullptr;
    }
};

TEST_CASE("polymorphic allocators") {
    test_mem_resource mem_res;
    std::pmr::polymorphic_allocator<char> alloc(&mem_res);

    using test_sink_pmr =
        spdlog::sinks::test_sink<std::mutex, std::pmr::polymorphic_allocator<char>>;
    auto test_sink = std::make_shared<test_sink_pmr>();
    auto logger =
        spdlog::basic_logger<std::pmr::polymorphic_allocator<char>>("orig", test_sink, alloc);
    logger.set_pattern("%v");

    SECTION("just log") {
        logger.info("Some message 1");
        CHECK(test_sink->lines() ==
              std::vector<test_sink_pmr::string, test_sink_pmr::string_alloc>{{"Some message 1"}});
        logger.info(
            "Some message {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}",
            "with a long text "
            "argument........................................................................");
    }

    CHECK(mem_res.allocations >= 2);  // in logger and in formatter
    CHECK(mem_res.alloc_bytes > 0);
}

#endif
