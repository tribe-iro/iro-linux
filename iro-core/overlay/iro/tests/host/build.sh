#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname "$0")/../.." && pwd)"
SRC="${ROOT}/tests/host"
OUT="${ROOT}/tests/host/.build"
mkdir -p "${OUT}"

CXX=${CXX:-clang++}
if ! command -v "${CXX}" >/dev/null 2>&1; then
  CXX=g++
fi

CC=${CC:-clang}
if ! command -v "${CC}" >/dev/null 2>&1; then
  CC=gcc
fi

INCLUDES="-I${ROOT}/include"

${CC} -std=c11 -Wall -Wextra -pedantic ${INCLUDES} -c "${SRC}/mocks/abi_kmem_mock.c" -o "${OUT}/abi_kmem_mock.o"
${CC} -std=c11 -Wall -Wextra -pedantic ${INCLUDES} -c "${SRC}/mocks/abi_errptr_mock.c" -o "${OUT}/abi_errptr_mock.o"
${CC} -std=c11 -Wall -Wextra -pedantic ${INCLUDES} -c "${SRC}/mocks/abi_gfp_mock.c" -o "${OUT}/abi_gfp_mock.o"
${CC} -std=c11 -Wall -Wextra -pedantic ${INCLUDES} -c "${SRC}/mocks/abi_printk_mock.c" -o "${OUT}/abi_printk_mock.o"

${CXX} -std=c++23 -Wall -Wextra -pedantic \
  ${INCLUDES} \
  "${SRC}/cases/basic_test.cc" \
  "${OUT}/abi_kmem_mock.o" \
  "${OUT}/abi_errptr_mock.o" \
  "${OUT}/abi_gfp_mock.o" \
  "${OUT}/abi_printk_mock.o" \
  -o "${OUT}/iro_core_host_test"

# Freestanding header compile check with exceptions disabled.
NOEXC_SRC="${OUT}/freestanding_noexcept_check.cc"
cat > "${NOEXC_SRC}" <<'EOF'
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
EOF

${CXX} -std=c++23 -Wall -Wextra -pedantic -fno-exceptions \
  ${INCLUDES} \
  -c "${NOEXC_SRC}" \
  -o "${OUT}/freestanding_noexcept_check.o"

# Compile-only check for make_unexpected and TRY-family macros.
TRY_SRC="${OUT}/try_macros_compile_check.cc"
cat > "${TRY_SRC}" <<'EOF'
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
EOF

${CXX} -std=c++23 -Wall -Wextra -pedantic \
  ${INCLUDES} \
  -c "${TRY_SRC}" \
  -o "${OUT}/try_macros_compile_check.o"

# Runtime check: try_new must return error (not terminate) on throwing constructors.
TRY_NEW_SRC="${OUT}/try_new_throwing_ctor.cc"
cat > "${TRY_NEW_SRC}" <<'EOF'
#include <iro/iro.hpp>

struct throw_ctor {
  throw_ctor() { throw 7; }
};

int main() {
  auto res = iro::mem::try_new<throw_ctor>(iro::mem::gfp_kernel());
  return res.has_value() ? 1 : 0;
}
EOF

${CXX} -std=c++23 -Wall -Wextra -pedantic \
  ${INCLUDES} \
  "${TRY_NEW_SRC}" \
  "${OUT}/abi_kmem_mock.o" \
  "${OUT}/abi_gfp_mock.o" \
  -o "${OUT}/try_new_throwing_ctor_test"

"${OUT}/iro_core_host_test"
"${OUT}/try_new_throwing_ctor_test"
