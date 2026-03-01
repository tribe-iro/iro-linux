# IRO v1.5 Example Sets (sched/mm)

This file is illustrative only. The demo types below are pseudo headers to
show manifest shape, member designators, and the exact macro names produced
after escaping. Replace with real kernel types/fields for your target tree.

## Demo header: sched (pseudo)

```c
struct iro_sched_se {
  unsigned long exec_start;
  unsigned long vruntime;
};

struct iro_task {
  int pid;
  char comm[16];
  struct iro_sched_se se;
  union {
    struct {
      unsigned int anon_state:2;
      unsigned int anon_prio:6;
    };
    unsigned int raw_state;
  };
  struct {
    unsigned int policy:3;
    unsigned int flags:5;
  } sched_flags;
};

enum iro_task_state {
  IRO_TASK_RUNNING = 0,
  IRO_TASK_SLEEPING = 1,
};

#define SCHED_PRIO_MAX 140
```

## layout/sched.toml

```toml
schema_version = "1.5"
set = "sched"
includes = ["iro/demo_sched.h"]

[options]
bitfield_policy = "geometry"
allow_anonymous_members = true
allow_nested_designators = true
allow_array_subscripts = true
strict = true

[types.task]
c_type = "struct iro_task"
fields = [
  "pid",
  "comm[3]",
  "se.exec_start",
  "sched_flags.flags",
]
bitfields = [
  "anon_state",
  "anon_prio",
  "sched_flags.policy",
]

[enums.task_state]
c_type = "enum iro_task_state"
values = ["IRO_TASK_RUNNING", "IRO_TASK_SLEEPING"]

[constants]
SCHED_PRIO_MAX = { expr = "SCHED_PRIO_MAX" }
```

## Generated macro names (sched)

```c
IRO_LAYOUT_SCHEMA_MAJOR
IRO_LAYOUT_SCHEMA_MINOR
IRO_LAYOUT_INPUT_HASH64__sched

IRO_SIZEOF__struct_iro_task
IRO_ALIGNOF__struct_iro_task
IRO_OFFSETOF__struct_iro_task__pid
IRO_OFFSETOF__struct_iro_task__comm_3_
IRO_OFFSETOF__struct_iro_task__se_exec_start
IRO_OFFSETOF__struct_iro_task__sched_flags_flags

IRO_BF_ABS_BIT_OFFSET__struct_iro_task__anon_state
IRO_BF_BIT_SIZE__struct_iro_task__anon_state
IRO_BF_IS_SIGNED__struct_iro_task__anon_state
IRO_BF_BYTE_OFFSET__struct_iro_task__anon_state
IRO_BF_BIT_IN_BYTE__struct_iro_task__anon_state
IRO_BF_SPAN_BYTES__struct_iro_task__anon_state

IRO_BF_ABS_BIT_OFFSET__struct_iro_task__anon_prio
IRO_BF_BIT_SIZE__struct_iro_task__anon_prio
IRO_BF_IS_SIGNED__struct_iro_task__anon_prio
IRO_BF_BYTE_OFFSET__struct_iro_task__anon_prio
IRO_BF_BIT_IN_BYTE__struct_iro_task__anon_prio
IRO_BF_SPAN_BYTES__struct_iro_task__anon_prio

IRO_BF_ABS_BIT_OFFSET__struct_iro_task__sched_flags_policy
IRO_BF_BIT_SIZE__struct_iro_task__sched_flags_policy
IRO_BF_IS_SIGNED__struct_iro_task__sched_flags_policy
IRO_BF_BYTE_OFFSET__struct_iro_task__sched_flags_policy
IRO_BF_BIT_IN_BYTE__struct_iro_task__sched_flags_policy
IRO_BF_SPAN_BYTES__struct_iro_task__sched_flags_policy

IRO_ENUM__enum_iro_task_state__IRO_TASK_RUNNING
IRO_ENUM__enum_iro_task_state__IRO_TASK_SLEEPING

IRO_CONST__SCHED_PRIO_MAX
```

## Demo header: mm (pseudo)

```c
struct iro_vma {
  unsigned long start;
  unsigned long end;
};

struct iro_mm {
  struct iro_vma vmas[4];
  struct {
    unsigned int anon_tag:4;
    unsigned int anon_mode:4;
  };
  unsigned long pgd;
};

enum iro_vm_fault {
  IRO_VM_FAULT_NONE = 0,
  IRO_VM_FAULT_OOM = 12,
};

#define IRO_PAGE_SHIFT 12
#define IRO_MMU_GUARD  0xffffu
```

## layout/mm.toml

```toml
schema_version = "1.5"
set = "mm"
includes = ["iro/demo_mm.h"]

[options]
bitfield_policy = "geometry"
allow_anonymous_members = true
allow_nested_designators = true
allow_array_subscripts = true
strict = true

[types.mm]
c_type = "struct iro_mm"
fields = [
  "vmas[2].start",
  "pgd",
]
bitfields = [
  "anon_tag",
  "anon_mode",
]

[enums.vm_fault]
c_type = "enum iro_vm_fault"
extract_all = true

[constants]
IRO_PAGE_SHIFT = { expr = "IRO_PAGE_SHIFT" }
IRO_MMU_GUARD = { expr = "IRO_MMU_GUARD", type = "unsigned long" }
```

## Generated macro names (mm)

```c
IRO_LAYOUT_SCHEMA_MAJOR
IRO_LAYOUT_SCHEMA_MINOR
IRO_LAYOUT_INPUT_HASH64__mm

IRO_SIZEOF__struct_iro_mm
IRO_ALIGNOF__struct_iro_mm
IRO_OFFSETOF__struct_iro_mm__vmas_2_start
IRO_OFFSETOF__struct_iro_mm__pgd

IRO_BF_ABS_BIT_OFFSET__struct_iro_mm__anon_tag
IRO_BF_BIT_SIZE__struct_iro_mm__anon_tag
IRO_BF_IS_SIGNED__struct_iro_mm__anon_tag
IRO_BF_BYTE_OFFSET__struct_iro_mm__anon_tag
IRO_BF_BIT_IN_BYTE__struct_iro_mm__anon_tag
IRO_BF_SPAN_BYTES__struct_iro_mm__anon_tag

IRO_BF_ABS_BIT_OFFSET__struct_iro_mm__anon_mode
IRO_BF_BIT_SIZE__struct_iro_mm__anon_mode
IRO_BF_IS_SIGNED__struct_iro_mm__anon_mode
IRO_BF_BYTE_OFFSET__struct_iro_mm__anon_mode
IRO_BF_BIT_IN_BYTE__struct_iro_mm__anon_mode
IRO_BF_SPAN_BYTES__struct_iro_mm__anon_mode

IRO_ENUM__enum_iro_vm_fault__IRO_VM_FAULT_NONE
IRO_ENUM__enum_iro_vm_fault__IRO_VM_FAULT_OOM

IRO_CONST__IRO_PAGE_SHIFT
IRO_CONST__IRO_MMU_GUARD
```
