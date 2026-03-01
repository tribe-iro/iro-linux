// Minimal host-side smoke test for IRO Core.
#include <assert.h>
#include <string.h>
#include <iro/iro.hpp>

namespace {

struct ThrowingMoveCopyable {
  ThrowingMoveCopyable() = default;
  ThrowingMoveCopyable(const ThrowingMoveCopyable&) = default;
  ThrowingMoveCopyable(ThrowingMoveCopyable&&) noexcept(false) {}
  ThrowingMoveCopyable& operator=(const ThrowingMoveCopyable&) = default;
  ThrowingMoveCopyable& operator=(ThrowingMoveCopyable&&) noexcept = default;
};

struct MoveOnlyThrowingMove {
  MoveOnlyThrowingMove() = default;
  MoveOnlyThrowingMove(const MoveOnlyThrowingMove&) = delete;
  MoveOnlyThrowingMove(MoveOnlyThrowingMove&&) noexcept(false) {}
};

using MoveIfNoexceptCopyRef =
    decltype(iro::freestanding::move_if_noexcept(*static_cast<ThrowingMoveCopyable*>(nullptr)));
using MoveIfNoexceptMoveRef =
    decltype(iro::freestanding::move_if_noexcept(*static_cast<MoveOnlyThrowingMove*>(nullptr)));

static_assert(iro::freestanding::is_same<MoveIfNoexceptCopyRef, const ThrowingMoveCopyable&>::value);
static_assert(iro::freestanding::is_same<MoveIfNoexceptMoveRef, MoveOnlyThrowingMove&&>::value);

struct ValueProbe {
  static int live;
  static bool throw_on_int_ctor;

  int value{0};

  ValueProbe() noexcept { ++live; }
  explicit ValueProbe(int v) : value(v) {
    if (throw_on_int_ctor) {
      throw_on_int_ctor = false;
      throw 1;
    }
    ++live;
  }
  ValueProbe(const ValueProbe& other) noexcept : value(other.value) { ++live; }
  ValueProbe(ValueProbe&& other) noexcept : value(other.value) { ++live; }
  ValueProbe& operator=(const ValueProbe&) = default;
  ValueProbe& operator=(ValueProbe&&) noexcept = default;
  ~ValueProbe() noexcept { --live; }
};

int ValueProbe::live = 0;
bool ValueProbe::throw_on_int_ctor = false;

struct ErrorProbe {
  static int live;
  static bool throw_on_copy;

  int code{0};

  ErrorProbe() noexcept { ++live; }
  explicit ErrorProbe(int c) noexcept : code(c) { ++live; }
  ErrorProbe(const ErrorProbe& other) : code(other.code) {
    if (throw_on_copy) {
      throw_on_copy = false;
      throw 2;
    }
    ++live;
  }
  ErrorProbe(ErrorProbe&& other) noexcept : code(other.code) { ++live; }
  ErrorProbe& operator=(const ErrorProbe&) = default;
  ErrorProbe& operator=(ErrorProbe&&) noexcept = default;
  ~ErrorProbe() noexcept { --live; }
};

int ErrorProbe::live = 0;
bool ErrorProbe::throw_on_copy = false;

struct SliceProbe {
  static int live;
  static int attempts;
  static int throw_at;

  int value{0};

  explicit SliceProbe(int v) : value(v) {
    ++attempts;
    if (throw_at > 0 && attempts == throw_at) {
      throw 3;
    }
    ++live;
  }
  SliceProbe(const SliceProbe& other) : value(other.value) { ++live; }
  SliceProbe(SliceProbe&& other) noexcept : value(other.value) { ++live; }
  SliceProbe& operator=(const SliceProbe&) = default;
  SliceProbe& operator=(SliceProbe&&) noexcept = default;
  ~SliceProbe() noexcept { --live; }
};

int SliceProbe::live = 0;
int SliceProbe::attempts = 0;
int SliceProbe::throw_at = 0;

static_assert(iro::freestanding::is_constructible<ValueProbe, int>::value);
static_assert(iro::freestanding::is_copy_constructible<ValueProbe>::value);
static_assert(iro::freestanding::is_move_constructible<ValueProbe>::value);
static_assert(iro::freestanding::is_nothrow_move_assignable<ValueProbe>::value);
static_assert(iro::freestanding::is_nothrow_swappable<ValueProbe>::value);

void reset_probes() {
  ValueProbe::live = 0;
  ValueProbe::throw_on_int_ctor = false;
  ErrorProbe::live = 0;
  ErrorProbe::throw_on_copy = false;
}

void test_expected_exception_safety() {
  reset_probes();
  {
    iro::expected<ValueProbe, ErrorProbe> lhs(ValueProbe(11));
    iro::expected<ValueProbe, ErrorProbe> rhs(iro::make_unexpected(ErrorProbe(7)));

    ErrorProbe::throw_on_copy = true;
    bool threw = false;
    try {
      lhs = rhs;
    } catch (...) {
      threw = true;
    }
    assert(threw);
    assert(lhs.has_value());
    assert(lhs.value().value == 11);

    lhs = rhs;
    assert(!lhs.has_value());
    assert(lhs.error().code == 7);

    iro::expected<ValueProbe, ErrorProbe> keep_value(ValueProbe(3));
    ValueProbe::throw_on_int_ctor = true;
    threw = false;
    try {
      keep_value.emplace(99);
    } catch (...) {
      threw = true;
    }
    assert(threw);
    assert(keep_value.has_value());
    assert(keep_value.value().value == 3);

    iro::expected<ValueProbe, ErrorProbe> keep_error(iro::make_unexpected(ErrorProbe(5)));
    ValueProbe::throw_on_int_ctor = true;
    threw = false;
    try {
      keep_error.emplace(42);
    } catch (...) {
      threw = true;
    }
    assert(threw);
    assert(!keep_error.has_value());
    assert(keep_error.error().code == 5);
  }

  assert(ValueProbe::live == 0);
  assert(ErrorProbe::live == 0);
}

void test_optional_exception_safety() {
  reset_probes();
  {
    iro::optional<ValueProbe> opt;
    opt.emplace(7);
    assert(opt.has_value());
    assert(opt.value().value == 7);

    ValueProbe::throw_on_int_ctor = true;
    bool threw = false;
    try {
      opt.emplace(9);
    } catch (...) {
      threw = true;
    }
    assert(threw);
    assert(!opt.has_value());
  }

  assert(ValueProbe::live == 0);
}

void test_boxed_slice_partial_construction() {
  SliceProbe::live = 0;
  SliceProbe::attempts = 0;
  SliceProbe::throw_at = 3;

  bool threw = false;
  try {
    auto result = iro::mem::make_boxed_slice<SliceProbe>(5, iro::mem::gfp_kernel(), 17);
    (void)result;
  } catch (...) {
    threw = true;
  }
  assert(threw);
  assert(SliceProbe::live == 0);

  SliceProbe::attempts = 0;
  SliceProbe::throw_at = 0;
  auto ok = iro::mem::make_boxed_slice<SliceProbe>(3, iro::mem::gfp_kernel(), 21);
  assert(ok.has_value());
  auto slice = iro::freestanding::move(ok.value());
  assert(slice.size() == 3);
  for (const auto& v : slice.as_span()) {
    assert(v.value == 21);
  }
  slice.reset();
  assert(SliceProbe::live == 0);
}

} // namespace

int main() {
  iro::expected<int, iro::errc> ok{1};
  assert(ok.has_value());
  assert(ok.value() == 1);

  iro::optional<int> opt{5};
  assert(static_cast<bool>(opt));
  assert(opt.value() == 5);

  char buf[64];
  auto written = iro::fmt::format_to(buf, sizeof(buf), IRO_FMT_STRING(2, "val {} hex {:x}"), 10, 0x2a);
  (void)written;
  assert(strcmp(buf, "val 10 hex 2a") == 0);

  auto slice_res = iro::mem::make_boxed_slice<int>(3, iro::mem::gfp_kernel(), 7);
  assert(slice_res.has_value());
  auto slice = iro::freestanding::move(slice_res.value());
  assert(slice.size() == 3);
  for (auto v : slice.as_span()) {
    assert(v == 7);
  }
  slice.reset();

  test_expected_exception_safety();
  test_optional_exception_safety();
  test_boxed_slice_partial_construction();

  iro::io::log(iro::io::log_level::info, IRO_FMT_STRING(1, "hello {}"), 42);
  return 0;
}
