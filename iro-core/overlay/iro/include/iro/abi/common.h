/* Copyright (c) The IRO Contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IRO_ABI_COMMON_H
#define IRO_ABI_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#  define IRO_EXTERN_C extern "C"
#  define IRO_EXTERN_C_BEGIN extern "C" {
#  define IRO_EXTERN_C_END }
#  define IRO_NOEXCEPT noexcept
#else
#  define IRO_EXTERN_C
#  define IRO_EXTERN_C_BEGIN
#  define IRO_EXTERN_C_END
#  define IRO_NOEXCEPT
#endif

#endif /* IRO_ABI_COMMON_H */
