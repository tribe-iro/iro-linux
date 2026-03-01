# IRO Interop Roadmap: Technical Specification

## Executive Summary

This document specifies the next phase of IRO development: completing the C/C++ kernel interop layer. Based on codebase analysis and research into implementation patterns, this roadmap provides concrete specifications for closing the identified gaps in both iro-tool and iro-core.

**Scope**: Low-level interop primitives only. Data structures (intrusive lists, RCU) are Phase 3.

**Constraints**: Clang 18+, Linux 5.0+, x86_64/aarch64 only.

---

## Part 1: Validated Gap Analysis

### 1.1 iro-tool Gaps

| Gap | Status | Evidence | Priority |
|-----|--------|----------|----------|
| Anonymous struct/union | Missing (by policy) | `deny_anonymous_members{true}` in `iro_manifest.hpp:27` | **High** |
| Bitfield bit-level info | Partial | Only accessor IDs extracted; missing `bit_offset`, `bit_size` | **High** |
| Enum extraction | Missing | No DW_TAG_enumeration_type support | **Medium** |
| Preprocessor constants | Missing | No macro/define extraction | **Medium** |
| Function pointer signatures | Missing | No DW_TAG_subroutine_type support | **Low** |
| Attribute metadata | Missing | Only sizeof/alignof values, not their sources | **Low** |

### 1.2 iro-core Gaps

| Gap | Status | Evidence | Priority |
|-----|--------|----------|----------|
| `container_of` template | Missing | No implementation found | **Critical** |
| MMIO primitives | Missing | No volatile read/write wrappers | **High** |
| `user_ptr<T>` wrapper | Missing | No __user annotation support | **High** |
| Callback registration patterns | Missing | No ops table infrastructure | **Medium** |
| `enum_cast<>` utility | Partial | `enum class` used, no generic cast | **Low** |

---

## Part 2: iro-core Implementation Specifications

### 2.1 container_of Template

**Purpose**: Recover pointer to containing structure from member pointer. Essential for intrusive data structures and kernel callback patterns.

**Location**: `/overlay/iro/include/iro/intrusive/container_of.hpp`

**Specification**:

```cpp
// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/type_traits.hpp>

namespace iro {

namespace detail {

// Compile-time offset computation via pointer-to-member
template<class Container, class Member>
struct member_offset {
  static constexpr freestanding::size_t compute(Member Container::* mp) noexcept {
    // Use null pointer arithmetic - valid in constexpr since C++17
    // for standard layout types
    return static_cast<freestanding::size_t>(
        reinterpret_cast<const char*>(&(static_cast<Container*>(nullptr)->*mp)) -
        static_cast<const char*>(nullptr));
  }
};

} // namespace detail

/**
 * @brief Recover containing structure pointer from member pointer.
 *
 * Type-safe equivalent of Linux kernel's container_of macro.
 * Uses pointer-to-member for compile-time verification.
 *
 * @tparam Container The containing structure type
 * @tparam Member The member type
 * @param ptr Pointer to the member within a Container instance
 * @param mp Pointer-to-member identifying which member
 * @return Pointer to the containing Container
 *
 * Usage:
 *   struct task_struct { ... list_head tasks; ... };
 *   list_head* node = get_next_node();
 *   task_struct* task = container_of(node, &task_struct::tasks);
 */
template<class Container, class Member>
IRO_NODISCARD constexpr Container* container_of(
    Member* ptr,
    Member Container::* mp) noexcept {
  static_assert(freestanding::is_standard_layout_v<Container>,
      "container_of requires standard layout type");

  if (ptr == nullptr) return nullptr;

  const auto offset = detail::member_offset<Container, Member>::compute(mp);
  return reinterpret_cast<Container*>(
      reinterpret_cast<char*>(ptr) - offset);
}

template<class Container, class Member>
IRO_NODISCARD constexpr const Container* container_of(
    const Member* ptr,
    Member Container::* mp) noexcept {
  static_assert(freestanding::is_standard_layout_v<Container>,
      "container_of requires standard layout type");

  if (ptr == nullptr) return nullptr;

  const auto offset = detail::member_offset<Container, Member>::compute(mp);
  return reinterpret_cast<const Container*>(
      reinterpret_cast<const char*>(ptr) - offset);
}

} // namespace iro
```

**Rationale**:
- Pointer-to-member approach is type-safe at compile time
- `is_standard_layout_v` constraint prevents UB with virtual bases
- Const overload preserves const-correctness
- Null check returns null (fail-safe, unlike kernel macro)

**References**:
- [avakar/container_of](https://github.com/avakar/container_of) - C++11 implementation
- [P0908R0: Offsetof for Pointers to Members](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0908r0.html)
- [P3407R0: Make idiomatic usage of offsetof well-defined](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3407r0.html)

---

### 2.2 MMIO Primitives

**Purpose**: Safe memory-mapped I/O with proper barriers. Required for any driver code.

**Location**: `/overlay/iro/include/iro/arch/mmio.hpp`

**Specification**:

```cpp
// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstdint.hpp>
#include <iro/annotations.hpp>

namespace iro::arch {

// =============================================================================
// Architecture-Specific Barriers
// =============================================================================

#if defined(__x86_64__)

// x86: Strong memory model. MMIO to uncached regions is naturally ordered.
// Compiler barrier sufficient for most cases.
inline void io_mb()  { asm volatile("mfence" ::: "memory"); }
inline void io_rmb() { asm volatile("lfence" ::: "memory"); }
inline void io_wmb() { asm volatile("sfence" ::: "memory"); }

// Compiler barrier only (no CPU fence)
inline void barrier() { asm volatile("" ::: "memory"); }

#elif defined(__aarch64__)

// ARM64: Weaker memory model. Need explicit barriers.
// DSB = Data Synchronization Barrier
// DMB = Data Memory Barrier
inline void io_mb()  { asm volatile("dsb sy" ::: "memory"); }
inline void io_rmb() { asm volatile("dsb ld" ::: "memory"); }
inline void io_wmb() { asm volatile("dsb st" ::: "memory"); }

inline void barrier() { asm volatile("" ::: "memory"); }

#else
#error "Unsupported architecture for MMIO"
#endif

// =============================================================================
// MMIO Read/Write (Volatile + Barrier)
// =============================================================================

/**
 * @brief MMIO read with barrier.
 *
 * Performs volatile read followed by read barrier.
 * Use for reading hardware registers.
 */
template<typename T>
IRO_NODISCARD inline T mmio_read(const volatile T* addr) noexcept {
  static_assert(sizeof(T) <= 8, "MMIO type too large");
  static_assert(__is_trivially_copyable(T), "MMIO type must be trivially copyable");

  T value = *addr;
  io_rmb();
  return value;
}

/**
 * @brief MMIO write with barrier.
 *
 * Performs write barrier followed by volatile write.
 * Use for writing hardware registers.
 */
template<typename T>
inline void mmio_write(volatile T* addr, T value) noexcept {
  static_assert(sizeof(T) <= 8, "MMIO type too large");
  static_assert(__is_trivially_copyable(T), "MMIO type must be trivially copyable");

  io_wmb();
  *addr = value;
}

// =============================================================================
// Relaxed MMIO (No Barrier - Use With Caution)
// =============================================================================

/**
 * @brief Relaxed MMIO read (no barrier).
 *
 * Only use when barriers are handled externally or not needed.
 */
template<typename T>
IRO_NODISCARD inline T mmio_read_relaxed(const volatile T* addr) noexcept {
  return *addr;
}

/**
 * @brief Relaxed MMIO write (no barrier).
 */
template<typename T>
inline void mmio_write_relaxed(volatile T* addr, T value) noexcept {
  *addr = value;
}

// =============================================================================
// Fixed-Width Convenience Functions
// =============================================================================

IRO_NODISCARD inline freestanding::uint8_t  readb(const volatile void* addr) noexcept {
  return mmio_read(static_cast<const volatile freestanding::uint8_t*>(addr));
}
IRO_NODISCARD inline freestanding::uint16_t readw(const volatile void* addr) noexcept {
  return mmio_read(static_cast<const volatile freestanding::uint16_t*>(addr));
}
IRO_NODISCARD inline freestanding::uint32_t readl(const volatile void* addr) noexcept {
  return mmio_read(static_cast<const volatile freestanding::uint32_t*>(addr));
}
IRO_NODISCARD inline freestanding::uint64_t readq(const volatile void* addr) noexcept {
  return mmio_read(static_cast<const volatile freestanding::uint64_t*>(addr));
}

inline void writeb(volatile void* addr, freestanding::uint8_t v) noexcept {
  mmio_write(static_cast<volatile freestanding::uint8_t*>(addr), v);
}
inline void writew(volatile void* addr, freestanding::uint16_t v) noexcept {
  mmio_write(static_cast<volatile freestanding::uint16_t*>(addr), v);
}
inline void writel(volatile void* addr, freestanding::uint32_t v) noexcept {
  mmio_write(static_cast<volatile freestanding::uint32_t*>(addr), v);
}
inline void writeq(volatile void* addr, freestanding::uint64_t v) noexcept {
  mmio_write(static_cast<volatile freestanding::uint64_t*>(addr), v);
}

// =============================================================================
// x86 Port I/O (x86_64 only)
// =============================================================================

#if defined(__x86_64__)

IRO_NODISCARD inline freestanding::uint8_t inb(freestanding::uint16_t port) noexcept {
  freestanding::uint8_t value;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

IRO_NODISCARD inline freestanding::uint16_t inw(freestanding::uint16_t port) noexcept {
  freestanding::uint16_t value;
  asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

IRO_NODISCARD inline freestanding::uint32_t inl(freestanding::uint16_t port) noexcept {
  freestanding::uint32_t value;
  asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

inline void outb(freestanding::uint16_t port, freestanding::uint8_t value) noexcept {
  asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

inline void outw(freestanding::uint16_t port, freestanding::uint16_t value) noexcept {
  asm volatile("outw %0, %1" :: "a"(value), "Nd"(port));
}

inline void outl(freestanding::uint16_t port, freestanding::uint32_t value) noexcept {
  asm volatile("outl %0, %1" :: "a"(value), "Nd"(port));
}

#endif // __x86_64__

} // namespace iro::arch
```

**Rationale**:
- Template approach allows any trivially copyable type
- Static asserts prevent misuse at compile time
- Separate relaxed variants for performance-critical paths
- Fixed-width functions match kernel API expectations
- Architecture-specific barriers based on memory model

**References**:
- [Memory mapped registers in C/C++](https://wiki.osdev.org/Memory_mapped_registers_in_C/C++)
- [Linux kernel memory-barriers.txt](https://www.kernel.org/doc/Documentation/memory-barriers.txt)
- [ARM Barriers](https://developer.arm.com/documentation/100941/0100/Barriers)

---

### 2.3 user_ptr Wrapper

**Purpose**: Type-safe wrapper for user-space pointers. Prevents accidental direct dereference and enforces copy_from/to_user usage.

**Location**: `/overlay/iro/include/iro/user/user_ptr.hpp`

**Specification**:

```cpp
// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/err/errc.hpp>

namespace iro {

/**
 * @brief Type-safe wrapper for user-space pointers.
 *
 * Wraps a pointer that points to user-space memory.
 * Cannot be directly dereferenced - forces use of copy_from_user/copy_to_user.
 *
 * Equivalent to kernel's __user annotation but with C++ type enforcement.
 */
template<class T>
class user_ptr {
public:
  using element_type = T;
  using pointer = T*;

  constexpr user_ptr() noexcept : m_ptr(nullptr) {}
  constexpr explicit user_ptr(T* ptr) noexcept : m_ptr(ptr) {}

  // Prevent accidental construction from kernel pointers
  template<class U, class = freestanding::enable_if_t<
      freestanding::is_convertible_v<U*, T*>>>
  constexpr explicit user_ptr(user_ptr<U> other) noexcept
      : m_ptr(other.get()) {}

  // Get raw pointer (for passing to kernel copy functions)
  IRO_NODISCARD constexpr T* get() const noexcept { return m_ptr; }

  // Explicit bool conversion
  IRO_NODISCARD constexpr explicit operator bool() const noexcept {
    return m_ptr != nullptr;
  }

  // Pointer arithmetic
  constexpr user_ptr operator+(freestanding::ptrdiff_t n) const noexcept {
    return user_ptr(m_ptr + n);
  }

  constexpr user_ptr operator-(freestanding::ptrdiff_t n) const noexcept {
    return user_ptr(m_ptr - n);
  }

  constexpr user_ptr& operator+=(freestanding::ptrdiff_t n) noexcept {
    m_ptr += n;
    return *this;
  }

  constexpr user_ptr& operator-=(freestanding::ptrdiff_t n) noexcept {
    m_ptr -= n;
    return *this;
  }

  // Comparison
  constexpr bool operator==(user_ptr other) const noexcept {
    return m_ptr == other.m_ptr;
  }

  constexpr bool operator!=(user_ptr other) const noexcept {
    return m_ptr != other.m_ptr;
  }

  // Deliberately NO operator* or operator->
  // This forces use of copy_from_user/copy_to_user

private:
  T* m_ptr;
};

// Deduction guide
template<class T>
user_ptr(T*) -> user_ptr<T>;

/**
 * @brief Create user_ptr from raw pointer.
 *
 * Use at syscall entry point to wrap user-provided pointers.
 */
template<class T>
IRO_NODISCARD constexpr user_ptr<T> make_user_ptr(T* ptr) noexcept {
  return user_ptr<T>(ptr);
}

// Forward declarations for copy functions (implemented with kernel ABI)
// These would call the actual kernel copy_from_user/copy_to_user

/**
 * @brief Copy data from user space.
 * @return Number of bytes NOT copied (0 on success)
 */
template<class T>
freestanding::size_t copy_from_user(T& dst, user_ptr<const T> src) noexcept;

/**
 * @brief Copy data to user space.
 * @return Number of bytes NOT copied (0 on success)
 */
template<class T>
freestanding::size_t copy_to_user(user_ptr<T> dst, const T& src) noexcept;

/**
 * @brief Copy array from user space.
 */
template<class T>
freestanding::size_t copy_from_user(T* dst, user_ptr<const T> src,
                                     freestanding::size_t count) noexcept;

/**
 * @brief Copy array to user space.
 */
template<class T>
freestanding::size_t copy_to_user(user_ptr<T> dst, const T* src,
                                   freestanding::size_t count) noexcept;

} // namespace iro
```

**Rationale**:
- No dereference operators = compile-time enforcement
- Explicit construction prevents implicit conversions
- Arithmetic operators allow walking arrays
- Copy functions return uncopied byte count (kernel convention)

---

### 2.4 IRQ Save/Restore Guards

**Purpose**: RAII wrappers for interrupt enable/disable. Critical for spinlock implementations.

**Location**: `/overlay/iro/include/iro/arch/irq.hpp`

**Specification**:

```cpp
// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstdint.hpp>
#include <iro/annotations.hpp>

namespace iro::arch {

// =============================================================================
// IRQ State Type
// =============================================================================

#if defined(__x86_64__)
using irq_flags_t = freestanding::uint64_t;
#elif defined(__aarch64__)
using irq_flags_t = freestanding::uint64_t;
#endif

// =============================================================================
// Raw IRQ Primitives
// =============================================================================

#if defined(__x86_64__)

IRO_NODISCARD inline irq_flags_t irq_save() noexcept {
  irq_flags_t flags;
  asm volatile(
      "pushfq\n\t"
      "popq %0\n\t"
      "cli"
      : "=r"(flags)
      :
      : "memory");
  return flags;
}

inline void irq_restore(irq_flags_t flags) noexcept {
  asm volatile(
      "pushq %0\n\t"
      "popfq"
      :
      : "r"(flags)
      : "memory", "cc");
}

inline void irq_disable() noexcept {
  asm volatile("cli" ::: "memory");
}

inline void irq_enable() noexcept {
  asm volatile("sti" ::: "memory");
}

IRO_NODISCARD inline bool irq_enabled() noexcept {
  irq_flags_t flags;
  asm volatile("pushfq\n\tpopq %0" : "=r"(flags));
  return (flags & 0x200) != 0;  // IF flag
}

#elif defined(__aarch64__)

IRO_NODISCARD inline irq_flags_t irq_save() noexcept {
  irq_flags_t flags;
  asm volatile(
      "mrs %0, daif\n\t"
      "msr daifset, #0xf"
      : "=r"(flags)
      :
      : "memory");
  return flags;
}

inline void irq_restore(irq_flags_t flags) noexcept {
  asm volatile("msr daif, %0" :: "r"(flags) : "memory");
}

inline void irq_disable() noexcept {
  asm volatile("msr daifset, #0xf" ::: "memory");
}

inline void irq_enable() noexcept {
  asm volatile("msr daifclr, #0xf" ::: "memory");
}

IRO_NODISCARD inline bool irq_enabled() noexcept {
  irq_flags_t flags;
  asm volatile("mrs %0, daif" : "=r"(flags));
  return (flags & 0x3C0) == 0;  // I, F, A, D bits
}

#endif

// =============================================================================
// RAII IRQ Guard
// =============================================================================

/**
 * @brief RAII guard for disabling/restoring interrupts.
 *
 * Usage:
 *   void critical_section() {
 *     irq_guard guard;
 *     // IRQs disabled here
 *   } // IRQs restored to previous state
 */
class irq_guard {
public:
  irq_guard() noexcept : m_flags(irq_save()) {}

  ~irq_guard() noexcept {
    irq_restore(m_flags);
  }

  // Non-copyable, non-movable
  irq_guard(const irq_guard&) = delete;
  irq_guard& operator=(const irq_guard&) = delete;
  irq_guard(irq_guard&&) = delete;
  irq_guard& operator=(irq_guard&&) = delete;

private:
  irq_flags_t m_flags;
};

} // namespace iro::arch
```

---

## Part 3: iro-tool Enhancement Specifications

### 3.1 Anonymous Struct/Union Support

**Current State**: `deny_anonymous_members{true}` in manifest options. Parser rejects types with anonymous members.

**Problem**: Many kernel structs (e.g., `struct page`, `sk_buff`, `mm_struct`) use anonymous unions for layout unions or padding.

**Solution**: Implement flattened field access with path notation.

**Manifest Extension**:

```toml
[options]
deny_anonymous_members = false  # Allow anonymous member traversal

[types.page]
c_type = "struct page"
fields = [
  "flags",
  ".compound_head",     # Field inside anonymous union (dot prefix)
  ".compound_dtor",
  ".compound_order",
]
```

**Implementation Approach**:

1. **gen_probe.cc**: When generating offsetof() expressions for dot-prefixed fields, emit:
   ```c
   offsetof(struct page, compound_head)  // C11 allows direct access
   ```
   C11 6.7.2.1p13 specifies that members of anonymous structs/unions are accessible as if they were direct members.

2. **layout_parse.cc**: Parse normally - the ELF note just contains the flattened offset.

3. **Validation**: Compile probe with `-std=c11` or later (already required).

**DWARF Parsing Alternative** (for future introspection):
- Traverse `DW_TAG_member` with `DW_AT_name == NULL` (anonymous)
- Recurse into `DW_TAG_structure_type` or `DW_TAG_union_type` child
- Flatten member names with path separators

**References**:
- [makedumpfile patch for anonymous struct resolving](http://www.mail-archive.com/kexec@lists.infradead.org/msg37420.html)

---

### 3.2 Bitfield Enhanced Extraction

**Current State**: Extracts bitfield names and accessor IDs. Does NOT extract bit widths or offsets.

**Problem**: Cannot generate correct accessor shims without knowing bit positions.

**Solution**: Add bit-level metadata to the probe note.

**Enhanced Record Structure**:

```cpp
// Current (layout_parse.cc:118-128):
struct NoteRecord {
  enum class Kind : uint16_t { Type = 1, Field = 2, BitfieldAccessor = 3 };
  Kind kind;
  uint16_t flags;
  string type_name;
  string field_name;
  uint64_t sizeof_type;
  uint32_t alignof_type;
  uint64_t offset_or_id;
};

// Enhanced (add to BitfieldAccessor records):
struct NoteRecord {
  // ... existing fields ...
  uint16_t bit_offset;   // Offset from start of containing word (bits)
  uint16_t bit_size;     // Width of bitfield (bits)
  uint16_t storage_size; // Size of containing storage unit (bytes)
};
```

**Probe Generation**:

```c
// For each bitfield, emit helper struct to compute bit position
#define IRO_BF_INFO(type, field) \
  { \
    .bit_offset = __builtin_offsetof(type, field) * 8 + /* needs runtime */, \
    .bit_size = sizeof(((type*)0)->field) * 8, /* wrong for bitfields */ \
  }
```

**Challenge**: C does not provide a portable way to query bitfield layout at compile time.

**DWARF-Based Solution** (Preferred):

1. Compile probe with `-g` (debug info)
2. Parse DWARF from probe object
3. For each `DW_TAG_member` with `DW_AT_bit_size`:
   - Read `DW_AT_data_bit_offset` (DWARF 4+) or compute from `DW_AT_bit_offset` + `DW_AT_byte_size` (DWARF 3)
   - Extract `DW_AT_bit_size`

**Note**: GCC uses `DW_AT_data_bit_offset` only with `-gdwarf-5`. Clang uses it with `-gdwarf-4`. Need to detect and handle both.

**References**:
- [LLVM D19630: Support DWARF4 bitfields via DW_AT_data_bit_offset](https://reviews.llvm.org/D19630)
- [Go issue #46784: BitOffset values of bitfields](https://github.com/golang/go/issues/46784)

---

### 3.3 Enum Extraction

**Current State**: No enum support.

**Problem**: Many kernel APIs use enums. Type-safe C++ wrappers need the values.

**Manifest Extension**:

```toml
[enums.gfp_t]
c_type = "gfp_t"
values = [
  "GFP_KERNEL",
  "GFP_ATOMIC",
  "GFP_NOWAIT",
]

# Alternative: extract all values automatically
[enums.gfp_t]
c_type = "gfp_t"
extract_all = true
```

**Probe Generation**:

```c
// Emit enum value as constant
static const uint64_t __iro_enum_gfp_t_GFP_KERNEL = (uint64_t)(GFP_KERNEL);
```

**Layout Parse Output**:

```cpp
// layout_gfp.hpp
#define IRO_ENUM_gfp_t_GFP_KERNEL  ((gfp_t)0x400u)
#define IRO_ENUM_gfp_t_GFP_ATOMIC  ((gfp_t)0x800u)
```

**DWARF-Based Full Extraction**:
- Parse `DW_TAG_enumeration_type`
- Read `DW_AT_type` for underlying type
- Iterate `DW_TAG_enumerator` children
- Read `DW_AT_name` and `DW_AT_const_value`

---

### 3.4 Preprocessor Constant Extraction

**Current State**: No macro support.

**Problem**: Many kernel constants are `#define`, not enum (e.g., `PAGE_SIZE`, `TASK_RUNNING`).

**Manifest Extension**:

```toml
[constants]
includes = ["<linux/mm.h>"]  # Additional includes for constants
values = [
  "PAGE_SIZE",
  "PAGE_SHIFT",
  "TASK_RUNNING",
]
```

**Probe Generation**:

```c
// Emit constant as static value
static const unsigned long __iro_const_PAGE_SIZE = (unsigned long)(PAGE_SIZE);
```

**Note Section Encoding**: Add new record type `Constant = 4`:

```cpp
enum class Kind : uint16_t {
  Type = 1,
  Field = 2,
  BitfieldAccessor = 3,
  Constant = 4,  // NEW
};
```

---

## Part 4: iro-core C Interop Namespace

### 4.1 Namespace Convention

```
iro::           Pure C++ (can do anything)
iro::c::        C-compatible (layout-identical to kernel)
iro::arch::     Architecture-specific primitives
```

### 4.2 C-Compatible list_head

**Location**: `/overlay/iro/include/iro/c/list.hpp`

**Specification**:

```cpp
namespace iro::c {

// Layout-identical to kernel's struct list_head
struct list_head {
  list_head* next;
  list_head* prev;
};

// Compile-time layout verification
static_assert(sizeof(list_head) == 2 * sizeof(void*));
static_assert(offsetof(list_head, next) == 0);
static_assert(offsetof(list_head, prev) == sizeof(void*));

// Initialize (matches INIT_LIST_HEAD)
inline void list_head_init(list_head* head) noexcept {
  head->next = head;
  head->prev = head;
}

// Check if empty
IRO_NODISCARD inline bool list_empty(const list_head* head) noexcept {
  return head->next == head;
}

// Insert between prev and next
inline void __list_add(list_head* entry, list_head* prev, list_head* next) noexcept {
  next->prev = entry;
  entry->next = next;
  entry->prev = prev;
  prev->next = entry;
}

// Add to head of list
inline void list_add(list_head* entry, list_head* head) noexcept {
  __list_add(entry, head, head->next);
}

// Add to tail of list
inline void list_add_tail(list_head* entry, list_head* head) noexcept {
  __list_add(entry, head->prev, head);
}

// Remove from list
inline void list_del(list_head* entry) noexcept {
  entry->prev->next = entry->next;
  entry->next->prev = entry->prev;
  // Poison pointers (optional, matches kernel LIST_POISON)
  entry->next = nullptr;
  entry->prev = nullptr;
}

// Get containing structure
template<class Container>
IRO_NODISCARD inline Container* list_entry(
    list_head* ptr,
    list_head Container::* member) noexcept {
  return container_of(ptr, member);
}

} // namespace iro::c
```

---

## Part 5: Implementation Priority

### Phase 2A: Critical Path (Unblocks Everything)

| Item | Location | Est. Lines | Blocks |
|------|----------|------------|--------|
| `container_of` | iro-core | ~60 | All intrusive structures |
| MMIO primitives | iro-core | ~150 | All driver code |
| IRQ guards | iro-core | ~80 | Spinlocks |
| Anonymous field support | iro-tool | ~50 | Major kernel structs |

### Phase 2B: Core Infrastructure

| Item | Location | Est. Lines | Blocks |
|------|----------|------------|--------|
| `user_ptr<T>` | iro-core | ~80 | Syscall implementations |
| C-compatible `list_head` | iro-core | ~100 | List interop |
| Bitfield bit-level info | iro-tool | ~200 | Correct accessor shims |
| Enum extraction | iro-tool | ~150 | Type-safe enums |

### Phase 2C: Completeness

| Item | Location | Est. Lines | Priority |
|------|----------|------------|----------|
| Preprocessor constants | iro-tool | ~100 | Medium |
| Function pointer sigs | iro-tool | ~200 | Low |
| Packed/aligned metadata | iro-tool | ~50 | Low |

---

## Part 6: Testing Strategy

### Unit Tests

1. **container_of**: Verify offset computation for nested structs, const correctness
2. **MMIO**: Compile-time only (runtime needs hardware); verify barrier emission in asm
3. **user_ptr**: Verify no dereference compiles, copy functions type-check
4. **list_head**: Verify layout matches kernel, operations correct

### Integration Tests

1. **Anonymous fields**: Create manifest with dot-prefix fields, verify probe compiles
2. **Bitfields**: Create struct with bitfields, verify bit offsets match `-fdump-record-layouts`
3. **Enums**: Extract enum, compare values to kernel headers

### Cross-Platform

All tests run on both x86_64 and aarch64 (CI matrix).

---

## Appendix A: File Manifest

**New Files (iro-core)**:
- `overlay/iro/include/iro/intrusive/container_of.hpp`
- `overlay/iro/include/iro/arch/mmio.hpp`
- `overlay/iro/include/iro/arch/irq.hpp`
- `overlay/iro/include/iro/user/user_ptr.hpp`
- `overlay/iro/include/iro/c/list.hpp`

**Modified Files (iro-tool)**:
- `overlay/scripts/iro/iro_manifest.hpp` - Add enum/constant support
- `overlay/scripts/iro/gen_probe/gen_probe.cc` - Handle dot-prefix fields, emit enum/constant probes
- `overlay/scripts/iro/layout_parse/layout_parse.cc` - Parse new record types

---

## Appendix B: References

### Standards & Proposals
- [P3407R0: Make idiomatic usage of offsetof well-defined](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3407r0.html)
- [P0908R0: Offsetof for Pointers to Members](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0908r0.html)
- [P1382R1: volatile_load<T> and volatile_store<T>](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1382r1.pdf)

### Kernel Documentation
- [Linux kernel memory-barriers.txt](https://www.kernel.org/doc/Documentation/memory-barriers.txt)
- [Linux kernel design-patterns.txt](https://www.kernel.org/doc/Documentation/driver-model/design-patterns.txt)

### DWARF
- [LLVM D19630: DWARF4 bitfield support](https://reviews.llvm.org/D19630)
- [Go issue on DW_AT_data_bit_offset](https://github.com/golang/go/issues/46784)

### Implementations
- [avakar/container_of](https://github.com/avakar/container_of)
- [OSDev: Memory mapped registers](https://wiki.osdev.org/Memory_mapped_registers_in_C/C++)
- [makedumpfile: anonymous struct patch](http://www.mail-archive.com/kexec@lists.infradead.org/msg37420.html)
