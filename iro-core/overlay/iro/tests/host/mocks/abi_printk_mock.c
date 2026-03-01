// Host-side printk mock writing to stderr.
#include <stdio.h>
#include <iro/abi/printk.h>

void iro_printk_level(int level, const char* msg) IRO_NOEXCEPT {
  fprintf(stderr, "[%d] %s\n", level, msg ? msg : "");
}
