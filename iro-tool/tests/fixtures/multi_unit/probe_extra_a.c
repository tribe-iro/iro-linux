#include "probe_types.h"

struct holder_a {
  struct common_type v;
  int x;
};

int marker_a(void) {
  return sizeof(struct holder_a);
}
