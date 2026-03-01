// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>

/**
 * @file annotations.hpp
 * @brief Static analysis annotations for Clang Thread Safety Analysis and beyond.
 *
 * These annotations integrate with Clang's -Wthread-safety family of warnings
 * to provide compile-time verification of locking discipline. They have no
 * runtime cost and are no-ops on non-Clang compilers.
 *
 * Enable with: -Wthread-safety -Wthread-safety-beta
 *
 * Documentation: https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
 *
 * Usage:
 *   class IRO_CAPABILITY("mutex") Mutex {
 *   public:
 *     void lock() IRO_ACQUIRE();
 *     void unlock() IRO_RELEASE();
 *     bool try_lock() IRO_TRY_ACQUIRE(true);
 *   };
 *
 *   class Counter {
 *     Mutex mu_;
 *     int count_ IRO_GUARDED_BY(mu_);
 *
 *   public:
 *     void increment() IRO_REQUIRES(mu_) {
 *       ++count_;  // Safe: mu_ is held
 *     }
 *
 *     int get() const IRO_REQUIRES(mu_) {
 *       return count_;
 *     }
 *   };
 */

// =============================================================================
// Thread Safety Annotations (Clang Thread Safety Analysis)
// =============================================================================

#if defined(__clang__) && defined(__clang_major__) && (__clang_major__ >= 3)
  #define IRO_THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
  #define IRO_THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

/**
 * @brief Declare a type as a capability (mutex, lock, etc.)
 * @param x String name describing the capability type
 */
#define IRO_CAPABILITY(x) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

/**
 * @brief Declare a type as a scoped capability (RAII lock guard)
 */
#define IRO_SCOPED_CAPABILITY \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

/**
 * @brief Data is protected by the given capability
 * @param x The capability (mutex) that guards this data
 */
#define IRO_GUARDED_BY(x) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

/**
 * @brief Pointer target is protected by the given capability
 * @param x The capability that guards the pointed-to data
 */
#define IRO_PT_GUARDED_BY(x) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

/**
 * @brief Function requires capability to be held on entry
 * @param ... One or more capabilities that must be held
 */
#define IRO_REQUIRES(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

/**
 * @brief Function requires shared (read) capability to be held
 * @param ... One or more capabilities for shared access
 */
#define IRO_REQUIRES_SHARED(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

/**
 * @brief Function acquires capability (caller must not hold it)
 * @param ... Capabilities acquired by this function
 */
#define IRO_ACQUIRE(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

/**
 * @brief Function acquires shared (read) capability
 * @param ... Capabilities acquired for shared access
 */
#define IRO_ACQUIRE_SHARED(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

/**
 * @brief Function releases capability (caller must hold it)
 * @param ... Capabilities released by this function
 */
#define IRO_RELEASE(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

/**
 * @brief Function releases shared capability
 * @param ... Capabilities released from shared access
 */
#define IRO_RELEASE_SHARED(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

/**
 * @brief Function releases all capabilities in generic context
 * @param ... Capabilities released (both exclusive and shared)
 */
#define IRO_RELEASE_GENERIC(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(release_generic_capability(__VA_ARGS__))

/**
 * @brief Try-acquire: acquires capability on success (returns true)
 * @param success_val The return value indicating success (usually true)
 * @param ... Capabilities acquired on success
 */
#define IRO_TRY_ACQUIRE(success_val, ...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(success_val, ##__VA_ARGS__))

/**
 * @brief Try-acquire for shared access
 * @param success_val The return value indicating success
 * @param ... Capabilities acquired for shared access on success
 */
#define IRO_TRY_ACQUIRE_SHARED(success_val, ...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(success_val, ##__VA_ARGS__))

/**
 * @brief Function must NOT hold the capability on entry (prevents deadlock)
 * @param ... Capabilities that must not be held
 */
#define IRO_EXCLUDES(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

/**
 * @brief Assert capability is held at this point (runtime no-op, static check)
 * @param x The capability that should be held
 */
#define IRO_ASSERT_CAPABILITY(x) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

/**
 * @brief Assert shared capability is held
 * @param x The capability that should be held for shared access
 */
#define IRO_ASSERT_SHARED_CAPABILITY(x) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

/**
 * @brief Indicate function returns a capability
 * @param ... The capability returned by this function
 */
#define IRO_RETURN_CAPABILITY(...) \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(__VA_ARGS__))

/**
 * @brief Disable thread safety analysis for this function
 *
 * Use sparingly! This is an escape hatch for cases where the analysis
 * cannot understand the locking pattern (e.g., lock ordering based on
 * runtime values).
 */
#define IRO_NO_THREAD_SAFETY_ANALYSIS \
  IRO_THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// =============================================================================
// Additional Static Analysis Hints
// =============================================================================

/**
 * @brief Indicate pointer parameter must not be null
 */
#if defined(__clang__) || defined(__GNUC__)
  #define IRO_NONNULL __attribute__((nonnull))
  #define IRO_NONNULL_ARGS(...) __attribute__((nonnull(__VA_ARGS__)))
#else
  #define IRO_NONNULL
  #define IRO_NONNULL_ARGS(...)
#endif

/**
 * @brief Indicate function returns non-null pointer
 */
#if defined(__clang__) || defined(__GNUC__)
  #define IRO_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
  #define IRO_RETURNS_NONNULL
#endif

/**
 * @brief Indicate function has no side effects (pure computation)
 */
#if defined(__clang__) || defined(__GNUC__)
  #define IRO_PURE __attribute__((pure))
#else
  #define IRO_PURE
#endif

/**
 * @brief Indicate function has no side effects and doesn't read global memory
 */
#if defined(__clang__) || defined(__GNUC__)
  #define IRO_CONST __attribute__((const))
#else
  #define IRO_CONST
#endif

/**
 * @brief Mark code path as unreachable (enables optimizations)
 */
#define IRO_UNREACHABLE() __builtin_unreachable()

/**
 * @brief Assume condition is true (undefined behavior if false!)
 *
 * Use with extreme caution. This tells the optimizer the condition
 * holds, enabling aggressive optimizations. If the assumption is
 * wrong, behavior is undefined.
 */
#if defined(__clang__)
  #define IRO_ASSUME(x) __builtin_assume(x)
#elif defined(__GNUC__) && __GNUC__ >= 13
  #define IRO_ASSUME(x) __attribute__((assume(x)))
#else
  #define IRO_ASSUME(x) ((void)0)
#endif

// =============================================================================
// Lifetime Annotations (Clang Lifetime Analysis - experimental)
// =============================================================================

#if defined(__clang__) && __clang_major__ >= 10
  /**
   * @brief Indicate parameter lifetime depends on another parameter
   */
  #define IRO_LIFETIMEBOUND [[clang::lifetimebound]]
#else
  #define IRO_LIFETIMEBOUND
#endif
