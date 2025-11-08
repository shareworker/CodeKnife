#pragma once

#include <any>
#include <vector>
#include <functional>
#include <utility>
#include <type_traits>

namespace SAK {

class CObject;

// 0-arity
template <typename R, typename Class>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)()) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (!args.empty()) return std::any{};
        Class* instance = static_cast<Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)();
            return std::any{};
        } else {
            R ret = (instance->*method)();
            return std::any(std::move(ret));
        }
    };
}

template <typename R, typename Class>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)() const) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (!args.empty()) return std::any{};
        const Class* instance = static_cast<const Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)();
            return std::any{};
        } else {
            R ret = (instance->*method)();
            return std::any(std::move(ret));
        }
    };
}

// 1-arity
template <typename R, typename Class, typename A1>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)(A1)) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (args.size() != 1) return std::any{};
        Class* instance = static_cast<Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)(std::any_cast<A1>(args[0]));
            return std::any{};
        } else {
            R ret = (instance->*method)(std::any_cast<A1>(args[0]));
            return std::any(std::move(ret));
        }
    };
}

template <typename R, typename Class, typename A1>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)(A1) const) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (args.size() != 1) return std::any{};
        const Class* instance = static_cast<const Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)(std::any_cast<A1>(args[0]));
            return std::any{};
        } else {
            R ret = (instance->*method)(std::any_cast<A1>(args[0]));
            return std::any(std::move(ret));
        }
    };
}

// 2-arity
template <typename R, typename Class, typename A1, typename A2>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)(A1, A2)) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (args.size() != 2) return std::any{};
        Class* instance = static_cast<Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]));
            return std::any{};
        } else {
            R ret = (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]));
            return std::any(std::move(ret));
        }
    };
}

template <typename R, typename Class, typename A1, typename A2>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)(A1, A2) const) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (args.size() != 2) return std::any{};
        const Class* instance = static_cast<const Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]));
            return std::any{};
        } else {
            R ret = (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]));
            return std::any(std::move(ret));
        }
    };
}

// 3-arity (extendable)
template <typename R, typename Class, typename A1, typename A2, typename A3>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)(A1, A2, A3)) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (args.size() != 3) return std::any{};
        Class* instance = static_cast<Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]), std::any_cast<A3>(args[2]));
            return std::any{};
        } else {
            R ret = (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]), std::any_cast<A3>(args[2]));
            return std::any(std::move(ret));
        }
    };
}

template <typename R, typename Class, typename A1, typename A2, typename A3>
std::function<std::any(CObject*, const std::vector<std::any>&)>
make_invoker(R (Class::*method)(A1, A2, A3) const) {
    return [method](CObject* obj, const std::vector<std::any>& args) -> std::any {
        if (args.size() != 3) return std::any{};
        const Class* instance = static_cast<const Class*>(obj);
        if constexpr (std::is_void_v<R>) {
            (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]), std::any_cast<A3>(args[2]));
            return std::any{};
        } else {
            R ret = (instance->*method)(std::any_cast<A1>(args[0]), std::any_cast<A2>(args[1]), std::any_cast<A3>(args[2]));
            return std::any(std::move(ret));
        }
    };
}

} // namespace SAK
