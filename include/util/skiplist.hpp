#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <random>
#include <type_traits>
#include <utility>

namespace SAK {

/// ConcurrentSkipList: Lock-free concurrent skip list with unsafe modification operations.
///
/// Concurrency Contract:
/// - insert/emplace/find/contains/lower_bound/upper_bound: Safe for concurrent access
/// - unsafe_erase/unsafe_clear/unsafe_swap: NOT safe with concurrent readers/writers
///   Caller must ensure exclusive access (no concurrent insert/find/iterate) when calling
///   these operations to avoid use-after-free and data races.
/// - Destructor calls unsafe_clear() and assumes no concurrent access during destruction
template <typename Key, typename Value, typename Compare = std::less<Key>>
class ConcurrentSkipList {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;

private:
    static constexpr size_type MAX_LEVEL = 16;

    struct Node {
        using atomic_node_ptr = std::atomic<Node*>;

        template <typename... Args>
        explicit Node(size_type height, Args&&... args)
            : value(std::forward<Args>(args)...), height_(height) {}

        value_type value;
        size_type height_;

        size_type height() const {
            return height_;
        }

        atomic_node_ptr& atomic_next(size_type level) {
            return reinterpret_cast<atomic_node_ptr*>(this + 1)[level];
        }

        const atomic_node_ptr& atomic_next(size_type level) const {
            return reinterpret_cast<const atomic_node_ptr*>(this + 1)[level];
        }

        Node* next(size_type level) const {
            return atomic_next(level).load(std::memory_order_acquire);
        }

        void set_next(size_type level, Node* node) {
            atomic_next(level).store(node, std::memory_order_relaxed);
        }
    };

    struct HeadNode {
        std::array<std::atomic<Node*>, MAX_LEVEL> next{};

        HeadNode() {
            for (auto& entry : next) {
                entry.store(nullptr, std::memory_order_relaxed);
            }
        }

        size_type height() const {
            return MAX_LEVEL;
        }

        std::atomic<Node*>& atomic_next(size_type level) {
            return next[level];
        }

        const std::atomic<Node*>& atomic_next(size_type level) const {
            return next[level];
        }

        Node* next_at(size_type level) const {
            return next[level].load(std::memory_order_acquire);
        }

        void set_next(size_type level, Node* node) {
            next[level].store(node, std::memory_order_relaxed);
        }
    };

    template <typename NodeType, typename ValueRef>
    class BasicIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = typename ConcurrentSkipList::value_type;
        using difference_type = typename ConcurrentSkipList::difference_type;
        using reference = ValueRef;
        using pointer = std::add_pointer_t<std::remove_reference_t<ValueRef>>;

        BasicIterator() : node_(nullptr) {}
        explicit BasicIterator(NodeType* node) : node_(node) {}

        template <typename OtherNodeType, typename OtherValueRef,
                  typename = std::enable_if_t<std::is_convertible<OtherNodeType*, NodeType*>::value>>
        BasicIterator(const BasicIterator<OtherNodeType, OtherValueRef>& other) : node_(other.node()) {}

        reference operator*() const {
            return node_->value;
        }

        pointer operator->() const {
            return &node_->value;
        }

        BasicIterator& operator++() {
            node_ = node_ ? node_->next(0) : nullptr;
            return *this;
        }

        BasicIterator operator++(int) {
            BasicIterator copy(*this);
            ++(*this);
            return copy;
        }

        bool operator==(const BasicIterator& other) const {
            return node_ == other.node_;
        }

        bool operator!=(const BasicIterator& other) const {
            return !(*this == other);
        }

        NodeType* node() const {
            return node_;
        }

    private:
        NodeType* node_;
    };

public:
    using iterator = BasicIterator<Node, value_type&>;
    using const_iterator = BasicIterator<const Node, const value_type&>;

    explicit ConcurrentSkipList(const Compare& compare = Compare())
        : compare_(compare), size_(0), max_height_(1) {}

    ConcurrentSkipList(const ConcurrentSkipList&) = delete;
    ConcurrentSkipList& operator=(const ConcurrentSkipList&) = delete;

    ConcurrentSkipList(ConcurrentSkipList&& other) noexcept {
        move_from(std::move(other));
    }

    ConcurrentSkipList& operator=(ConcurrentSkipList&& other) noexcept {
        if (this != &other) {
            unsafe_clear();
            move_from(std::move(other));
        }
        return *this;
    }

    ~ConcurrentSkipList() {
        unsafe_clear();
    }

    std::pair<iterator, bool> insert(const value_type& value) {
        return emplace(value.first, value.second);
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        return emplace(std::move(value.first), std::move(value.second));
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        Node* new_node = create_node(random_level(), std::forward<Args>(args)...);
        std::array<Node*, MAX_LEVEL> prev_nodes{};
        std::array<Node*, MAX_LEVEL> next_nodes{};

        for (;;) {
            bool found_existing = find_path(new_node->value.first, prev_nodes, next_nodes);
            if (found_existing) {
                Node* existing = next_nodes[0];
                destroy_node(new_node);
                return { iterator(existing), false };
            }

            for (size_type level = 0; level < new_node->height(); ++level) {
                new_node->set_next(level, next_nodes[level]);
            }

            Node* expected = next_nodes[0];
            if (!link_after(prev_nodes[0], 0, expected, new_node)) {
                continue;
            }

            publish_max_height(new_node->height());

            for (size_type level = 1; level < new_node->height(); ++level) {
                for (;;) {
                    expected = next_nodes[level];
                    if (link_after(prev_nodes[level], level, expected, new_node)) {
                        break;
                    }
                    find_path(new_node->value.first, prev_nodes, next_nodes);
                    new_node->set_next(level, next_nodes[level]);
                }
            }

            size_.fetch_add(1, std::memory_order_release);
            return { iterator(new_node), true };
        }
    }

    iterator find(const key_type& key) {
        return iterator(find_node(key));
    }

    const_iterator find(const key_type& key) const {
        return const_iterator(find_node(key));
    }

    bool contains(const key_type& key) {
        return find(key) != end();
    }

    bool contains(const key_type& key) const {
        return find(key) != end();
    }

    size_type count(const key_type& key) {
        return contains(key) ? 1u : 0u;
    }

    size_type count(const key_type& key) const {
        return contains(key) ? 1u : 0u;
    }

    iterator lower_bound(const key_type& key) {
        return iterator(lower_bound_node(key));
    }

    const_iterator lower_bound(const key_type& key) const {
        return const_iterator(lower_bound_node(key));
    }

    iterator upper_bound(const key_type& key) {
        return iterator(upper_bound_node(key));
    }

    const_iterator upper_bound(const key_type& key) const {
        return const_iterator(upper_bound_node(key));
    }

    std::pair<iterator, iterator> equal_range(const key_type& key) {
        return { lower_bound(key), upper_bound(key) };
    }

    std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const {
        return { lower_bound(key), upper_bound(key) };
    }

    iterator begin() {
        return iterator(head_.next_at(0));
    }

    const_iterator begin() const {
        return const_iterator(head_.next_at(0));
    }

    const_iterator cbegin() const {
        return const_iterator(head_.next_at(0));
    }

    iterator end() {
        return iterator(nullptr);
    }

    const_iterator end() const {
        return const_iterator(nullptr);
    }

    const_iterator cend() const {
        return const_iterator(nullptr);
    }

    size_type unsafe_erase(const key_type& key) {
        std::lock_guard<std::mutex> lock(erase_mutex_);
        std::array<Node*, MAX_LEVEL> prev_nodes{};
        std::array<Node*, MAX_LEVEL> next_nodes{};
        if (!find_path(key, prev_nodes, next_nodes)) {
            return 0;
        }

        Node* node = next_nodes[0];
        for (size_type level = 0; level < node->height(); ++level) {
            set_next_after(prev_nodes[level], level, node->next(level));
        }

        size_.fetch_sub(1, std::memory_order_release);
        trim_max_height();
        destroy_node(node);
        return 1;
    }

    iterator unsafe_erase(iterator pos) {
        if (pos == end()) {
            return end();
        }
        Node* next = pos.node()->next(0);
        unsafe_erase(pos->first);
        return iterator(next);
    }

    iterator unsafe_erase(const_iterator pos) {
        if (pos == end()) {
            return end();
        }
        Node* next = pos.node()->next(0);
        unsafe_erase(pos->first);
        return iterator(next);
    }

    iterator unsafe_erase(const_iterator first, const_iterator last) {
        auto current = first;
        while (current != last) {
            current = unsafe_erase(current);
        }
        return iterator(const_cast<Node*>(current.node()));
    }

    void unsafe_clear() noexcept {
        std::lock_guard<std::mutex> lock(erase_mutex_);
        Node* current = head_.next_at(0);
        while (current) {
            Node* next = current->next(0);
            destroy_node(current);
            current = next;
        }

        for (size_type level = 0; level < MAX_LEVEL; ++level) {
            head_.set_next(level, nullptr);
        }

        size_.store(0, std::memory_order_relaxed);
        max_height_.store(1, std::memory_order_relaxed);
    }

    size_type size() const {
        return size_.load(std::memory_order_acquire);
    }

    bool empty() const {
        return size() == 0;
    }

    size_type max_size() const {
        return std::numeric_limits<size_type>::max() / sizeof(Node);
    }

    void unsafe_swap(ConcurrentSkipList& other) {
        if (this == &other) {
            return;
        }

        std::lock(erase_mutex_, other.erase_mutex_);
        std::lock_guard<std::mutex> left_lock(erase_mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> right_lock(other.erase_mutex_, std::adopt_lock);

        for (size_type level = 0; level < MAX_LEVEL; ++level) {
            Node* this_next = head_.next[level].load(std::memory_order_relaxed);
            Node* other_next = other.head_.next[level].load(std::memory_order_relaxed);
            head_.next[level].store(other_next, std::memory_order_relaxed);
            other.head_.next[level].store(this_next, std::memory_order_relaxed);
        }
        std::swap(compare_, other.compare_);
        swap_atomic(size_, other.size_);
        swap_atomic(max_height_, other.max_height_);
    }

private:
    template <typename Atomic>
    static void swap_atomic(Atomic& lhs, Atomic& rhs) {
        auto lhs_value = lhs.load(std::memory_order_relaxed);
        auto rhs_value = rhs.load(std::memory_order_relaxed);
        lhs.store(rhs_value, std::memory_order_relaxed);
        rhs.store(lhs_value, std::memory_order_relaxed);
    }

    static size_type node_allocation_size(size_type height) {
        return sizeof(Node) + sizeof(typename Node::atomic_node_ptr) * height;
    }

    template <typename... Args>
    static Node* create_node(size_type height, Args&&... args) {
        void* storage = ::operator new(node_allocation_size(height));
        Node* node = new (storage) Node(height, std::forward<Args>(args)...);
        for (size_type level = 0; level < height; ++level) {
            new (&node->atomic_next(level)) typename Node::atomic_node_ptr(nullptr);
        }
        return node;
    }

    static void destroy_node(Node* node) noexcept {
        if (!node) {
            return;
        }
        for (size_type level = 0; level < node->height(); ++level) {
            node->atomic_next(level).~atomic<Node*>();
        }
        node->~Node();
        ::operator delete(node);
    }

    bool link_after(Node* prev, size_type level, Node*& expected, Node* desired) {
        if (prev == nullptr) {
            return head_.atomic_next(level).compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
        }
        return prev->atomic_next(level).compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void set_next_after(Node* prev, size_type level, Node* next) {
        if (prev == nullptr) {
            head_.set_next(level, next);
            return;
        }
        prev->set_next(level, next);
    }

    static bool keys_equal(const Compare& compare, const key_type& lhs, const key_type& rhs) {
        return !compare(lhs, rhs) && !compare(rhs, lhs);
    }

    size_type random_level() const {
        thread_local std::minstd_rand generator(std::random_device{}());
        thread_local std::uniform_int_distribution<int> distribution(0, 3);

        size_type level = 1;
        while (level < MAX_LEVEL && distribution(generator) == 0) {
            ++level;
        }
        return level;
    }

    void publish_max_height(size_type new_height) {
        size_type observed = max_height_.load(std::memory_order_acquire);
        while (observed < new_height &&
               !max_height_.compare_exchange_weak(observed, new_height, std::memory_order_acq_rel, std::memory_order_acquire)) {
        }
    }

    void trim_max_height() {
        size_type current = max_height_.load(std::memory_order_relaxed);
        while (current > 1 && head_.next_at(current - 1) == nullptr) {
            --current;
        }
        max_height_.store(current, std::memory_order_relaxed);
    }

    bool find_path(const key_type& key, std::array<Node*, MAX_LEVEL>& prev_nodes, std::array<Node*, MAX_LEVEL>& next_nodes) const {
        Node* prev = nullptr;
        size_type current_height = max_height_.load(std::memory_order_acquire);

        for (size_type level = current_height; level > 0; --level) {
            Node* current = prev ? prev->next(level - 1) : head_.next_at(level - 1);
            while (current && compare_(current->value.first, key)) {
                prev = current;
                current = current->next(level - 1);
            }
            prev_nodes[level - 1] = prev;
            next_nodes[level - 1] = current;
        }

        for (size_type level = current_height; level < MAX_LEVEL; ++level) {
            prev_nodes[level] = nullptr;
            next_nodes[level] = nullptr;
        }

        return next_nodes[0] && keys_equal(compare_, next_nodes[0]->value.first, key);
    }

    Node* find_node(const key_type& key) const {
        std::array<Node*, MAX_LEVEL> prev_nodes{};
        std::array<Node*, MAX_LEVEL> next_nodes{};
        return find_path(key, prev_nodes, next_nodes) ? next_nodes[0] : nullptr;
    }

    Node* lower_bound_node(const key_type& key) const {
        Node* prev = nullptr;
        size_type current_height = max_height_.load(std::memory_order_acquire);
        for (size_type level = current_height; level > 0; --level) {
            Node* current = prev ? prev->next(level - 1) : head_.next_at(level - 1);
            while (current && compare_(current->value.first, key)) {
                prev = current;
                current = current->next(level - 1);
            }
        }
        return prev ? prev->next(0) : head_.next_at(0);
    }

    Node* upper_bound_node(const key_type& key) const {
        Node* prev = nullptr;
        size_type current_height = max_height_.load(std::memory_order_acquire);
        for (size_type level = current_height; level > 0; --level) {
            Node* current = prev ? prev->next(level - 1) : head_.next_at(level - 1);
            while (current && !compare_(key, current->value.first)) {
                prev = current;
                current = current->next(level - 1);
            }
        }
        return prev ? prev->next(0) : head_.next_at(0);
    }

    void move_from(ConcurrentSkipList&& other) {
        compare_ = std::move(other.compare_);
        for (size_type level = 0; level < MAX_LEVEL; ++level) {
            head_.set_next(level, other.head_.next[level].load(std::memory_order_relaxed));
            other.head_.set_next(level, nullptr);
        }
        size_.store(other.size_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        max_height_.store(other.max_height_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.size_.store(0, std::memory_order_relaxed);
        other.max_height_.store(1, std::memory_order_relaxed);
    }

    HeadNode head_;
    Compare compare_{};
    std::atomic<size_type> size_;
    std::atomic<size_type> max_height_;
    mutable std::mutex erase_mutex_;
};

template <typename Key, typename Value, typename Compare = std::less<Key>>
using SkipList = ConcurrentSkipList<Key, Value, Compare>;

} // namespace SAK
