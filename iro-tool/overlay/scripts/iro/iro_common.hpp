// SPDX-License-Identifier: GPL-2.0-only
// IRO shared utilities for host tools.
// This header provides common functionality used by layout_parse, gen_probe, and depcheck.
//
// =============================================================================
// IRO Tool — Scope and Design Decisions (READ THIS)
// =============================================================================
//
// TARGET PLATFORMS (non-negotiable):
//   • x86_64 Linux (ELF64, little-endian)
//   • aarch64 Linux (ELF64, little-endian, LP64 ABI only)
//
// EXPLICITLY NOT SUPPORTED:
//   • Big-endian architectures (dead for modern Linux userspace)
//   • 32-bit architectures (including aarch64 ILP32)
//   • Non-Linux platforms
//   • Kernel versions < 5.0
//
// DESIGN PHILOSOPHY:
//   • Zero external dependencies — everything is self-contained
//   • Fail-fast with actionable diagnostics — no silent corruption
//   • Deterministic outputs — byte-for-byte reproducible builds
//   • Spec-driven — all behavior traces to IRO-TOOL-SPEC-4.2
//
// BUILD REQUIREMENTS:
//   • Clang 18+ (primary, tested configuration)
//   • GCC 14+ works but is not the tested configuration
//   • Linux host (for ELF header definitions)
//
// WHY CLANG:
//   • Superior diagnostics catch bugs GCC misses (-Weverything)
//   • First-class sanitizers (ASan/UBSan) essential for binary parsing code
//   • Faster compile times for rapid development iteration
//   • Full C++23 support without experimental flags
//
// WHY THESE CONSTRAINTS:
//   This tool solves a specific problem: letting C++ code access kernel struct
//   layouts without including kernel headers. The constraints above cover 99%+
//   of real-world kernel development. Supporting exotic configurations would
//   add complexity without proportional value.
//
// If you need big-endian, 32-bit, or non-Linux support, this tool is not for
// you. Fork it or use a different approach.
// =============================================================================

#ifndef IRO_COMMON_HPP_
#define IRO_COMMON_HPP_

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace iro {

// -----------------------------------------------------------------------------
// Version Axes and Constants (IRO-TOOL-SPEC-4.2)
// -----------------------------------------------------------------------------

// Layout/note schema compatibility for probe descriptor + generated outputs.
inline constexpr std::uint16_t kLayoutSchemaMajor = 4;
inline constexpr std::uint16_t kLayoutSchemaMinor = 2;
// Manifest language compatibility (schema_version in manifest TOML).
inline constexpr std::string_view kManifestSchemaVersionString = "1.5";
inline constexpr std::uint32_t kNoteType = 0x49524F01u;   // "IRO" + v1
inline constexpr std::uint32_t kDescMagic = 0x49524F4Cu;  // "IROL"

// Parser hardening limits (§10.5)
inline constexpr std::size_t kMaxDescSize = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxRecordCount = 200000;
inline constexpr std::size_t kMaxStringLen = 4096;
inline constexpr std::size_t kMaxSetNameLen = 256;
inline constexpr std::size_t kMaxDepfileBytes = 64 * 1024 * 1024;

// Tool versions - single source of truth
inline constexpr std::string_view kLayoutParseVersion = "1.5.0";
inline constexpr std::string_view kGenProbeVersion = "1.5.0";
inline constexpr std::string_view kDepcheckVersion = "1.5.0";

// -----------------------------------------------------------------------------
// Error Handling
// -----------------------------------------------------------------------------

struct ToolError : std::runtime_error {
  explicit ToolError(const std::string& msg) : std::runtime_error(msg) {}
  explicit ToolError(const char* msg) : std::runtime_error(msg) {}
};

[[noreturn]] inline void fatal(const std::string& msg) {
  throw ToolError(msg);
}

[[noreturn]] inline void fatal(const std::string& msg, const std::string& hint) {
  throw ToolError(msg + " — " + hint);
}

// -----------------------------------------------------------------------------
// Checked Arithmetic (§10.5)
// -----------------------------------------------------------------------------

inline std::size_t checked_add(std::size_t a, std::size_t b, std::string_view what) {
  if (a > std::numeric_limits<std::size_t>::max() - b) {
    fatal("overflow while computing " + std::string(what));
  }
  return a + b;
}

inline std::size_t checked_mul(std::size_t a, std::size_t b, std::string_view what) {
  if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
    fatal("overflow while computing " + std::string(what));
  }
  return a * b;
}

inline std::uint32_t checked_add_u32(std::uint32_t a, std::uint32_t b, std::string_view what) {
  if (a > std::numeric_limits<std::uint32_t>::max() - b) {
    fatal("overflow while computing " + std::string(what));
  }
  return a + b;
}

inline std::uint32_t checked_mul_u32(std::uint32_t a, std::uint32_t b, std::string_view what) {
  if (a != 0 && b > std::numeric_limits<std::uint32_t>::max() / a) {
    fatal("overflow while computing " + std::string(what));
  }
  return a * b;
}

// -----------------------------------------------------------------------------
// UTF-8 Validation
// -----------------------------------------------------------------------------

inline bool is_valid_utf8(std::string_view s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  const auto* end = p + s.size();
  while (p < end) {
    unsigned char c = *p++;
    if (c <= 0x7F) continue;
    int need = 0;
    std::uint32_t code = 0;
    std::uint32_t min = 0;
    if ((c & 0xE0) == 0xC0) {
      need = 1;
      code = c & 0x1F;
      min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
      need = 2;
      code = c & 0x0F;
      min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
      need = 3;
      code = c & 0x07;
      min = 0x10000;
    } else {
      return false;
    }
    if (end - p < need) return false;
    for (int i = 0; i < need; ++i) {
      unsigned char t = *p++;
      if ((t & 0xC0) != 0x80) return false;
      code = (code << 6) | (t & 0x3F);
    }
    if (code < min) return false;               // overlong
    if (code > 0x10FFFF) return false;
    if (code >= 0xD800 && code <= 0xDFFF) return false;  // surrogates
  }
  return true;
}

inline void require_utf8(std::string_view what, std::string_view s) {
  if (!is_valid_utf8(s)) {
    fatal(std::string(what) + ": invalid UTF-8");
  }
  if (s.find('\0') != std::string_view::npos) {
    fatal(std::string(what) + ": contains NUL byte");
  }
}

// -----------------------------------------------------------------------------
// Set Name Validation (§5.2)
// -----------------------------------------------------------------------------

inline void require_safe_set_name(std::string_view set) {
  if (set.empty()) {
    fatal("manifest: set name is empty");
  }
  if (set.size() > kMaxSetNameLen) {
    fatal("manifest: set name exceeds maximum length (" + std::to_string(kMaxSetNameLen) + ")");
  }
  for (char c : set) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    if (!ok) {
      fatal("manifest: set contains invalid character '" + std::string(1, c) +
            "' (allowed: [A-Za-z0-9_])");
    }
  }
}

// -----------------------------------------------------------------------------
// Identifier Escaping (§11.4)
// -----------------------------------------------------------------------------

inline std::string escape_identifier(std::string_view src) {
  std::string out;
  if (!src.empty() && std::isdigit(static_cast<unsigned char>(src.front()))) {
    out.push_back('_');
  }
  bool last_us = false;
  for (char c : src) {
    bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    char mapped = ok ? c : '_';
    if (mapped == '_' && last_us) continue;  // collapse multiple underscores
    out.push_back(mapped);
    last_us = (mapped == '_');
  }
  if (out.empty()) out = "_";
  return out;
}

// -----------------------------------------------------------------------------
// SHA-256 Implementation (Compact, Public Domain Style)
// Used for computing input hashes per §6.5 and bitfield accessor IDs per §8.4.1
// -----------------------------------------------------------------------------

class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() {
    h_[0] = 0x6a09e667u;
    h_[1] = 0xbb67ae85u;
    h_[2] = 0x3c6ef372u;
    h_[3] = 0xa54ff53au;
    h_[4] = 0x510e527fu;
    h_[5] = 0x9b05688cu;
    h_[6] = 0x1f83d9abu;
    h_[7] = 0x5be0cd19u;
    sz_ = 0;
    buflen_ = 0;
  }

  void update(std::span<const std::byte> data) {
    for (auto b : data) {
      buf_[buflen_++] = static_cast<std::uint8_t>(b);
      if (buflen_ == 64) {
        compress(buf_.data());
        buflen_ = 0;
        sz_ += 512;
      }
    }
  }

  void update(std::string_view s) {
    update(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size()));
  }

  void update(const std::string& s) {
    update(std::string_view(s));
  }

  std::array<std::uint8_t, 32> finish() {
    std::uint64_t bits = sz_ + static_cast<std::uint64_t>(buflen_) * 8;
    buf_[buflen_++] = 0x80;
    if (buflen_ > 56) {
      while (buflen_ < 64) buf_[buflen_++] = 0;
      compress(buf_.data());
      buflen_ = 0;
    }
    while (buflen_ < 56) buf_[buflen_++] = 0;
    for (int i = 7; i >= 0; --i) {
      buf_[buflen_++] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xffu);
    }
    compress(buf_.data());

    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i) {
      out[i * 4 + 0] = static_cast<std::uint8_t>((h_[i] >> 24) & 0xffu);
      out[i * 4 + 1] = static_cast<std::uint8_t>((h_[i] >> 16) & 0xffu);
      out[i * 4 + 2] = static_cast<std::uint8_t>((h_[i] >> 8) & 0xffu);
      out[i * 4 + 3] = static_cast<std::uint8_t>(h_[i] & 0xffu);
    }
    return out;
  }

 private:
  static constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }

  void compress(const std::uint8_t* chunk) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
        0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
        0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(chunk[i * 4]) << 24) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    std::uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];

    for (int i = 0; i < 64; ++i) {
      auto S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      auto ch = (e & f) ^ ((~e) & g);
      auto temp1 = h + S1 + ch + k[i] + w[i];
      auto S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      auto maj = (a & b) ^ (a & c) ^ (b & c);
      auto temp2 = S0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
  }

  std::uint32_t h_[8]{};
  std::array<std::uint8_t, 64> buf_{};
  std::uint64_t sz_{0};
  std::size_t buflen_{0};
};

// Convenience functions
inline std::array<std::uint8_t, 32> sha256_bytes(std::span<const std::byte> data) {
  Sha256 ctx;
  ctx.update(data);
  return ctx.finish();
}

inline std::array<std::uint8_t, 32> sha256_string(std::string_view s) {
  Sha256 ctx;
  ctx.update(s);
  return ctx.finish();
}

inline std::array<std::uint8_t, 32> sha256_concat(
    const std::vector<std::span<const std::byte>>& parts) {
  Sha256 ctx;
  for (auto part : parts) {
    ctx.update(part);
  }
  return ctx.finish();
}

// Extract low 64 bits as little-endian uint64
inline std::uint64_t hash256_to_hash64(const std::array<std::uint8_t, 32>& h256) {
  std::uint64_t h64 = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    h64 |= (static_cast<std::uint64_t>(h256[i]) << (i * 8));
  }
  return h64;
}

// -----------------------------------------------------------------------------
// SHA-256 Implementation Verification (NIST FIPS 180-4 Test Vectors)
// -----------------------------------------------------------------------------
// These are the official NIST test vectors. If any fail, the implementation is
// broken and all hash-dependent operations (input_hash64, accessor IDs) are
// invalid. We verify once at first use; subsequent calls are no-ops.
//
// Test vectors from: https://csrc.nist.gov/CSRC/media/Projects/
//                    Cryptographic-Standards-and-Guidelines/documents/examples/SHA256.pdf

namespace detail {

// NIST test vector 1: empty string ""
// SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
inline constexpr std::array<std::uint8_t, 32> kSha256Empty = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
    0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};

// NIST test vector 2: "abc"
// SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
inline constexpr std::array<std::uint8_t, 32> kSha256Abc = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

// NIST test vector 3: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" (448 bits)
// SHA-256(...) = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
inline constexpr std::array<std::uint8_t, 32> kSha256_448bit = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93,
    0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};

inline bool g_sha256_verified = false;

}  // namespace detail

// Verify SHA-256 implementation against NIST test vectors.
// Called once on first use. Throws ToolError if verification fails.
// This is a defense-in-depth measure — if someone corrupts the round constants
// or the compress function, we catch it immediately rather than producing
// silently wrong hashes that break build reproducibility.
inline void verify_sha256_implementation() {
  if (detail::g_sha256_verified) return;

  auto check = [](std::string_view input, const std::array<std::uint8_t, 32>& expected,
                  const char* name) {
    const auto actual = sha256_string(input);
    if (actual != expected) {
      // Don't use fatal() here — provide maximum diagnostic information
      std::ostringstream msg;
      msg << "CRITICAL: SHA-256 implementation verification failed for " << name << "\n";
      msg << "  Input:    \"" << input << "\" (" << input.size() << " bytes)\n";
      msg << "  Expected: ";
      for (auto b : expected) msg << std::hex << std::setfill('0') << std::setw(2) << (int)b;
      msg << "\n  Actual:   ";
      for (auto b : actual) msg << std::hex << std::setfill('0') << std::setw(2) << (int)b;
      msg << "\nThis indicates a corrupted binary or compiler bug. Do not proceed.";
      throw ToolError(msg.str());
    }
  };

  check("", detail::kSha256Empty, "empty string");
  check("abc", detail::kSha256Abc, "\"abc\"");
  check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        detail::kSha256_448bit, "448-bit message");

  detail::g_sha256_verified = true;
}

// -----------------------------------------------------------------------------
// ELF Machine Type Validation (Cross-Compilation Safety)
// -----------------------------------------------------------------------------
// Maps architecture strings (from probe.cmd) to expected ELF e_machine values.
// This catches the critical bug where a probe is compiled for the wrong arch,
// producing silently incorrect struct offsets.

inline constexpr std::uint16_t kEM_X86_64 = 62;    // EM_X86_64
inline constexpr std::uint16_t kEM_AARCH64 = 183;  // EM_AARCH64

struct ArchInfo {
  std::string_view name;
  std::uint16_t e_machine;
  std::string_view description;
};

inline constexpr std::array<ArchInfo, 2> kSupportedArchs = {{
    {"x86_64", kEM_X86_64, "AMD x86-64 / Intel 64"},
    {"aarch64", kEM_AARCH64, "ARM 64-bit (AArch64)"},
}};

// Returns the expected e_machine value for an architecture string.
// Returns 0 if the architecture is not recognized.
inline std::uint16_t arch_to_e_machine(std::string_view arch) {
  for (const auto& info : kSupportedArchs) {
    if (info.name == arch) return info.e_machine;
  }
  return 0;
}

// Returns a human-readable name for an e_machine value.
inline std::string_view e_machine_to_name(std::uint16_t e_machine) {
  for (const auto& info : kSupportedArchs) {
    if (info.e_machine == e_machine) return info.name;
  }
  return "unknown";
}

// Validates that an ELF file's e_machine matches the expected architecture.
// This is the key cross-compilation safety check.
inline void validate_elf_machine(std::uint16_t actual_e_machine, std::string_view expected_arch) {
  const auto expected_e_machine = arch_to_e_machine(expected_arch);

  if (expected_e_machine == 0) {
    fatal("unsupported target architecture: '" + std::string(expected_arch) + "'",
          "supported architectures: x86_64, aarch64");
  }

  if (actual_e_machine != expected_e_machine) {
    std::ostringstream msg;
    msg << "ELF e_machine mismatch: probe object is "
        << e_machine_to_name(actual_e_machine) << " (0x" << std::hex << actual_e_machine
        << "), but probe.cmd specifies " << expected_arch << " (0x" << expected_e_machine << ")";

    std::ostringstream hint;
    hint << "the probe was compiled for the wrong architecture. "
         << "Ensure CROSS_COMPILE and ARCH are set correctly, or rebuild the probe";

    fatal(msg.str(), hint.str());
  }
}

// -----------------------------------------------------------------------------
// Hex Encoding/Decoding
// -----------------------------------------------------------------------------

inline std::string to_hex(std::span<const std::uint8_t> bytes) {
  std::ostringstream oss;
  for (auto b : bytes) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return oss.str();
}

inline std::string format_hex64(std::uint64_t v) {
  std::ostringstream oss;
  oss << std::hex << std::setw(16) << std::setfill('0') << v;
  return oss.str();
}

inline bool is_lower_hex(std::string_view s) {
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

inline std::vector<std::uint8_t> decode_hex(std::string_view hex) {
  if (hex.size() % 2 != 0) {
    fatal("hex decode requires even length");
  }
  std::vector<std::uint8_t> out;
  out.reserve(hex.size() / 2);
  auto nybble = [](char c) -> std::uint8_t {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + (c - 'A'));
    fatal("non-hex digit in input");
  };
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    out.push_back(static_cast<std::uint8_t>((nybble(hex[i]) << 4) | nybble(hex[i + 1])));
  }
  return out;
}

// -----------------------------------------------------------------------------
// File I/O Utilities
// -----------------------------------------------------------------------------

namespace fs = std::filesystem;

inline std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) fatal("failed to open: " + p.string());
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline std::vector<std::byte> read_bytes(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) fatal("failed to open: " + p.string());
  f.seekg(0, std::ios::end);
  const auto size = f.tellg();
  if (size < 0) fatal("failed to stat file: " + p.string());
  f.seekg(0, std::ios::beg);
  std::vector<std::byte> buf(static_cast<std::size_t>(size));
  if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) {
    fatal("failed to read file: " + p.string());
  }
  return buf;
}

inline std::string read_text_with_limit(const fs::path& p, std::size_t max_bytes) {
  std::error_code ec;
  const auto sz = fs::file_size(p, ec);
  if (!ec && sz > max_bytes) {
    fatal("file too large: " + p.string() + " (max " + std::to_string(max_bytes) + " bytes)");
  }
  return read_text(p);
}

inline void atomic_write(const fs::path& dst, const std::string& content) {
  const fs::path tmp = dst.parent_path() / (dst.filename().string() + ".tmp");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) fatal("failed to open for write: " + tmp.string());
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!f) fatal("failed to write: " + tmp.string());
  }
  fs::rename(tmp, dst);
}

inline void write_if_changed(const fs::path& dst, const std::string& content) {
  if (fs::exists(dst)) {
    std::string existing = read_text(dst);
    if (existing == content) return;
  }
  fs::create_directories(dst.parent_path());
  atomic_write(dst, content);
}

// -----------------------------------------------------------------------------
// Path Normalization
// -----------------------------------------------------------------------------

inline std::string normalize_path(std::string_view p) {
  std::vector<std::string> components;
  std::string current;
  for (char c : p) {
    if (c == '/') {
      if (!current.empty() && current != ".") {
        if (current == "..") {
          if (!components.empty()) components.pop_back();
        } else {
          components.push_back(current);
        }
      }
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty() && current != ".") {
    if (current == "..") {
      if (!components.empty()) components.pop_back();
    } else {
      components.push_back(current);
    }
  }
  std::ostringstream joined;
  for (std::size_t i = 0; i < components.size(); ++i) {
    if (i) joined << '/';
    joined << components[i];
  }
  return joined.str();
}

}  // namespace iro

#endif  // IRO_COMMON_HPP_
