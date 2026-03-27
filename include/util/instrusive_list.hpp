#pragma once

#include <cstddef>
#include <type_traits>

namespace SAK {

struct InstrusiveListNode {
    InstrusiveListNode* next{};
    InstrusiveListNode* prev{};
};

template<typename List, typename T>
class InstrusiveListBase {
    InstrusiveListNode my_head;
    std::size_t my_size{0};

    static InstrusiveListNode& node(T& item) { return List::node(item); }
    static T& item(InstrusiveListNode* node) { return List::item(node); }
    static const T& item(const InstrusiveListNode* node) { return List::item(node); }

    template<typename DereferenceType>
    class iterator_impl {
        static_assert(std::is_same<DereferenceType, T>::value ||
            std::is_same<DereferenceType, const T>::value, "Incorrect DereferenceType in iterator_impl");
        using pointer_type = typename std::conditional<std::is_same<DereferenceType, T>::value,
                                                       InstrusiveListNode*,
                                                       const InstrusiveListNode*>::type;
    public:
        iterator_impl() : my_pos(nullptr) {}
        iterator_impl(pointer_type pos) : my_pos(pos) {}
        iterator_impl& operator++() { 
            my_pos = my_pos->next;
            return *this;
        }

        iterator_impl operator++(int) {
            iterator_impl tmp(*this);
            ++*this;
            return tmp;
        }

        iterator_impl& operator--() {
            my_pos = my_pos->prev;
            return *this;
        }

        iterator_impl operator--(int) {
            iterator_impl tmp(*this);
            --*this;
            return tmp;
        }

        bool operator==(const iterator_impl& rhs) const {
            return my_pos == rhs.my_pos;
        }

        bool operator!=(const iterator_impl& rhs) const {
            return my_pos != rhs.my_pos;
        }

        DereferenceType& operator*() const {
            return List::item(my_pos);
        }

        DereferenceType* operator->() const {
            return &List::item(my_pos);
        }
        
    private:
        pointer_type my_pos;
    };

public:
    using iterator = iterator_impl<T>;
    using const_iterator = iterator_impl<const T>;

    InstrusiveListBase() : my_size(0) {
        my_head.prev = &my_head;
        my_head.next = &my_head;
    }

    bool Empty() const { return my_head.next == &my_head; }
    std::size_t Size() const { return my_size; }

    iterator Begin() { return iterator(my_head.next); }
    iterator End() { return iterator(&my_head); }
    const_iterator Begin() const { return const_iterator(my_head.next); }
    const_iterator End() const { return const_iterator(&my_head); }

    void PushFront(T& val) {
        node(val).prev = &my_head;
        node(val).next = my_head.next;
        my_head.next->prev = &node(val);
        my_head.next = &node(val);
        ++my_size;
    }

    void Remove(T& val) {
        node(val).prev->next = node(val).next;
        node(val).next->prev = node(val).prev;
        --my_size;
    }

    iterator Erase(iterator it) {
        T& val = *it;
        ++it;
        Remove(val);
        return it;
    }
};

template<typename T, typename U, InstrusiveListNode U::*NodePtr>
class MemptrInstrusiveList : public InstrusiveListBase<MemptrInstrusiveList<T, U, NodePtr>, T> {
    friend class InstrusiveListBase<MemptrInstrusiveList<T, U, NodePtr>, T>;

    static InstrusiveListNode& node(T& val) { return val.*NodePtr; }

    static T& item(InstrusiveListNode* node) {
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(node) -
            (static_cast<std::ptrdiff_t>(reinterpret_cast<char*>(&(reinterpret_cast<T*>(0x1000)->*NodePtr)) - reinterpret_cast<char*>(0x1000))));
    }

    static const T& item(const InstrusiveListNode* node) {
        return item(const_cast<InstrusiveListNode*>(node));
    }
};

template<typename T>
class InstrusiveList : public InstrusiveListBase<InstrusiveList<T>, T> {
    friend class InstrusiveListBase<InstrusiveList<T>, T>;

    static InstrusiveListNode& node(T& val) { return val; }

    static T& item(InstrusiveListNode* node) {
        return *static_cast<T*>(node);
    }

    static const T& item(const InstrusiveListNode* node) {
        return *static_cast<const T*>(node);
    }
};

}
