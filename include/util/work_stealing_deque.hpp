#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace SAK {
namespace thread {

template <typename T>
class work_stealing_deque {
public:
    explicit work_stealing_deque(std::size_t initial_capacity = 32)
        : top_(0),
          bottom_(0),
          buffer_(std::make_shared<buffer>(normalize_capacity(initial_capacity))) {}

    void push_bottom(T value) {
        std::size_t bottom = bottom_.load(std::memory_order_relaxed);
        std::size_t top = top_.load(std::memory_order_acquire);
        std::shared_ptr<buffer> current = std::atomic_load_explicit(&buffer_, std::memory_order_acquire);

        if (bottom - top >= current->capacity - 1) {
            current = grow(current, top, bottom);
        }

        const std::size_t index = bottom & current->mask;
        std::atomic_store_explicit(&current->slots[index], std::make_shared<T>(std::move(value)), std::memory_order_relaxed);
        bottom_.store(bottom + 1, std::memory_order_release);
    }

    std::optional<T> pop_bottom() {
        std::size_t bottom = bottom_.load(std::memory_order_relaxed);
        if (bottom == 0) {
            return std::nullopt;
        }

        bottom -= 1;
        bottom_.store(bottom, std::memory_order_seq_cst);
        std::size_t top = top_.load(std::memory_order_seq_cst);

        if (top > bottom) {
            bottom_.store(bottom + 1, std::memory_order_relaxed);
            return std::nullopt;
        }

        std::shared_ptr<buffer> current = std::atomic_load_explicit(&buffer_, std::memory_order_acquire);
        const std::size_t index = bottom & current->mask;
        std::shared_ptr<T> item = std::atomic_load_explicit(&current->slots[index], std::memory_order_acquire);

        if (top == bottom) {
            if (!top_.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                bottom_.store(bottom + 1, std::memory_order_relaxed);
                return std::nullopt;
            }
            bottom_.store(bottom + 1, std::memory_order_relaxed);
        }

        std::atomic_store_explicit(&current->slots[index], std::shared_ptr<T>{}, std::memory_order_release);
        if (!item) {
            return std::nullopt;
        }
        return std::move(*item);
    }

    std::optional<T> steal_top() {
        std::size_t top = top_.load(std::memory_order_acquire);
        std::size_t bottom = bottom_.load(std::memory_order_acquire);

        if (top >= bottom) {
            return std::nullopt;
        }

        std::shared_ptr<buffer> current = std::atomic_load_explicit(&buffer_, std::memory_order_acquire);
        const std::size_t index = top & current->mask;
        std::shared_ptr<T> item = std::atomic_load_explicit(&current->slots[index], std::memory_order_acquire);

        if (!top_.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return std::nullopt;
        }

        std::atomic_store_explicit(&current->slots[index], std::shared_ptr<T>{}, std::memory_order_release);
        if (!item) {
            return std::nullopt;
        }
        return std::move(*item);
    }

    bool empty() const {
        return top_.load(std::memory_order_acquire) >= bottom_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const {
        return std::atomic_load_explicit(&buffer_, std::memory_order_acquire)->capacity;
    }

private:
    struct buffer {
        explicit buffer(std::size_t size)
            : capacity(size), mask(size - 1), slots(size) {}

        std::size_t capacity;
        std::size_t mask;
        std::vector<std::shared_ptr<T>> slots;
    };

    static std::size_t normalize_capacity(std::size_t requested) {
        std::size_t capacity = requested < 2 ? 2 : requested;
        std::size_t normalized = 1;
        while (normalized < capacity) {
            normalized <<= 1;
        }
        return normalized;
    }

    std::shared_ptr<buffer> grow(const std::shared_ptr<buffer>& current, std::size_t top, std::size_t bottom) {
        std::size_t new_capacity = current->capacity * 2;
        while (bottom - top >= new_capacity - 1) {
            new_capacity *= 2;
        }

        std::shared_ptr<buffer> expanded = std::make_shared<buffer>(new_capacity);
        for (std::size_t index = top; index < bottom; ++index) {
            const std::size_t old_slot = index & current->mask;
            const std::size_t new_slot = index & expanded->mask;
            std::shared_ptr<T> item = std::atomic_load_explicit(&current->slots[old_slot], std::memory_order_acquire);
            std::atomic_store_explicit(&expanded->slots[new_slot], item, std::memory_order_relaxed);
        }

        std::atomic_store_explicit(&buffer_, expanded, std::memory_order_release);
        return expanded;
    }

    std::atomic<std::size_t> top_;
    std::atomic<std::size_t> bottom_;
    std::shared_ptr<buffer> buffer_;
};

} // namespace thread
} // namespace SAK
