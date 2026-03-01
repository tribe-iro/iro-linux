#include <iro/freestanding/expected.hpp>
#include <iro/freestanding/optional.hpp>
#include <iro/mem/boxed_slice.hpp>

struct noexc_value {
  int v{0};
  noexc_value() noexcept = default;
  explicit noexc_value(int x) noexcept : v(x) {}
  noexc_value(const noexc_value&) noexcept = default;
  noexc_value(noexc_value&&) noexcept = default;
  noexc_value& operator=(const noexc_value&) noexcept = default;
  noexc_value& operator=(noexc_value&&) noexcept = default;
};

int main() {
  iro::freestanding::optional<noexc_value> opt;
  opt.emplace(1);

  iro::freestanding::expected<noexc_value, iro::err::errc> ex(noexc_value(3));
  ex.emplace(4);

  auto slice = iro::mem::make_boxed_slice<noexc_value>(2, iro::mem::gfp_kernel(), 7);
  return slice.has_value() ? 0 : 1;
}
