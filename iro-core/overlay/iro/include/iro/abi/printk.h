/* Copyright (c) The IRO Contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef IRO_ABI_PRINTK_H
#define IRO_ABI_PRINTK_H

#include <iro/abi/common.h>

IRO_EXTERN_C_BEGIN

void iro_printk_level(int level, const char* msg) IRO_NOEXCEPT;

IRO_EXTERN_C_END

#endif /* IRO_ABI_PRINTK_H */
