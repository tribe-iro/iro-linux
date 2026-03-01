// Host-side ERR_PTR helpers mimicking kernel convention.
#include <stdint.h>
#include <iro/abi/errptr.h>

bool iro_is_err(const void* p) IRO_NOEXCEPT {
  long v = (long)(intptr_t)p;
  return v >= -4095 && v < 0;
}

long iro_ptr_err(const void* p) IRO_NOEXCEPT {
  return (long)(intptr_t)p;
}

void* iro_err_ptr(long neg_errno) IRO_NOEXCEPT {
  return (void*)(intptr_t)neg_errno;
}
