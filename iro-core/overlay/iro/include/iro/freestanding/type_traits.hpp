// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/cstddef.hpp>

/**
 * @file type_traits.hpp
 * @brief Minimal builtin-backed trait wrappers required by IRO Core.
 */

namespace iro::freestanding {

template<class T, T v>
struct integral_constant {
  static constexpr T value = v;
  using value_type = T;
  using type = integral_constant;
  constexpr operator value_type() const noexcept { return value; }
  constexpr value_type operator()() const noexcept { return value; }
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template<bool B, class T = void>
struct enable_if {};

template<class T>
struct enable_if<true, T> { using type = T; };

template<bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<class T, class U>
struct is_same : false_type {};

template<class T>
struct is_same<T, T> : true_type {};

template<class T> struct remove_const          { using type = T; };
template<class T> struct remove_const<const T> { using type = T; };
template<class T> struct remove_volatile                { using type = T; };
template<class T> struct remove_volatile<volatile T>    { using type = T; };
template<class T> struct remove_cv { using type = typename remove_const<typename remove_volatile<T>::type>::type; };

template<class T> struct remove_reference      { using type = T; };
template<class T> struct remove_reference<T&>  { using type = T; };
template<class T> struct remove_reference<T&&> { using type = T; };

template<class T>
using remove_cv_t = typename remove_cv<T>::type;

template<class T>
using remove_reference_t = typename remove_reference<T>::type;

template<class T>
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

template<bool B, class T, class F>
struct conditional { using type = T; };

template<class T, class F>
struct conditional<false, T, F> { using type = F; };

template<bool B, class T, class F>
using conditional_t = typename conditional<B, T, F>::type;

template<class T> struct type_identity { using type = T; };
template<class T> using type_identity_t = typename type_identity<T>::type;

template<class T>
struct is_lvalue_reference : false_type {};
template<class T>
struct is_lvalue_reference<T&> : true_type {};

template<class T>
struct is_rvalue_reference : false_type {};
template<class T>
struct is_rvalue_reference<T&&> : true_type {};

template<class T>
struct is_reference : integral_constant<bool,
                                        is_lvalue_reference<T>::value ||
                                            is_rvalue_reference<T>::value> {};

template<class T>
struct is_const : false_type {};
template<class T>
struct is_const<const T> : true_type {};

template<class T>
struct is_pointer : false_type {};
template<class T>
struct is_pointer<T*> : true_type {};
template<class T>
struct is_pointer<T* const> : true_type {};
template<class T>
struct is_pointer<T* volatile> : true_type {};
template<class T>
struct is_pointer<T* const volatile> : true_type {};

template<class T>
struct is_null_pointer : false_type {};
template<>
struct is_null_pointer<decltype(nullptr)> : true_type {};

template<class T>
struct is_integral : false_type {};

template<> struct is_integral<bool> : true_type {};
template<> struct is_integral<char> : true_type {};
template<> struct is_integral<signed char> : true_type {};
template<> struct is_integral<unsigned char> : true_type {};
template<> struct is_integral<wchar_t> : true_type {};
#if defined(__cpp_char8_t)
template<> struct is_integral<char8_t> : true_type {};
#endif
template<> struct is_integral<char16_t> : true_type {};
template<> struct is_integral<char32_t> : true_type {};
template<> struct is_integral<short> : true_type {};
template<> struct is_integral<unsigned short> : true_type {};
template<> struct is_integral<int> : true_type {};
template<> struct is_integral<unsigned int> : true_type {};
template<> struct is_integral<long> : true_type {};
template<> struct is_integral<unsigned long> : true_type {};
template<> struct is_integral<long long> : true_type {};
template<> struct is_integral<unsigned long long> : true_type {};

template<class T>
struct is_enum : integral_constant<bool, __is_enum(T)> {};

template<class T>
struct is_signed : false_type {};

template<> struct is_signed<signed char> : true_type {};
template<> struct is_signed<unsigned char> : false_type {};
template<> struct is_signed<char> : integral_constant<bool, static_cast<char>(-1) < static_cast<char>(0)> {};
template<> struct is_signed<wchar_t> : integral_constant<bool, static_cast<wchar_t>(-1) < static_cast<wchar_t>(0)> {};
#if defined(__cpp_char8_t)
template<> struct is_signed<char8_t> : false_type {};
#endif
template<> struct is_signed<char16_t> : false_type {};
template<> struct is_signed<char32_t> : false_type {};
template<> struct is_signed<short> : true_type {};
template<> struct is_signed<unsigned short> : false_type {};
template<> struct is_signed<int> : true_type {};
template<> struct is_signed<unsigned int> : false_type {};
template<> struct is_signed<long> : true_type {};
template<> struct is_signed<unsigned long> : false_type {};
template<> struct is_signed<long long> : true_type {};
template<> struct is_signed<unsigned long long> : false_type {};
template<> struct is_signed<bool> : false_type {};

template<class T>
struct make_unsigned;

template<> struct make_unsigned<signed char> { using type = unsigned char; };
template<> struct make_unsigned<unsigned char> { using type = unsigned char; };
template<> struct make_unsigned<char> { using type = unsigned char; };
template<> struct make_unsigned<short> { using type = unsigned short; };
template<> struct make_unsigned<unsigned short> { using type = unsigned short; };
template<> struct make_unsigned<int> { using type = unsigned int; };
template<> struct make_unsigned<unsigned int> { using type = unsigned int; };
template<> struct make_unsigned<long> { using type = unsigned long; };
template<> struct make_unsigned<unsigned long> { using type = unsigned long; };
template<> struct make_unsigned<long long> { using type = unsigned long long; };
template<> struct make_unsigned<unsigned long long> { using type = unsigned long long; };
template<> struct make_unsigned<bool> { using type = unsigned char; };

template<class T>
using make_unsigned_t = typename make_unsigned<T>::type;

template<class T>
struct is_trivially_copyable : integral_constant<bool, __is_trivially_copyable(T)> {};

template<class T>
#if defined(__clang__)
struct is_trivially_destructible : integral_constant<bool, __is_trivially_destructible(T)> {};
#else
struct is_trivially_destructible : integral_constant<bool, __has_trivial_destructor(T)> {};
#endif

template<class T>
struct is_trivially_move_constructible : integral_constant<bool, __is_trivially_constructible(T, T&&)> {};

template<class T>
struct is_standard_layout : integral_constant<bool, __is_standard_layout(T)> {};

template<class T, class... Args>
struct is_constructible : integral_constant<bool, __is_constructible(T, Args...)> {};

template<class T, class... Args>
struct is_nothrow_constructible : integral_constant<bool, __is_nothrow_constructible(T, Args...)> {};

template<class T>
struct is_copy_constructible : integral_constant<bool, __is_constructible(T, const T&)> {};

template<class T>
struct is_nothrow_copy_constructible : integral_constant<bool, __is_nothrow_constructible(T, const T&)> {};

template<class T>
struct is_move_constructible : integral_constant<bool, __is_constructible(T, T&&)> {};

template<class T>
struct is_nothrow_move_constructible : integral_constant<bool, __is_nothrow_constructible(T, T&&)> {};

template<class T>
struct is_nothrow_move_assignable : integral_constant<bool, __is_nothrow_assignable(T&, T&&)> {};

template<class T>
struct is_nothrow_default_constructible : integral_constant<bool, __is_nothrow_constructible(T)> {};

template<class T>
#if defined(__clang__)
struct is_nothrow_destructible : integral_constant<bool, __is_nothrow_destructible(T)> {};
#else
struct is_nothrow_destructible : is_trivially_destructible<T> {};
#endif

template<class T>
struct is_nothrow_swappable
    : integral_constant<bool,
                        is_nothrow_move_constructible<T>::value &&
                        is_nothrow_move_assignable<T>::value> {};

template<class T>
inline constexpr bool is_standard_layout_v = is_standard_layout<T>::value;

template<class T>
inline constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;

template<class T>
inline constexpr bool is_trivially_destructible_v = is_trivially_destructible<T>::value;

} // namespace iro::freestanding
