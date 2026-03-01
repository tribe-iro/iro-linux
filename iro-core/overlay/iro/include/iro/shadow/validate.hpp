// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/type_traits.hpp>

/**
 * @file validate.hpp
 * @brief Macros for validating shadow type layouts against kernel struct layouts.
 *
 * Shadow types mirror kernel structs without including kernel headers. These macros
 * provide compile-time validation that shadow types match the kernel's actual layout
 * (as extracted by iro-tool).
 *
 * Usage:
 *   #include <generated/iro/layout_subsystem.hpp>
 *   #include <iro/shadow/validate.hpp>
 *
 *   namespace iro::shadow::subsystem {
 *
 *   struct my_shadow {
 *     int field_a;
 *     long field_b;
 *   };
 *
 *   // Validate entire type
 *   IRO_VALIDATE_SHADOW_TYPE(my_shadow, my_kernel_type);
 *
 *   // Validate specific fields
 *   IRO_VALIDATE_SHADOW_FIELD(my_shadow, field_a, my_kernel_type, field_a);
 *   IRO_VALIDATE_SHADOW_FIELD(my_shadow, field_b, my_kernel_type, field_b);
 *
 *   // Or use the all-in-one macro for simple 1:1 mappings
 *   IRO_DEFINE_VALIDATED_SHADOW(my_shadow, my_kernel_type,
 *     IRO_FIELD(field_a),
 *     IRO_FIELD(field_b)
 *   );
 *
 *   } // namespace
 */

// =============================================================================
// Core Validation Macros
// =============================================================================

/**
 * @brief Validate shadow type size and alignment against kernel type.
 * @param shadow_type The C++ shadow type name
 * @param kernel_type The kernel type name (used to form IRO_SIZEOF__/IRO_ALIGNOF__ macros)
 */
#define IRO_VALIDATE_SHADOW_TYPE(shadow_type, kernel_type) \
  static_assert(sizeof(shadow_type) == IRO_SIZEOF__##kernel_type, \
      "IRO: sizeof(" #shadow_type ") != sizeof(" #kernel_type ")"); \
  static_assert(alignof(shadow_type) == IRO_ALIGNOF__##kernel_type, \
      "IRO: alignof(" #shadow_type ") != alignof(" #kernel_type ")")

/**
 * @brief Validate shadow field offset against kernel field offset.
 * @param shadow_type The C++ shadow type name
 * @param shadow_field The field name in the shadow type
 * @param kernel_type The kernel type name
 * @param kernel_field The field name in the kernel type
 */
#define IRO_VALIDATE_SHADOW_FIELD(shadow_type, shadow_field, kernel_type, kernel_field) \
  static_assert(offsetof(shadow_type, shadow_field) == IRO_OFFSETOF__##kernel_type##__##kernel_field, \
      "IRO: offsetof(" #shadow_type "::" #shadow_field ") != offsetof(" #kernel_type "::" #kernel_field ")")

/**
 * @brief Validate shadow field where shadow and kernel field names match.
 */
#define IRO_VALIDATE_SHADOW_FIELD_SAME(shadow_type, field, kernel_type) \
  IRO_VALIDATE_SHADOW_FIELD(shadow_type, field, kernel_type, field)

// =============================================================================
// Type Property Assertions
// =============================================================================

/**
 * @brief Assert that a shadow type has standard layout (required for offsetof).
 */
#define IRO_ASSERT_STANDARD_LAYOUT(shadow_type) \
  static_assert(::iro::freestanding::is_standard_layout_v<shadow_type>, \
      "IRO: " #shadow_type " must be standard layout for safe kernel interop")

/**
 * @brief Assert that a shadow type is trivially copyable (safe for memcpy).
 */
#define IRO_ASSERT_TRIVIALLY_COPYABLE(shadow_type) \
  static_assert(::iro::freestanding::is_trivially_copyable_v<shadow_type>, \
      "IRO: " #shadow_type " must be trivially copyable for safe kernel interop")

/**
 * @brief Assert that a shadow type is trivially destructible (no cleanup needed).
 */
#define IRO_ASSERT_TRIVIALLY_DESTRUCTIBLE(shadow_type) \
  static_assert(::iro::freestanding::is_trivially_destructible_v<shadow_type>, \
      "IRO: " #shadow_type " must be trivially destructible for safe kernel interop")

/**
 * @brief Assert all common kernel interop requirements.
 */
#define IRO_ASSERT_KERNEL_COMPATIBLE(shadow_type) \
  IRO_ASSERT_STANDARD_LAYOUT(shadow_type); \
  IRO_ASSERT_TRIVIALLY_COPYABLE(shadow_type); \
  IRO_ASSERT_TRIVIALLY_DESTRUCTIBLE(shadow_type)

// =============================================================================
// Combined Validation
// =============================================================================

/**
 * @brief Full validation: type layout + kernel compatibility.
 * @param shadow_type The C++ shadow type
 * @param kernel_type The kernel type name for layout macros
 *
 * Validates:
 * - sizeof matches
 * - alignof matches
 * - is_standard_layout
 * - is_trivially_copyable
 * - is_trivially_destructible
 */
#define IRO_VALIDATE_SHADOW_FULL(shadow_type, kernel_type) \
  IRO_VALIDATE_SHADOW_TYPE(shadow_type, kernel_type); \
  IRO_ASSERT_KERNEL_COMPATIBLE(shadow_type)

// =============================================================================
// Field List Helpers (for IRO_DEFINE_VALIDATED_SHADOW)
// =============================================================================

#define IRO_FIELD(name) name

// Internal: expand field validation
#define IRO_VALIDATE_FIELD_IMPL(shadow_type, kernel_type, field) \
  IRO_VALIDATE_SHADOW_FIELD_SAME(shadow_type, field, kernel_type);

// Internal: foreach over variadic fields
#define IRO_FOREACH_1(macro, shadow, kernel, f1) macro(shadow, kernel, f1)
#define IRO_FOREACH_2(macro, shadow, kernel, f1, f2) macro(shadow, kernel, f1) macro(shadow, kernel, f2)
#define IRO_FOREACH_3(macro, shadow, kernel, f1, f2, f3) \
  macro(shadow, kernel, f1) macro(shadow, kernel, f2) macro(shadow, kernel, f3)
#define IRO_FOREACH_4(macro, shadow, kernel, f1, f2, f3, f4) \
  macro(shadow, kernel, f1) macro(shadow, kernel, f2) macro(shadow, kernel, f3) macro(shadow, kernel, f4)
#define IRO_FOREACH_5(macro, shadow, kernel, f1, f2, f3, f4, f5) \
  macro(shadow, kernel, f1) macro(shadow, kernel, f2) macro(shadow, kernel, f3) \
  macro(shadow, kernel, f4) macro(shadow, kernel, f5)
#define IRO_FOREACH_6(macro, shadow, kernel, f1, f2, f3, f4, f5, f6) \
  macro(shadow, kernel, f1) macro(shadow, kernel, f2) macro(shadow, kernel, f3) \
  macro(shadow, kernel, f4) macro(shadow, kernel, f5) macro(shadow, kernel, f6)
#define IRO_FOREACH_7(macro, shadow, kernel, f1, f2, f3, f4, f5, f6, f7) \
  macro(shadow, kernel, f1) macro(shadow, kernel, f2) macro(shadow, kernel, f3) \
  macro(shadow, kernel, f4) macro(shadow, kernel, f5) macro(shadow, kernel, f6) \
  macro(shadow, kernel, f7)
#define IRO_FOREACH_8(macro, shadow, kernel, f1, f2, f3, f4, f5, f6, f7, f8) \
  macro(shadow, kernel, f1) macro(shadow, kernel, f2) macro(shadow, kernel, f3) \
  macro(shadow, kernel, f4) macro(shadow, kernel, f5) macro(shadow, kernel, f6) \
  macro(shadow, kernel, f7) macro(shadow, kernel, f8)

#define IRO_GET_FOREACH_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define IRO_FOREACH(macro, shadow, kernel, ...) \
  IRO_GET_FOREACH_MACRO(__VA_ARGS__, \
    IRO_FOREACH_8, IRO_FOREACH_7, IRO_FOREACH_6, IRO_FOREACH_5, \
    IRO_FOREACH_4, IRO_FOREACH_3, IRO_FOREACH_2, IRO_FOREACH_1) \
  (macro, shadow, kernel, __VA_ARGS__)

/**
 * @brief Define a validated shadow type with field checks.
 *
 * Usage:
 *   IRO_VALIDATE_SHADOW_WITH_FIELDS(my_shadow, kernel_type,
 *     field_a, field_b, field_c
 *   );
 *
 * Expands to full type validation plus offsetof checks for each field.
 */
#define IRO_VALIDATE_SHADOW_WITH_FIELDS(shadow_type, kernel_type, ...) \
  IRO_VALIDATE_SHADOW_FULL(shadow_type, kernel_type); \
  IRO_FOREACH(IRO_VALIDATE_FIELD_IMPL, shadow_type, kernel_type, __VA_ARGS__)
