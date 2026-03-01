#include <iro/iro.hpp>

struct throw_ctor {
  throw_ctor() { throw 7; }
};

int main() {
  auto res = iro::mem::try_new<throw_ctor>(iro::mem::gfp_kernel());
  return res.has_value() ? 1 : 0;
}
