#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

template <class Signature>
class Callback;

/**
 * Non-allocating stand-in for std::function.
 *
 * Holds a small, trivially copyable callable (typically a `[this]` lambda or a
 * plain function) inline. Anything bigger fails to compile, so it can never
 * fall back to the heap the way std::function may.
 */
template <class R, class... Args>
class Callback<R(Args...)> {
public:
    Callback() = default;

    template <class F>
        requires(!std::same_as<F, Callback> && std::is_invocable_r_v<R, const F&, Args...>)
    Callback(F f) noexcept  // implicit, so call sites can pass a lambda directly
        : invoke_{[](const void* callable, Args... args) -> R {
              return (*std::launder(static_cast<const F*>(callable)))(std::forward<Args>(args)...);
          }} {
        static_assert(sizeof(F) <= sizeof(storage_), "callable too big: capture one pointer at most");
        static_assert(alignof(F) <= alignof(void*), "callable is over-aligned");
        static_assert(std::is_trivially_copyable_v<F> && std::is_trivially_destructible_v<F>,
                      "callable must be trivially copyable and destructible");
        ::new (storage_.data()) F(f);
    }

    explicit operator bool() const noexcept { return invoke_ != nullptr; }

    /** Calling an empty Callback is undefined: check it first if it may be empty. */
    R operator()(Args... args) const { return invoke_(storage_.data(), std::forward<Args>(args)...); }

private:
    R (*invoke_)(const void*, Args...) = nullptr;
    alignas(void*) std::array<std::byte, sizeof(void*)> storage_{};
};
