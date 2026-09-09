#pragma once

#include <cstddef>
#include <type_traits>

// Stateful tracking allocator for testing allocator propagation behaviour
template <class T,
          bool POCCA,  // propagate_on_container_copy_assignment
          bool POCMA,  // propagate_on_container_move_assignment
          bool POCS>   // propagate_on_container_swap
struct tracking_allocator {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::integral_constant<bool, POCCA>;
    using propagate_on_container_move_assignment = std::integral_constant<bool, POCMA>;
    using propagate_on_container_swap = std::integral_constant<bool, POCS>;
    using is_always_equal = std::false_type;

    int id;  // identity: two allocators are equal iff their ids match

    explicit tracking_allocator(int i = 0)
        : id(i) {}

    // Required rebind so the allocator can be rebound to other value types.
    template <class U>
    struct rebind {
        using other = tracking_allocator<U, POCCA, POCMA, POCS>;
    };

    template <class U>
    tracking_allocator(const tracking_allocator<U, POCCA, POCMA, POCS> &o)
        : id(o.id) {}

    T *allocate(std::size_t n) { return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) noexcept { ::operator delete(p); }

    friend bool operator==(const tracking_allocator &lhs, const tracking_allocator &rhs) noexcept {
        return lhs.id == rhs.id;
    }
    friend bool operator!=(const tracking_allocator &lhs, const tracking_allocator &rhs) noexcept {
        return lhs.id != rhs.id;
    }
};

// Convenience aliases
using alloc_no_prop = tracking_allocator<char, false, false, false>;
using alloc_poc_copy = tracking_allocator<char, true, false, false>;
using alloc_poc_move = tracking_allocator<char, false, true, false>;
using alloc_poc_swap = tracking_allocator<char, false, false, true>;
