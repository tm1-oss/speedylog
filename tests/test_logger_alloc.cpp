// Tests for basic_logger copy/move assignment and swap with stateful allocators.
// Covers allocator propagation traits (POCCA, POCMA, POCS).
#include "includes.h"
#include "tracking_allocator.h"

// Helper: build a dummy basic_logger<Alloc> with no sinks and a known name
template <class Alloc>
static spdlog::basic_logger<Alloc> make_logger(const char *name, Alloc alloc) {
    using string_type = typename spdlog::basic_logger<Alloc>::string_type;
    return spdlog::basic_logger<Alloc>(string_type(name, alloc), alloc, alloc);
}

TEST_CASE("copy-assign POCCA=false keeps destination allocator", "[alloc][assign]") {
    alloc_no_prop alloc_src(1);
    alloc_no_prop alloc_dst(2);

    auto src = make_logger("src", alloc_src);
    src.set_level(spdlog::level::warn);
    src.flush_on(spdlog::level::err);

    auto dst = make_logger("dst", alloc_dst);
    dst = src;

    // Allocator of dst must NOT change (POCCA=false)
    CHECK(dst.get_fmt_buf_allocator().id == 2);

    // Data fields must be copied from src
    CHECK(dst.name() == "src");
    CHECK(dst.level() == spdlog::level::warn);
    CHECK(dst.flush_level() == spdlog::level::err);
    CHECK(dst.sinks() == src.sinks());
}

TEST_CASE("copy-assign POCCA=true propagates allocator", "[alloc][assign]") {
    alloc_poc_copy alloc_src(1);
    alloc_poc_copy alloc_dst(2);

    auto src = make_logger("src", alloc_src);
    src.set_level(spdlog::level::warn);

    auto dst = make_logger("dst", alloc_dst);
    dst = src;

    // Allocator must have propagated from src (id==1)
    CHECK(dst.get_fmt_buf_allocator().id == 1);

    // Data fields must be copied
    CHECK(dst.name() == "src");
    CHECK(dst.level() == spdlog::level::warn);
    CHECK(dst.sinks() == src.sinks());
}

TEST_CASE("move-assign POCMA=true propagates allocator", "[alloc][assign]") {
    alloc_poc_move alloc_src(1);
    alloc_poc_move alloc_dst(2);

    auto src = make_logger("src", alloc_src);
    src.set_level(spdlog::level::warn);
    src.flush_on(spdlog::level::err);
    auto src_sinks = src.sinks();

    auto dst = make_logger("dst", alloc_dst);
    dst = std::move(src);

    // Allocator must have propagated from src (id==1)
    CHECK(dst.get_fmt_buf_allocator().id == 1);

    // Data fields must be moved
    CHECK(dst.name() == "src");
    CHECK(dst.level() == spdlog::level::warn);
    CHECK(dst.flush_level() == spdlog::level::err);
    CHECK(dst.sinks() == src_sinks);
}

TEST_CASE("move-assign POCMA=false keeps destination allocator", "[alloc][assign]") {
    alloc_no_prop alloc_src(1);
    alloc_no_prop alloc_dst(2);

    auto src = make_logger("src", alloc_src);
    src.set_level(spdlog::level::warn);
    src.flush_on(spdlog::level::err);
    auto src_sinks = src.sinks();

    auto dst = make_logger("dst", alloc_dst);
    dst = std::move(src);

    // Allocator of dst must NOT change (POCMA=false)
    CHECK(dst.get_fmt_buf_allocator().id == 2);

    // Data fields must still be moved
    CHECK(dst.name() == "src");
    CHECK(dst.level() == spdlog::level::warn);
    CHECK(dst.flush_level() == spdlog::level::err);
    CHECK(dst.sinks() == src_sinks);
}
