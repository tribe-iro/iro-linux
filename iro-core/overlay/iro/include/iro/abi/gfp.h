/* Copyright (c) The IRO Contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IRO_ABI_GFP_H
#define IRO_ABI_GFP_H

#include <iro/abi/common.h>

IRO_EXTERN_C_BEGIN

unsigned int iro_gfp_kernel(void) IRO_NOEXCEPT;
unsigned int iro_gfp_atomic(void) IRO_NOEXCEPT;
unsigned int iro_gfp_nowait(void) IRO_NOEXCEPT;
unsigned int iro_gfp_noio(void) IRO_NOEXCEPT;
unsigned int iro_gfp_nofs(void) IRO_NOEXCEPT;
unsigned int iro_gfp_zero(void) IRO_NOEXCEPT;
size_t       iro_kmalloc_minalign(void) IRO_NOEXCEPT;

IRO_EXTERN_C_END

#endif /* IRO_ABI_GFP_H */
