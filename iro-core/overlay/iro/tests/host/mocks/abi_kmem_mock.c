// Host-side mock for kmem ABI.
#include <stdlib.h>
#include <iro/abi/kmem.h>
#include <iro/abi/gfp.h>

void* iro_kmalloc(size_t size, unsigned int flags) IRO_NOEXCEPT {
  (void)flags;
  return malloc(size);
}

void* iro_krealloc(void* ptr, size_t size, unsigned int flags) IRO_NOEXCEPT {
  (void)flags;
  return realloc(ptr, size);
}

void iro_kfree(void* ptr) IRO_NOEXCEPT { free(ptr); }
