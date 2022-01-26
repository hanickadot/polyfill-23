#ifndef HANA23_UTILITY_MOVE_ONLY_FUNCTION_HPP
#define HANA23_UTILITY_MOVE_ONLY_FUNCTION_HPP

#include <type_traits>
#include <utility>
#include <concepts>

namespace hana23 {

using std::size_t;

// is in_place

constexpr inline size_t _move_only_function_buffer_size = sizeof(void *);

template <typename T> static constexpr bool _move_only_function_sbo_compatible = (sizeof(T) <= _move_only_function_buffer_size) && std::is_nothrow_move_constructible_v<T>;

using _move_only_function_storage_t = std::aligned_storage_t<_move_only_function_buffer_size>;

template <typename> struct _is_in_place_type_t: std::false_type { };
template <typename T> struct _is_in_place_type_t<std::in_place_type_t<T>>: std::true_type { };

template <typename T> concept _is_in_place_type_t_v = _is_in_place_type_t<T>::value;

// comparable to nullptr

template <typename T> concept _is_comparable_with_nullptr = requires(T obj) {
	{ obj == nullptr } -> std::same_as<bool>;
};

// is_invocable_from

template <typename F> struct _is_invocable;

template <typename R, typename... Args> struct _is_invocable<R(Args...)> {
	template <typename VT> static constexpr bool from_v = (std::is_invocable_r_v<R, VT, Args...> && std::is_invocable_r_v<R, VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) noexcept> {
	template <typename VT> static constexpr bool from_v = (std::is_nothrow_invocable_r_v<R, VT, Args...> && std::is_nothrow_invocable_r_v<R, VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) const> {
	template <typename VT> static constexpr bool from_v = (std::is_invocable_r_v<R, const VT, Args...> && std::is_invocable_r_v<R, const VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) const noexcept> {
	template <typename VT> static constexpr bool from_v = (std::is_nothrow_invocable_r_v<R, const VT, Args...> && std::is_nothrow_invocable_r_v<R, const VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) &> {
	template <typename VT> static constexpr bool from_v = (std::is_invocable_r_v<R, VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) & noexcept> {
	template <typename VT> static constexpr bool from_v = (std::is_nothrow_invocable_r_v<R, VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) const &> {
	template <typename VT> static constexpr bool from_v = (std::is_invocable_r_v<R, const VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) const & noexcept> {
	template <typename VT> static constexpr bool from_v = (std::is_nothrow_invocable_r_v<R, const VT &, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) &&> {
	template <typename VT> static constexpr bool from_v = (std::is_invocable_r_v<R, VT &&, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) && noexcept> {
	template <typename VT> static constexpr bool from_v = (std::is_nothrow_invocable_r_v<R, VT &&, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) const &&> {
	template <typename VT> static constexpr bool from_v = (std::is_invocable_r_v<R, const VT &&, Args...>);
};

template <typename R, typename... Args> struct _is_invocable<R(Args...) const && noexcept> {
	template <typename VT> static constexpr bool from_v = (std::is_nothrow_invocable_r_v<R, const VT &&, Args...>);
};

} // namespace hana23

#endif