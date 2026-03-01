// Host-side mock for gfp ABI helpers.
#include <stddef.h>
#include <stdalign.h>
#include <iro/abi/gfp.h>

unsigned int iro_gfp_kernel(void) IRO_NOEXCEPT { return 0; }
unsigned int iro_gfp_atomic(void) IRO_NOEXCEPT { return 0; }
unsigned int iro_gfp_nowait(void) IRO_NOEXCEPT { return 0; }
unsigned int iro_gfp_noio(void) IRO_NOEXCEPT { return 0; }
unsigned int iro_gfp_nofs(void) IRO_NOEXCEPT { return 0; }
unsigned int iro_gfp_zero(void) IRO_NOEXCEPT { return 0; }
size_t iro_kmalloc_minalign(void) IRO_NOEXCEPT { return alignof(max_align_t); }
