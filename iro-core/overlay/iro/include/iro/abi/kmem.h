/* Copyright (c) The IRO Contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IRO_ABI_KMEM_H
#define IRO_ABI_KMEM_H

#include <iro/abi/common.h>

IRO_EXTERN_C_BEGIN

void* iro_kmalloc(size_t size, unsigned int flags) IRO_NOEXCEPT;
void* iro_krealloc(void* ptr, size_t size, unsigned int flags) IRO_NOEXCEPT;
void  iro_kfree(void* ptr) IRO_NOEXCEPT;

IRO_EXTERN_C_END

#endif /* IRO_ABI_KMEM_H */
