// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/new.hpp>

// Delete global new/delete to enforce explicit allocator usage.
// Must be included before any use of operator new/delete.

using __iro_size_t = __SIZE_TYPE__;
namespace std { struct nothrow_t; }

__attribute__((unavailable("IRO Core forbids global operator new/delete")))
void* operator new(__iro_size_t);
__attribute__((unavailable("IRO Core forbids global operator new/delete")))
void* operator new[](__iro_size_t);
__attribute__((unavailable("IRO Core forbids global operator delete")))
void operator delete(void*) noexcept;
__attribute__((unavailable("IRO Core forbids global operator delete[]")))
void operator delete[](void*) noexcept;
__attribute__((unavailable("IRO Core forbids global operator new (nothrow)")))
void* operator new(__iro_size_t, const std::nothrow_t&) noexcept;
__attribute__((unavailable("IRO Core forbids global operator new[] (nothrow)")))
void* operator new[](__iro_size_t, const std::nothrow_t&) noexcept;
__attribute__((unavailable("IRO Core forbids global operator delete (nothrow)")))
void operator delete(void*, const std::nothrow_t&) noexcept;
__attribute__((unavailable("IRO Core forbids global operator delete[] (nothrow)")))
void operator delete[](void*, const std::nothrow_t&) noexcept;
__attribute__((unavailable("IRO Core forbids sized delete")))
void operator delete(void*, __iro_size_t) noexcept;
__attribute__((unavailable("IRO Core forbids sized delete[]")))
void operator delete[](void*, __iro_size_t) noexcept;
__attribute__((unavailable("IRO Core forbids aligned new")))
void* operator new(__iro_size_t, iro::freestanding::align_val_t);
__attribute__((unavailable("IRO Core forbids aligned new[]")))
void* operator new[](__iro_size_t, iro::freestanding::align_val_t);
__attribute__((unavailable("IRO Core forbids aligned delete")))
void operator delete(void*, iro::freestanding::align_val_t) noexcept;
__attribute__((unavailable("IRO Core forbids aligned delete[]")))
void operator delete[](void*, iro::freestanding::align_val_t) noexcept;
