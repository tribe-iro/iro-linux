// SPDX-License-Identifier: GPL-2.0-only
// Minimal DWARF reader for bitfield geometry and enum extraction (IRO v1.5).

#ifndef IRO_DWARF_READER_HPP_
#define IRO_DWARF_READER_HPP_

#include "../iro_common.hpp"
#include "../iro_manifest.hpp"

#include <map>
#include <string>
#include <vector>

namespace iro::dwarf {

struct BitfieldGeometry {
  std::uint64_t abs_bit_offset{0};
  std::uint16_t bit_size{0};
  std::uint8_t is_signed{0};
  std::uint64_t byte_offset{0};
  std::uint8_t bit_in_byte{0};
  std::uint8_t span_bytes{0};
};

struct EnumValue {
  std::string name;
  std::uint64_t raw{0};
  bool is_signed{false};
};

struct ExtractResult {
  std::map<std::string, std::map<std::string, BitfieldGeometry, std::less<>>, std::less<>>
      bitfields;
  std::map<std::string, std::vector<EnumValue>, std::less<>> enums_all;
};

ExtractResult extract_dwarf(const std::vector<std::byte>& elf, const Manifest& manifest);

}  // namespace iro::dwarf

#endif  // IRO_DWARF_READER_HPP_
