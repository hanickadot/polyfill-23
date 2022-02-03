#ifndef HANA23_MOVE_ONLY_FUNCTION_HPP
#define HANA23_MOVE_ONLY_FUNCTION_HPP

#include "utility/move_only_function.hpp"
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <cassert>
#include <cstddef>

namespace hana23 {

template <typename T> class move_only_function;

// instance for R(Args...)   noexcept(false)

template <typename R, typename... Args> class move_only_function<R(Args...)   noexcept(false)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...)   noexcept(false)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t( storage_t & obj, Args... args) noexcept(false);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(false) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(false) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args)   noexcept(false) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...)   noexcept(true)

template <typename R, typename... Args> class move_only_function<R(Args...)   noexcept(true)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...)   noexcept(true)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t( storage_t & obj, Args... args) noexcept(true);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(true) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(true) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args)   noexcept(true) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...) const  noexcept(false)

template <typename R, typename... Args> class move_only_function<R(Args...) const  noexcept(false)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...) const  noexcept(false)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t(const storage_t & obj, Args... args) noexcept(false);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(false) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(false) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args) const  noexcept(false) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...) const  noexcept(true)

template <typename R, typename... Args> class move_only_function<R(Args...) const  noexcept(true)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...) const  noexcept(true)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t(const storage_t & obj, Args... args) noexcept(true);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(true) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(true) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args) const  noexcept(true) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...)  & noexcept(false)

template <typename R, typename... Args> class move_only_function<R(Args...)  & noexcept(false)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...)  & noexcept(false)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t( storage_t & obj, Args... args) noexcept(false);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(false) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(false) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args)  & noexcept(false) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...)  & noexcept(true)

template <typename R, typename... Args> class move_only_function<R(Args...)  & noexcept(true)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...)  & noexcept(true)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t( storage_t & obj, Args... args) noexcept(true);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(true) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(true) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args)  & noexcept(true) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...) const & noexcept(false)

template <typename R, typename... Args> class move_only_function<R(Args...) const & noexcept(false)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...) const & noexcept(false)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t(const storage_t & obj, Args... args) noexcept(false);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(false) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(false) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args) const & noexcept(false) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...) const & noexcept(true)

template <typename R, typename... Args> class move_only_function<R(Args...) const & noexcept(true)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...) const & noexcept(true)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t(const storage_t & obj, Args... args) noexcept(true);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(true) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(true) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args) const & noexcept(true) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...)  && noexcept(false)

template <typename R, typename... Args> class move_only_function<R(Args...)  && noexcept(false)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...)  && noexcept(false)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t( storage_t & obj, Args... args) noexcept(false);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(false) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(false) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args)  && noexcept(false) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...)  && noexcept(true)

template <typename R, typename... Args> class move_only_function<R(Args...)  && noexcept(true)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...)  && noexcept(true)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t( storage_t & obj, Args... args) noexcept(true);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(true) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = []( storage_t & obj, Args... args) noexcept(true) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast< Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args)  && noexcept(true) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...) const && noexcept(false)

template <typename R, typename... Args> class move_only_function<R(Args...) const && noexcept(false)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...) const && noexcept(false)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t(const storage_t & obj, Args... args) noexcept(false);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(false) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(false) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args) const && noexcept(false) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};


// instance for R(Args...) const && noexcept(true)

template <typename R, typename... Args> class move_only_function<R(Args...) const && noexcept(true)> {
	template <typename VT> static constexpr bool is_callable_from = hana23::_is_invocable<R(Args...) const && noexcept(true)>::template from_v<VT>;

	using storage_t = _move_only_function_storage_t;

	struct vtable_t {
		typedef R call_t(const storage_t & obj, Args... args) noexcept(true);
		typedef void move_construct_t(storage_t & destination, storage_t & source);
		typedef void destroy_t(storage_t & obj);

		call_t * call;
		move_construct_t * move_construct;
		destroy_t * destroy;
	};

	template <typename Callable> struct short_implementation: vtable_t {
		static_assert(sizeof(Callable) <= sizeof(storage_t));
		static_assert(std::is_nothrow_move_constructible_v<Callable>);

		static Callable * get_pointer(storage_t & input) noexcept {
			return static_cast<Callable *>(static_cast<void *>(&input));
		}

		static const Callable * get_pointer(const storage_t & input) noexcept {
			return static_cast<const Callable *>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) Callable(std::forward<CArgs>(args)...);
		}

		// these functions needs to be virtual
		constexpr short_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(true) -> R {
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) { new (&destination) Callable(std::move(*get_pointer(source))); },
			.destroy = [](storage_t & obj) { get_pointer(obj)->~Callable(); },
		} { }
	};

	template <typename Callable> struct allocating_implementation: vtable_t {
		using callable_ptr = Callable *;

		static callable_ptr & get_pointer(storage_t & input) noexcept {
			return *static_cast<Callable **>(static_cast<void *>(&input));
		}

		static const callable_ptr & get_pointer(const storage_t & input) noexcept {
			return *static_cast<const Callable **>(static_cast<const void *>(&input));
		}

		template <typename... CArgs> static void create_object_with(storage_t & storage, CArgs &&... args) {
			new (&storage) callable_ptr(new Callable(std::forward<CArgs>(args)...));
		}

		// these functions needs to be virtual
		constexpr allocating_implementation(): vtable_t{

			.call = [](const storage_t & obj, Args... args) noexcept(true) -> R {
				// it's UB to call moved-out function
				assert(get_pointer(obj) != nullptr);
				if constexpr (std::is_void_v<R>)
					std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...);
				else
					return std::invoke(static_cast<const Callable &>(*get_pointer(obj)), std::forward<Args>(args)...); },

			.move_construct = [](storage_t & destination, storage_t & source) {
				// it moves pointer owning Callable (no copy) to a new storage
				new (&destination) callable_ptr(get_pointer(source));
				// to avoid having two pointers referencing the same place, we need to overwrite rhs
				get_pointer(source) = nullptr; },

			.destroy = [](storage_t & obj) {
				// heap destruction
				delete get_pointer(obj);
				// and destroy storage of pointer (it doesn't destroy the object, only pointer lifetime)
				get_pointer(obj).~callable_ptr(); },
		} { }
	};

	template <typename Callable> static constexpr auto vtable_for = std::conditional_t<_move_only_function_sbo_compatible<Callable>, short_implementation<Callable>, allocating_implementation<Callable>>{};

	const vtable_t * vtable{nullptr};
	storage_t storage{};

	void release() noexcept {
		if (vtable) {
			vtable->destroy(storage);
			vtable = nullptr;
		}
	}

public:
	using result_type = R;

	move_only_function() noexcept = default;
	move_only_function(std::nullptr_t) noexcept { }

	move_only_function(move_only_function && other) noexcept: vtable{other.vtable} {
		if (vtable) {
			vtable->move_construct(storage, other.storage);
		}
	}

	move_only_function(const move_only_function &) = delete;

	template <typename F> move_only_function(F && f) requires(is_callable_from<std::decay_t<F>> && !std::is_same_v<std::remove_cvref_t<F>, move_only_function> && !hana23::_is_in_place_type_t_v<std::remove_cvref_t<F>>) {
		static_assert(std::is_constructible_v<std::decay_t<F>, F>);

		// empty function pointers and move_only_functions should be empty
		if constexpr (_is_comparable_with_nullptr<std::decay_t<F>>) {
			if (f == nullptr) {
				return;
			}
		}

		// init after check
		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));
	}

	template <typename T, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, std::forward<CArgs>(args)...);
	}

	template <typename T, typename U, class... CArgs> explicit move_only_function(std::in_place_type_t<T>, std::initializer_list<U> il, CArgs &&... args) requires(std::is_constructible_v<std::decay_t<T>, std::initializer_list<U> &, CArgs...> && is_callable_from<std::decay_t<T>>): vtable{&vtable_for<std::decay_t<T>>} {
		static_assert(std::is_same_v<std::decay_t<T>, T>);
		vtable_for<std::decay_t<T>>.create_object_with(storage, il, std::forward<CArgs>(args)...);
	}

	move_only_function & operator=(move_only_function && rhs) {
		release();

		if (rhs.vtable) {
			rhs.vtable->move_construct(storage, rhs.storage);
			vtable = rhs.vtable;
		}

		return *this;
	}

	move_only_function & operator=(const move_only_function &) = delete;

	move_only_function & operator=(std::nullptr_t) noexcept {
		release();

		return *this;
	}

	template <class F> move_only_function & operator=(F && f) {
		release();

		vtable = &vtable_for<std::decay_t<F>>;
		vtable_for<std::decay_t<F>>.create_object_with(storage, std::forward<F>(f));

		return *this;
	}

	void swap(move_only_function & other) noexcept {
		move_only_function tmp = std::move(*this);
		*this = std::move(other);
		other = std::move(tmp);
	}

	explicit operator bool() const noexcept {
		return vtable;
	}

	R operator()(Args... args) const && noexcept(true) {
		// it's UB to call destroyed object
		assert(vtable != nullptr);

		return vtable->call(storage, std::forward<Args>(args)...);
	}

	~move_only_function() {
		if (vtable) {
			vtable->destroy(storage);
		}
	}

	friend void swap(move_only_function & lhs, move_only_function & rhs) noexcept {
		lhs.swap(rhs);
	}

	friend bool operator==(const move_only_function & f, std::nullptr_t) noexcept {
		return f.operator bool();
	}
};




} // namespace hana23

#endif
