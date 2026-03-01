#include <iro/iro.hpp>

static iro::expected<int, iro::errc> fetch(bool ok) {
  if (ok) return 7;
  return iro::make_unexpected(iro::err::einval);
}

static iro::expected<int, iro::errc> run(bool ok) {
  int a = IRO_TRY(fetch(ok));
  IRO_TRY_VOID(fetch(true));
  int b = IRO_TRY_MAP(fetch(ok), [](iro::errc e) { return e; });
  return a + b;
}

int main() {
  auto e = iro::err::einval;
  auto u = iro::make_unexpected(e);
  (void)u;
  return run(true).has_value() ? 0 : 1;
}
