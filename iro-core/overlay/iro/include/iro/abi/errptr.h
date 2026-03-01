/* Copyright (c) The IRO Contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IRO_ABI_ERRPTR_H
#define IRO_ABI_ERRPTR_H

#include <iro/abi/common.h>

IRO_EXTERN_C_BEGIN

bool  iro_is_err(const void* p) IRO_NOEXCEPT;
long  iro_ptr_err(const void* p) IRO_NOEXCEPT;
void* iro_err_ptr(long neg_errno) IRO_NOEXCEPT;

IRO_EXTERN_C_END

#endif /* IRO_ABI_ERRPTR_H */
