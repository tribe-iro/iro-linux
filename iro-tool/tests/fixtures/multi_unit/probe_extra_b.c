#include "probe_types.h"

struct holder_b {
  struct common_type v;
  int y;
};

int marker_b(void) {
  return sizeof(struct holder_b);
}
