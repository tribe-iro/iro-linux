// SPDX-License-Identifier: GPL-2.0-only
// Minimal DWARF reader for bitfield geometry and enum extraction (IRO v1.5).

#include "dwarf_reader.hpp"

#include <elf.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace iro::dwarf {
namespace {

// DWARF tags
constexpr std::uint64_t DW_TAG_array_type = 0x01;
constexpr std::uint64_t DW_TAG_enumeration_type = 0x04;
constexpr std::uint64_t DW_TAG_member = 0x0d;
constexpr std::uint64_t DW_TAG_pointer_type = 0x0f;
constexpr std::uint64_t DW_TAG_structure_type = 0x13;
constexpr std::uint64_t DW_TAG_typedef = 0x16;
constexpr std::uint64_t DW_TAG_union_type = 0x17;
constexpr std::uint64_t DW_TAG_base_type = 0x24;
constexpr std::uint64_t DW_TAG_const_type = 0x26;
constexpr std::uint64_t DW_TAG_enumerator = 0x28;
constexpr std::uint64_t DW_TAG_volatile_type = 0x53;
constexpr std::uint64_t DW_TAG_restrict_type = 0x55;
constexpr std::uint64_t DW_TAG_atomic_type = 0x47;

// DWARF attributes
constexpr std::uint64_t DW_AT_name = 0x03;
constexpr std::uint64_t DW_AT_byte_size = 0x0b;
constexpr std::uint64_t DW_AT_bit_offset = 0x0c;
constexpr std::uint64_t DW_AT_bit_size = 0x0d;
constexpr std::uint64_t DW_AT_const_value = 0x1c;
constexpr std::uint64_t DW_AT_data_member_location = 0x38;
constexpr std::uint64_t DW_AT_declaration = 0x3c;
constexpr std::uint64_t DW_AT_encoding = 0x3e;
constexpr std::uint64_t DW_AT_type = 0x49;
constexpr std::uint64_t DW_AT_data_bit_offset = 0x6c;
constexpr std::uint64_t DW_AT_str_offsets_base = 0x72;

// DWARF forms
constexpr std::uint64_t DW_FORM_addr = 0x01;
constexpr std::uint64_t DW_FORM_block2 = 0x03;
constexpr std::uint64_t DW_FORM_block4 = 0x04;
constexpr std::uint64_t DW_FORM_data2 = 0x05;
constexpr std::uint64_t DW_FORM_data4 = 0x06;
constexpr std::uint64_t DW_FORM_data8 = 0x07;
constexpr std::uint64_t DW_FORM_string = 0x08;
constexpr std::uint64_t DW_FORM_block = 0x09;
constexpr std::uint64_t DW_FORM_block1 = 0x0a;
constexpr std::uint64_t DW_FORM_data1 = 0x0b;
constexpr std::uint64_t DW_FORM_flag = 0x0c;
constexpr std::uint64_t DW_FORM_sdata = 0x0d;
constexpr std::uint64_t DW_FORM_strp = 0x0e;
constexpr std::uint64_t DW_FORM_udata = 0x0f;
constexpr std::uint64_t DW_FORM_ref_addr = 0x10;
constexpr std::uint64_t DW_FORM_ref1 = 0x11;
constexpr std::uint64_t DW_FORM_ref2 = 0x12;
constexpr std::uint64_t DW_FORM_ref4 = 0x13;
constexpr std::uint64_t DW_FORM_ref8 = 0x14;
constexpr std::uint64_t DW_FORM_ref_udata = 0x15;
constexpr std::uint64_t DW_FORM_indirect = 0x16;
constexpr std::uint64_t DW_FORM_sec_offset = 0x17;
constexpr std::uint64_t DW_FORM_exprloc = 0x18;
constexpr std::uint64_t DW_FORM_flag_present = 0x19;
constexpr std::uint64_t DW_FORM_strx = 0x1a;
constexpr std::uint64_t DW_FORM_implicit_const = 0x21;
constexpr std::uint64_t DW_FORM_strx1 = 0x25;
constexpr std::uint64_t DW_FORM_strx2 = 0x26;
constexpr std::uint64_t DW_FORM_strx3 = 0x27;
constexpr std::uint64_t DW_FORM_strx4 = 0x28;

// DWARF operations
constexpr std::uint8_t DW_OP_plus = 0x22;
constexpr std::uint8_t DW_OP_plus_uconst = 0x23;
constexpr std::uint8_t DW_OP_constu = 0x10;
constexpr std::uint8_t DW_OP_consts = 0x11;

// DWARF encodings
constexpr std::uint64_t DW_ATE_boolean = 0x02;
constexpr std::uint64_t DW_ATE_signed = 0x05;
constexpr std::uint64_t DW_ATE_signed_char = 0x06;
constexpr std::uint64_t DW_ATE_unsigned = 0x07;
constexpr std::uint64_t DW_ATE_unsigned_char = 0x08;
constexpr std::uint64_t DW_ATE_unsigned_fixed = 0x0e;
constexpr std::uint64_t DW_ATE_signed_fixed = 0x0f;

// DWARF unit types (DWARF5)
constexpr std::uint8_t DW_UT_type = 0x02;
constexpr std::uint8_t DW_UT_split_type = 0x06;

struct DwarfSections {
  std::vector<std::byte> info_owned;
  std::span<const std::byte> info;
  std::span<const std::byte> abbrev;
  std::span<const std::byte> str;
  std::span<const std::byte> str_offsets;
  std::unordered_map<std::size_t, std::uint64_t> info_relocations;
};

inline std::size_t checked_off_add(std::size_t base, std::size_t delta, std::string_view what) {
  return checked_add(base, delta, what);
}

inline std::size_t checked_off_mul(std::size_t a, std::size_t b, std::string_view what) {
  return checked_mul(a, b, what);
}

template <class T>
T read_obj(std::span<const std::byte> buf, std::size_t offset) {
  if (checked_off_add(offset, sizeof(T), "DWARF object read end") > buf.size()) {
    fatal("DWARF read_obj: out of bounds");
  }
  T obj{};
  std::memcpy(&obj, buf.data() + offset, sizeof(T));
  return obj;
}

void write_u_le(std::vector<std::byte>& buf, std::size_t off, std::uint64_t value, std::size_t size) {
  if (checked_off_add(off, size, "DWARF relocation write end") > buf.size()) {
    fatal("DWARF relocation write out of bounds");
  }
  for (std::size_t i = 0; i < size; ++i) {
    buf[off + i] = std::byte((value >> (i * 8)) & 0xffu);
  }
}

void apply_debug_info_relocations(const std::span<const std::byte> buf_span,
                                  const std::span<const std::byte> debug_info_src,
                                  const Elf64_Shdr& rela_sh,
                                  const Elf64_Shdr& symtab_sh,
                                  std::vector<std::byte>& debug_info_out,
                                  std::unordered_map<std::size_t, std::uint64_t>& reloc_values) {
  debug_info_out.assign(debug_info_src.begin(), debug_info_src.end());

  if (rela_sh.sh_entsize < sizeof(Elf64_Rela)) {
    fatal("DWARF: invalid .rela.debug_info entry size");
  }
  if (symtab_sh.sh_entsize < sizeof(Elf64_Sym)) {
    fatal("DWARF: invalid symbol table entry size");
  }

  const std::size_t rela_count = rela_sh.sh_size / rela_sh.sh_entsize;
  const std::size_t sym_count = symtab_sh.sh_size / symtab_sh.sh_entsize;

  for (std::size_t i = 0; i < rela_count; ++i) {
    const std::size_t rela_off = checked_off_add(
        static_cast<std::size_t>(rela_sh.sh_offset),
        checked_off_mul(i, static_cast<std::size_t>(rela_sh.sh_entsize),
                        "DWARF relocation table entry offset"),
        "DWARF relocation entry offset");
    const Elf64_Rela rela = read_obj<Elf64_Rela>(buf_span, rela_off);

    const std::size_t sym_index = static_cast<std::size_t>(ELF64_R_SYM(rela.r_info));
    if (sym_index >= sym_count) {
      fatal("DWARF: relocation symbol index out of range");
    }
    const std::size_t sym_off = checked_off_add(
        static_cast<std::size_t>(symtab_sh.sh_offset),
        checked_off_mul(sym_index, static_cast<std::size_t>(symtab_sh.sh_entsize),
                        "DWARF symbol table entry offset"),
        "DWARF symbol entry offset");
    const Elf64_Sym sym = read_obj<Elf64_Sym>(buf_span, sym_off);

    const std::uint64_t value =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(sym.st_value) + rela.r_addend);
    const std::uint32_t type = static_cast<std::uint32_t>(ELF64_R_TYPE(rela.r_info));
    const std::size_t patch_off = static_cast<std::size_t>(rela.r_offset);
    reloc_values[patch_off] = value;

    bool patched = false;
#ifdef R_X86_64_64
    if (type == R_X86_64_64) {
      write_u_le(debug_info_out, patch_off, value, 8);
      patched = true;
    }
#endif
#ifdef R_X86_64_32
    if (type == R_X86_64_32) {
      write_u_le(debug_info_out, patch_off, value, 4);
      patched = true;
    }
#endif
#ifdef R_X86_64_32S
    if (type == R_X86_64_32S) {
      write_u_le(debug_info_out, patch_off, value, 4);
      patched = true;
    }
#endif
#ifdef R_AARCH64_ABS64
    if (type == R_AARCH64_ABS64) {
      write_u_le(debug_info_out, patch_off, value, 8);
      patched = true;
    }
#endif
#ifdef R_AARCH64_ABS32
    if (type == R_AARCH64_ABS32) {
      write_u_le(debug_info_out, patch_off, value, 4);
      patched = true;
    }
#endif

    // Ignore relocation kinds not required for DWARF structural parsing.
    if (!patched) {
      continue;
    }
  }
}

std::uint64_t read_u(std::span<const std::byte> buf, std::size_t& off, std::size_t size) {
  if (checked_off_add(off, size, "DWARF scalar read end") > buf.size()) {
    fatal("DWARF parse overflow");
  }
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < size; ++i) {
    v |= (std::uint64_t(std::to_integer<std::uint8_t>(buf[off + i])) << (i * 8));
  }
  off += size;
  return v;
}

std::uint64_t read_uleb(std::span<const std::byte> buf, std::size_t& off) {
  std::uint64_t result = 0;
  std::uint32_t shift = 0;
  while (true) {
    if (off >= buf.size()) fatal("DWARF uleb overflow");
    std::uint8_t byte = std::to_integer<std::uint8_t>(buf[off++]);
    result |= (std::uint64_t(byte & 0x7f) << shift);
    if ((byte & 0x80) == 0) break;
    shift += 7;
    if (shift > 63) fatal("DWARF uleb too large");
  }
  return result;
}

std::int64_t read_sleb(std::span<const std::byte> buf, std::size_t& off) {
  std::int64_t result = 0;
  std::uint32_t shift = 0;
  std::uint8_t byte = 0;
  while (true) {
    if (off >= buf.size()) fatal("DWARF sleb overflow");
    byte = std::to_integer<std::uint8_t>(buf[off++]);
    result |= (std::int64_t(byte & 0x7f) << shift);
    shift += 7;
    if ((byte & 0x80) == 0) break;
    if (shift > 63) fatal("DWARF sleb too large");
  }
  if ((shift < 64) && (byte & 0x40)) {
    result |= -((std::int64_t)1 << shift);
  }
  return result;
}

std::string read_cstr(std::span<const std::byte> buf, std::size_t& off) {
  std::size_t start = off;
  while (off < buf.size()) {
    if (std::to_integer<std::uint8_t>(buf[off]) == 0) {
      std::string out(reinterpret_cast<const char*>(buf.data() + start), off - start);
      ++off;
      return out;
    }
    ++off;
  }
  fatal("DWARF unterminated string");
}

std::string read_strp(std::span<const std::byte> str, std::uint64_t off) {
  if (off >= str.size()) {
    fatal("DWARF strp out of range");
  }
  const char* p = reinterpret_cast<const char*>(str.data() + off);
  return std::string(p, strnlen(p, str.size() - off));
}

struct AbbrevAttr {
  std::uint64_t name{0};
  std::uint64_t form{0};
  bool has_implicit_const{false};
  std::int64_t implicit_const{0};
};

struct Abbrev {
  std::uint64_t code{0};
  std::uint64_t tag{0};
  bool has_children{false};
  std::vector<AbbrevAttr> attrs;
};

using AbbrevTable = std::unordered_map<std::uint64_t, Abbrev>;

AbbrevTable parse_abbrev_table(std::span<const std::byte> abbrev, std::size_t offset) {
  AbbrevTable table;
  std::size_t off = offset;

  while (off < abbrev.size()) {
    std::uint64_t code = read_uleb(abbrev, off);
    if (code == 0) break;
    Abbrev a;
    a.code = code;
    a.tag = read_uleb(abbrev, off);
    if (off >= abbrev.size()) fatal("DWARF abbrev overflow");
    a.has_children = (std::to_integer<std::uint8_t>(abbrev[off++]) != 0);

    while (true) {
      std::uint64_t name = read_uleb(abbrev, off);
      std::uint64_t form = read_uleb(abbrev, off);
      if (name == 0 && form == 0) break;
      AbbrevAttr attr{.name = name, .form = form};
      if (form == DW_FORM_implicit_const) {
        attr.has_implicit_const = true;
        attr.implicit_const = read_sleb(abbrev, off);
      }
      a.attrs.push_back(attr);
    }

    table.emplace(a.code, std::move(a));
  }

  return table;
}

struct AttrValue {
  // Ref values are absolute offsets in .debug_info.
  enum class Kind { Unsigned, Signed, String, Ref, Flag, Expr, Strx, Block };
  Kind kind{Kind::Unsigned};
  std::uint64_t u{0};
  std::int64_t s{0};
  std::string str;
  std::vector<std::byte> bytes;
  bool flag{false};
};

struct Unit {
  std::uint64_t offset{0};
  std::uint64_t end{0};
  std::uint64_t abbrev_offset{0};
  std::uint16_t version{0};
  std::uint8_t address_size{0};
  std::uint8_t unit_type{0};
  std::uint64_t offset_size{4};
  std::uint64_t str_offsets_base{0};
};

struct Die {
  std::uint64_t offset{0};
  std::uint64_t tag{0};
  bool has_children{false};
  std::uint32_t depth{0};
  std::uint64_t parent_offset{0};
  std::size_t unit_index{0};
  std::map<std::uint64_t, AttrValue, std::less<>> attrs;
};

std::uint64_t relocate_if_needed(const DwarfSections& sections, std::size_t field_off,
                                 std::uint64_t raw) {
  auto it = sections.info_relocations.find(field_off);
  if (it != sections.info_relocations.end()) {
    return it->second;
  }
  return raw;
}

AttrValue read_attr_value(std::span<const std::byte> info, const DwarfSections& sections,
                          const Unit& unit, const AbbrevAttr& spec, std::size_t& off);

AttrValue read_attr_value(std::span<const std::byte> info, const DwarfSections& sections,
                          const Unit& unit, const AbbrevAttr& spec, std::size_t& off) {
  AttrValue out;

  if (spec.form == DW_FORM_implicit_const) {
    out.kind = AttrValue::Kind::Signed;
    out.s = spec.implicit_const;
    return out;
  }

  auto read_block = [&](std::size_t len) {
    if (checked_off_add(off, len, "DWARF block end") > info.size()) fatal("DWARF block overflow");
    std::vector<std::byte> bytes(len);
    if (len > 0) {
      std::memcpy(bytes.data(), info.data() + off, len);
    }
    off += len;
    out.kind = AttrValue::Kind::Block;
    out.bytes = std::move(bytes);
  };

  switch (spec.form) {
    case DW_FORM_addr:
      out.kind = AttrValue::Kind::Unsigned;
      {
        const std::size_t field_off = off;
        out.u = relocate_if_needed(sections, field_off, read_u(info, off, unit.address_size));
      }
      return out;
    case DW_FORM_data1:
      out.kind = AttrValue::Kind::Unsigned;
      out.u = read_u(info, off, 1);
      return out;
    case DW_FORM_data2:
      out.kind = AttrValue::Kind::Unsigned;
      out.u = read_u(info, off, 2);
      return out;
    case DW_FORM_data4:
      out.kind = AttrValue::Kind::Unsigned;
      {
        const std::size_t field_off = off;
        out.u = relocate_if_needed(sections, field_off, read_u(info, off, 4));
      }
      return out;
    case DW_FORM_data8:
      out.kind = AttrValue::Kind::Unsigned;
      {
        const std::size_t field_off = off;
        out.u = relocate_if_needed(sections, field_off, read_u(info, off, 8));
      }
      return out;
    case DW_FORM_udata:
      out.kind = AttrValue::Kind::Unsigned;
      out.u = read_uleb(info, off);
      return out;
    case DW_FORM_sdata:
      out.kind = AttrValue::Kind::Signed;
      out.s = read_sleb(info, off);
      return out;
    case DW_FORM_flag:
      out.kind = AttrValue::Kind::Flag;
      out.flag = (read_u(info, off, 1) != 0);
      return out;
    case DW_FORM_flag_present:
      out.kind = AttrValue::Kind::Flag;
      out.flag = true;
      return out;
    case DW_FORM_string:
      out.kind = AttrValue::Kind::String;
      out.str = read_cstr(info, off);
      return out;
    case DW_FORM_strp: {
      const std::size_t field_off = off;
      const std::uint64_t offset =
          relocate_if_needed(sections, field_off, read_u(info, off, unit.offset_size));
      out.kind = AttrValue::Kind::String;
      out.str = read_strp(sections.str, offset);
      return out;
    }
    case DW_FORM_strx: {
      out.kind = AttrValue::Kind::Strx;
      out.u = read_uleb(info, off);
      return out;
    }
    case DW_FORM_strx1:
      out.kind = AttrValue::Kind::Strx;
      out.u = read_u(info, off, 1);
      return out;
    case DW_FORM_strx2:
      out.kind = AttrValue::Kind::Strx;
      out.u = read_u(info, off, 2);
      return out;
    case DW_FORM_strx3:
      out.kind = AttrValue::Kind::Strx;
      out.u = read_u(info, off, 3);
      return out;
    case DW_FORM_strx4:
      out.kind = AttrValue::Kind::Strx;
      out.u = read_u(info, off, 4);
      return out;
    case DW_FORM_ref_addr:
      out.kind = AttrValue::Kind::Ref;
      {
        const std::size_t field_off = off;
        out.u = relocate_if_needed(sections, field_off, read_u(info, off, unit.offset_size));
      }
      return out;
    case DW_FORM_ref1:
      out.kind = AttrValue::Kind::Ref;
      out.u = unit.offset + read_u(info, off, 1);
      return out;
    case DW_FORM_ref2:
      out.kind = AttrValue::Kind::Ref;
      out.u = unit.offset + read_u(info, off, 2);
      return out;
    case DW_FORM_ref4:
      out.kind = AttrValue::Kind::Ref;
      out.u = unit.offset + read_u(info, off, 4);
      return out;
    case DW_FORM_ref8:
      out.kind = AttrValue::Kind::Ref;
      out.u = unit.offset + read_u(info, off, 8);
      return out;
    case DW_FORM_ref_udata:
      out.kind = AttrValue::Kind::Ref;
      out.u = unit.offset + read_uleb(info, off);
      return out;
    case DW_FORM_sec_offset:
      out.kind = AttrValue::Kind::Unsigned;
      {
        const std::size_t field_off = off;
        out.u = relocate_if_needed(sections, field_off, read_u(info, off, unit.offset_size));
      }
      return out;
    case DW_FORM_exprloc: {
      std::uint64_t len = read_uleb(info, off);
      read_block(static_cast<std::size_t>(len));
      out.kind = AttrValue::Kind::Expr;
      return out;
    }
    case DW_FORM_block:
      read_block(static_cast<std::size_t>(read_uleb(info, off)));
      return out;
    case DW_FORM_block1:
      read_block(static_cast<std::size_t>(read_u(info, off, 1)));
      return out;
    case DW_FORM_block2:
      read_block(static_cast<std::size_t>(read_u(info, off, 2)));
      return out;
    case DW_FORM_block4:
      read_block(static_cast<std::size_t>(read_u(info, off, 4)));
      return out;
    case DW_FORM_indirect: {
      AbbrevAttr indirect_spec = spec;
      indirect_spec.form = read_uleb(info, off);
      return read_attr_value(info, sections, unit, indirect_spec, off);
    }
    default:
      fatal("DWARF: unsupported form 0x" + format_hex64(spec.form));
  }
}

struct DwarfContext {
  DwarfSections sections;
  std::vector<Unit> units;
  std::vector<Die> dies;
  std::unordered_map<std::uint64_t, std::size_t> die_by_offset;
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> children;
  std::unordered_map<std::uint64_t, AbbrevTable> abbrev_cache;
};

DwarfSections load_sections(const std::vector<std::byte>& buf) {
  if (buf.size() < sizeof(Elf64_Ehdr)) {
    fatal("DWARF: object too small for ELF header");
  }

  const auto buf_span = std::span<const std::byte>(buf.data(), buf.size());
  const auto eh = read_obj<Elf64_Ehdr>(buf_span, 0);
  if (eh.e_ident[EI_MAG0] != ELFMAG0 || eh.e_ident[EI_MAG1] != ELFMAG1 ||
      eh.e_ident[EI_MAG2] != ELFMAG2 || eh.e_ident[EI_MAG3] != ELFMAG3) {
    fatal("DWARF: not an ELF file");
  }
  if (eh.e_ident[EI_CLASS] != ELFCLASS64) {
    fatal("DWARF: only ELF64 is supported");
  }
  if (eh.e_ident[EI_DATA] != ELFDATA2LSB) {
    fatal("DWARF: only little-endian ELF is supported");
  }

  const std::size_t shoff = eh.e_shoff;
  const std::size_t shentsize = eh.e_shentsize;
  const std::size_t shnum = eh.e_shnum;
  const std::size_t shstrndx = eh.e_shstrndx;

  if (shoff == 0 || shentsize < sizeof(Elf64_Shdr)) {
    fatal("DWARF: invalid section header table");
  }
  if (shnum == 0 || shstrndx >= shnum) {
    fatal("DWARF: invalid section header metadata");
  }

  const auto get_sh = [&](std::size_t idx) -> Elf64_Shdr {
    const std::size_t off = checked_off_add(
        shoff, checked_off_mul(idx, shentsize, "DWARF section header offset"),
        "DWARF section header base");
    if (checked_off_add(off, sizeof(Elf64_Shdr), "DWARF section header end") > buf.size()) {
      fatal("DWARF: section header overflow");
    }
    return read_obj<Elf64_Shdr>(buf_span, off);
  };

  const auto shstr = get_sh(shstrndx);
  if (checked_off_add(static_cast<std::size_t>(shstr.sh_offset),
                      static_cast<std::size_t>(shstr.sh_size),
                      "DWARF shstrtab end") > buf.size()) {
    fatal("DWARF: shstrtab overflow");
  }
  const std::span<const char> shstrtab(
      reinterpret_cast<const char*>(buf.data() + shstr.sh_offset), shstr.sh_size);

  auto get_name = [&](std::size_t off) -> std::string_view {
    if (off >= shstrtab.size()) fatal("DWARF: section name offset out of range");
    const char* p = shstrtab.data() + off;
    return std::string_view(p, strnlen(p, shstrtab.size() - off));
  };

  DwarfSections sections{};
  bool have_rela_debug_info = false;
  Elf64_Shdr rela_debug_info{};

  for (std::size_t i = 0; i < shnum; ++i) {
    const auto sh = get_sh(i);
    const auto name = get_name(sh.sh_name);
    if (checked_off_add(static_cast<std::size_t>(sh.sh_offset),
                        static_cast<std::size_t>(sh.sh_size),
                        "DWARF section end") > buf.size()) {
      fatal("DWARF: section overflow");
    }
    const std::span<const std::byte> data(buf.data() + sh.sh_offset, sh.sh_size);

    if (name == ".debug_info") sections.info = data;
    else if (name == ".debug_abbrev") sections.abbrev = data;
    else if (name == ".debug_str") sections.str = data;
    else if (name == ".debug_str_offsets") sections.str_offsets = data;
    else if (name == ".rela.debug_info") {
      have_rela_debug_info = true;
      rela_debug_info = sh;
    }
  }

  if (have_rela_debug_info && !sections.info.empty()) {
    const auto symtab_sh = get_sh(static_cast<std::size_t>(rela_debug_info.sh_link));
    apply_debug_info_relocations(buf_span, sections.info, rela_debug_info, symtab_sh,
                                 sections.info_owned, sections.info_relocations);
    sections.info = std::span<const std::byte>(sections.info_owned.data(), sections.info_owned.size());
  }

  return sections;
}

DwarfContext parse_dwarf(const std::vector<std::byte>& buf) {
  DwarfContext ctx;
  ctx.sections = load_sections(buf);

  if (ctx.sections.info.empty() || ctx.sections.abbrev.empty()) {
    fatal("DWARF: missing required debug sections");
  }

  const auto info = ctx.sections.info;
  std::size_t off = 0;

  while (checked_off_add(off, 4, "DWARF unit header probe") <= info.size()) {
    std::size_t unit_offset = off;
    std::uint64_t unit_length = read_u(info, off, 4);
    bool is_64 = false;
    if (unit_length == 0xffffffffu) {
      is_64 = true;
      unit_length = read_u(info, off, 8);
    }
    if (unit_length == 0) break;

    const std::size_t unit_end = checked_off_add(
        off, static_cast<std::size_t>(unit_length), "DWARF unit end");
    if (unit_end > info.size()) {
      fatal("DWARF: unit extends past end of .debug_info");
    }

    Unit unit;
    unit.offset = unit_offset;
    unit.end = unit_end;
    unit.offset_size = is_64 ? 8 : 4;

    if (checked_off_add(off, 2, "DWARF unit header") > unit_end) fatal("DWARF: unit header overflow");
    unit.version = static_cast<std::uint16_t>(read_u(info, off, 2));

    if (unit.version >= 5) {
      if (checked_off_add(off, 2, "DWARF unit v5 header") > unit_end) fatal("DWARF: unit header overflow");
      unit.unit_type = static_cast<std::uint8_t>(read_u(info, off, 1));
      unit.address_size = static_cast<std::uint8_t>(read_u(info, off, 1));
      {
        const std::size_t abbrev_field_off = off;
        unit.abbrev_offset = read_u(info, off, unit.offset_size);
        unit.abbrev_offset =
            relocate_if_needed(ctx.sections, abbrev_field_off, unit.abbrev_offset);
      }
      if (unit.unit_type == DW_UT_type || unit.unit_type == DW_UT_split_type) {
        (void)read_u(info, off, 8);               // type_signature
        (void)read_u(info, off, unit.offset_size); // type_offset
      }
    } else {
      const std::size_t abbrev_field_off = off;
      unit.abbrev_offset = read_u(info, off, unit.offset_size);
      unit.abbrev_offset =
          relocate_if_needed(ctx.sections, abbrev_field_off, unit.abbrev_offset);
      unit.address_size = static_cast<std::uint8_t>(read_u(info, off, 1));
    }

    const std::size_t unit_index = ctx.units.size();
    ctx.units.push_back(unit);

    AbbrevTable* abbrev_table = nullptr;
    auto it = ctx.abbrev_cache.find(unit.abbrev_offset);
    if (it == ctx.abbrev_cache.end()) {
      auto table = parse_abbrev_table(ctx.sections.abbrev, unit.abbrev_offset);
      auto [ins, _] = ctx.abbrev_cache.emplace(unit.abbrev_offset, std::move(table));
      abbrev_table = &ins->second;
    } else {
      abbrev_table = &it->second;
    }

    std::vector<std::uint64_t> parent_stack;
    while (off < unit_end) {
      const std::size_t die_off = off;
      const std::uint64_t code = read_uleb(info, off);
      if (code == 0) {
        if (!parent_stack.empty()) parent_stack.pop_back();
        continue;
      }
      auto ab_it = abbrev_table->find(code);
      if (ab_it == abbrev_table->end()) {
        fatal("DWARF: unknown abbrev code " + std::to_string(code) +
              " at .debug_info offset 0x" + format_hex64(static_cast<std::uint64_t>(die_off)) +
              " (unit_offset=0x" + format_hex64(unit.offset) +
              ", unit_version=" + std::to_string(unit.version) +
              ", unit_addr_size=" + std::to_string(unit.address_size) +
              ", abbrev_offset=0x" + format_hex64(unit.abbrev_offset) + ")");
      }
      const auto& ab = ab_it->second;

      Die die;
      die.offset = die_off;
      die.tag = ab.tag;
      die.has_children = ab.has_children;
      die.depth = static_cast<std::uint32_t>(parent_stack.size());
      die.parent_offset = parent_stack.empty() ? 0 : parent_stack.back();
      die.unit_index = unit_index;

      for (const auto& spec : ab.attrs) {
        AttrValue val = read_attr_value(info, ctx.sections, unit, spec, off);
        die.attrs.emplace(spec.name, std::move(val));
      }

      const auto die_index = ctx.dies.size();
      ctx.dies.push_back(std::move(die));
      ctx.die_by_offset.emplace(ctx.dies.back().offset, die_index);
      if (ctx.dies.back().parent_offset != 0) {
        ctx.children[ctx.dies.back().parent_offset].push_back(die_index);
      }

      // Record str_offsets_base on the unit from the root DIE.
      if (ctx.dies.back().depth == 0) {
        auto it_attr = ctx.dies.back().attrs.find(DW_AT_str_offsets_base);
        if (it_attr != ctx.dies.back().attrs.end() &&
            it_attr->second.kind == AttrValue::Kind::Unsigned) {
          ctx.units[unit_index].str_offsets_base = it_attr->second.u;
        }
      }

      if (ab.has_children) {
        parent_stack.push_back(ctx.dies.back().offset);
      }
    }

    off = unit_end;
  }

  return ctx;
}

const Die* find_die(const DwarfContext& ctx, std::uint64_t offset) {
  if (offset >= ctx.sections.info.size()) {
    fatal("DWARF: DIE reference out of .debug_info range");
  }
  auto it = ctx.die_by_offset.find(offset);
  if (it == ctx.die_by_offset.end()) return nullptr;
  return &ctx.dies[it->second];
}

std::optional<std::string> get_attr_string(const DwarfContext& ctx, const Die& die, std::uint64_t attr) {
  auto it = die.attrs.find(attr);
  if (it == die.attrs.end()) return std::nullopt;
  const auto& v = it->second;
  if (v.kind == AttrValue::Kind::String) return v.str;
  if (v.kind == AttrValue::Kind::Strx) {
    const auto& unit = ctx.units[die.unit_index];
    if (ctx.sections.str_offsets.empty()) {
      fatal("DWARF: missing .debug_str_offsets for strx forms");
    }
    if (unit.str_offsets_base > std::numeric_limits<std::size_t>::max()) {
      fatal("DWARF: str_offsets_base out of host range");
    }
    if (v.u > std::numeric_limits<std::size_t>::max()) {
      fatal("DWARF: strx index out of host range");
    }
    const std::size_t offset_size = static_cast<std::size_t>(unit.offset_size);
    const std::size_t table_off = checked_off_add(
        static_cast<std::size_t>(unit.str_offsets_base),
        checked_off_mul(static_cast<std::size_t>(v.u), offset_size,
                        "DWARF str_offsets entry index"),
        "DWARF str_offsets table offset");
    if (checked_off_add(table_off, offset_size, "DWARF str_offsets entry end") >
        ctx.sections.str_offsets.size()) {
      fatal("DWARF: str_offsets index out of range");
    }
    std::size_t tmp = table_off;
    const std::uint64_t str_off = read_u(ctx.sections.str_offsets, tmp, offset_size);
    return read_strp(ctx.sections.str, str_off);
  }
  return std::nullopt;
}

std::optional<std::uint64_t> get_attr_u64(const Die& die, std::uint64_t attr) {
  auto it = die.attrs.find(attr);
  if (it == die.attrs.end()) return std::nullopt;
  const auto& v = it->second;
  if (v.kind == AttrValue::Kind::Unsigned) return v.u;
  if (v.kind == AttrValue::Kind::Signed) {
    if (v.s < 0) return std::nullopt;
    return static_cast<std::uint64_t>(v.s);
  }
  return std::nullopt;
}

std::optional<std::int64_t> get_attr_s64(const Die& die, std::uint64_t attr) {
  auto it = die.attrs.find(attr);
  if (it == die.attrs.end()) return std::nullopt;
  const auto& v = it->second;
  if (v.kind == AttrValue::Kind::Signed) return v.s;
  if (v.kind == AttrValue::Kind::Unsigned) {
    return static_cast<std::int64_t>(v.u);
  }
  return std::nullopt;
}

std::optional<std::uint64_t> get_attr_ref(const Die& die, std::uint64_t attr) {
  auto it = die.attrs.find(attr);
  if (it == die.attrs.end()) return std::nullopt;
  const auto& v = it->second;
  if (v.kind == AttrValue::Kind::Ref) return v.u;
  return std::nullopt;
}

std::uint64_t eval_exprloc(const std::vector<std::byte>& bytes) {
  std::vector<std::uint64_t> stack;
  std::size_t off = 0;
  while (off < bytes.size()) {
    std::uint8_t op = std::to_integer<std::uint8_t>(bytes[off++]);
    switch (op) {
      case DW_OP_plus_uconst: {
        std::uint64_t val = read_uleb(std::span<const std::byte>(bytes.data(), bytes.size()), off);
        if (stack.empty()) {
          stack.push_back(val);
        } else {
          stack.back() += val;
        }
        break;
      }
      case DW_OP_constu: {
        std::uint64_t val = read_uleb(std::span<const std::byte>(bytes.data(), bytes.size()), off);
        stack.push_back(val);
        break;
      }
      case DW_OP_consts: {
        std::int64_t val = read_sleb(std::span<const std::byte>(bytes.data(), bytes.size()), off);
        if (val < 0) fatal("DWARF: negative consts in exprloc");
        stack.push_back(static_cast<std::uint64_t>(val));
        break;
      }
      case DW_OP_plus: {
        if (stack.size() < 2) fatal("DWARF: malformed exprloc");
        auto b = stack.back();
        stack.pop_back();
        auto a = stack.back();
        stack.pop_back();
        stack.push_back(a + b);
        break;
      }
      default:
        fatal("DWARF: unsupported exprloc op 0x" + format_hex64(op));
    }
  }
  if (stack.size() != 1) fatal("DWARF: malformed exprloc stack");
  return stack.back();
}

std::uint64_t data_member_location(const Die& die) {
  auto it = die.attrs.find(DW_AT_data_member_location);
  if (it == die.attrs.end()) return 0;
  const auto& v = it->second;
  switch (v.kind) {
    case AttrValue::Kind::Unsigned:
      return v.u;
    case AttrValue::Kind::Signed:
      if (v.s < 0) fatal("DWARF: negative data_member_location");
      return static_cast<std::uint64_t>(v.s);
    case AttrValue::Kind::Expr:
    case AttrValue::Kind::Block:
      return eval_exprloc(v.bytes);
    default:
      fatal("DWARF: unsupported data_member_location form");
  }
}

const Die* resolve_type(const DwarfContext& ctx, const Die& die, int depth = 0) {
  if (depth > 32) fatal("DWARF: type chain too deep");
  if (die.tag == DW_TAG_typedef || die.tag == DW_TAG_const_type ||
      die.tag == DW_TAG_volatile_type || die.tag == DW_TAG_restrict_type ||
      die.tag == DW_TAG_atomic_type) {
    auto ref = get_attr_ref(die, DW_AT_type);
    if (!ref) return nullptr;
    auto* next = find_die(ctx, *ref);
    if (!next) return nullptr;
    return resolve_type(ctx, *next, depth + 1);
  }
  return &die;
}

std::optional<std::uint64_t> type_byte_size(const DwarfContext& ctx, const Die& die) {
  if (auto sz = get_attr_u64(die, DW_AT_byte_size)) return sz;
  if (die.tag == DW_TAG_pointer_type) {
    return ctx.units[die.unit_index].address_size;
  }
  return std::nullopt;
}

std::uint8_t type_is_signed(const DwarfContext& ctx, const Die& die) {
  const Die* base = resolve_type(ctx, die);
  if (!base) fatal("DWARF: missing base type");
  auto enc = get_attr_u64(*base, DW_AT_encoding);
  if (!enc) fatal("DWARF: base type missing encoding");

  switch (*enc) {
    case DW_ATE_signed:
    case DW_ATE_signed_char:
    case DW_ATE_signed_fixed:
      return 1;
    case DW_ATE_unsigned:
    case DW_ATE_unsigned_char:
    case DW_ATE_unsigned_fixed:
    case DW_ATE_boolean:
      return 0;
    default:
      fatal("DWARF: unsupported base type encoding");
  }
}

bool is_declaration(const Die& die) {
  auto it = die.attrs.find(DW_AT_declaration);
  if (it == die.attrs.end()) return false;
  const auto& v = it->second;
  if (v.kind == AttrValue::Kind::Flag) return v.flag;
  if (v.kind == AttrValue::Kind::Unsigned) return v.u != 0;
  return false;
}

struct MemberLookup {
  const Die* member{nullptr};
  const Die* containing_type{nullptr};
  std::uint64_t offset_to_containing_type{0};
  std::uint64_t member_location{0};
};

std::optional<MemberLookup> find_member_recursive(const DwarfContext& ctx, const Die& type_die,
                                                  std::string_view name,
                                                  bool allow_anonymous) {
  auto it_children = ctx.children.find(type_die.offset);
  if (it_children == ctx.children.end()) return std::nullopt;

  std::optional<MemberLookup> found;
  auto consider = [&](std::optional<MemberLookup> candidate) {
    if (!candidate) return;
    if (found) {
      fatal("DWARF: ambiguous member '" + std::string(name) + "'");
    }
    found = std::move(candidate);
  };

  for (auto idx : it_children->second) {
    const auto& child = ctx.dies[idx];
    if (child.tag != DW_TAG_member) continue;

    const auto member_name = get_attr_string(ctx, child, DW_AT_name).value_or("");
    const auto member_loc = data_member_location(child);

    if (!member_name.empty() && member_name == name) {
      consider(MemberLookup{&child, &type_die, 0, member_loc});
    }

    if (allow_anonymous && member_name.empty()) {
      auto type_ref = get_attr_ref(child, DW_AT_type);
      if (!type_ref) continue;
      const auto* anon_type = find_die(ctx, *type_ref);
      if (!anon_type) continue;
      anon_type = resolve_type(ctx, *anon_type);
      if (!anon_type) continue;
      if (anon_type->tag != DW_TAG_structure_type && anon_type->tag != DW_TAG_union_type) continue;
      auto nested = find_member_recursive(ctx, *anon_type, name, allow_anonymous);
      if (nested) {
        if (nested->offset_to_containing_type >
            std::numeric_limits<std::uint64_t>::max() - member_loc) {
          fatal("DWARF: anonymous member offset overflow");
        }
        nested->offset_to_containing_type += member_loc;
        consider(std::move(nested));
      }
    }
  }

  return found;
}

struct ResolvedBitfield {
  const Die* member{nullptr};
  const Die* containing_type{nullptr};
  std::uint64_t prefix_bytes{0};
};

ResolvedBitfield resolve_bitfield(const DwarfContext& ctx, const Die& root_type,
                                  const std::vector<DesignatorComponent>& comps,
                                  bool allow_anonymous) {
  const Die* current = &root_type;
  std::uint64_t prefix_bytes = 0;

  if (comps.empty()) {
    fatal("DWARF: empty member designator");
  }

  for (std::size_t i = 0; i + 1 < comps.size(); ++i) {
    const auto& comp = comps[i];
    auto lookup = find_member_recursive(ctx, *current, comp.name, allow_anonymous);
    if (!lookup) {
      fatal("DWARF: member '" + comp.name + "' not found");
    }

    auto type_ref = get_attr_ref(*lookup->member, DW_AT_type);
    if (!type_ref) fatal("DWARF: member missing type");
    const auto* member_type = find_die(ctx, *type_ref);
    if (!member_type) fatal("DWARF: member type not found");
    member_type = resolve_type(ctx, *member_type);
    if (!member_type) fatal("DWARF: member type resolution failed");

    std::uint64_t offset_to_member = 0;
    if (lookup->offset_to_containing_type >
        std::numeric_limits<std::uint64_t>::max() - lookup->member_location) {
      fatal("DWARF: member offset overflow");
    }
    offset_to_member = lookup->offset_to_containing_type + lookup->member_location;
    if (comp.index) {
      if (member_type->tag != DW_TAG_array_type) {
        fatal("DWARF: array subscript applied to non-array member '" + comp.name + "'");
      }
      auto elem_ref = get_attr_ref(*member_type, DW_AT_type);
      if (!elem_ref) fatal("DWARF: array type missing element type");
      const auto* elem_type = find_die(ctx, *elem_ref);
      if (!elem_type) fatal("DWARF: array element type not found");
      elem_type = resolve_type(ctx, *elem_type);
      if (!elem_type) fatal("DWARF: array element type resolution failed");
      const auto elem_size = type_byte_size(ctx, *elem_type);
      if (!elem_size) fatal("DWARF: array element size unavailable");
      std::uint64_t indexed_offset = 0;
      if (*comp.index != 0 &&
          *elem_size > std::numeric_limits<std::uint64_t>::max() / *comp.index) {
        fatal("DWARF: array index offset overflow");
      }
      indexed_offset = *comp.index * *elem_size;
      if (offset_to_member > std::numeric_limits<std::uint64_t>::max() - indexed_offset) {
        fatal("DWARF: member + index offset overflow");
      }
      const std::uint64_t total_offset = offset_to_member + indexed_offset;
      if (prefix_bytes > std::numeric_limits<std::uint64_t>::max() - total_offset) {
        fatal("DWARF: bitfield prefix offset overflow");
      }
      prefix_bytes += total_offset;
      current = elem_type;
    } else {
      if (member_type->tag == DW_TAG_array_type) {
        fatal("DWARF: array member requires subscript for designator");
      }
      if (prefix_bytes > std::numeric_limits<std::uint64_t>::max() - offset_to_member) {
        fatal("DWARF: bitfield prefix offset overflow");
      }
      prefix_bytes += offset_to_member;
      current = member_type;
    }
  }

  const auto& last = comps.back();
  auto lookup = find_member_recursive(ctx, *current, last.name, allow_anonymous);
  if (!lookup) {
    fatal("DWARF: member '" + last.name + "' not found");
  }
  if (last.index) {
    fatal("DWARF: bitfield designator cannot end with array subscript");
  }

  if (prefix_bytes > std::numeric_limits<std::uint64_t>::max() - lookup->offset_to_containing_type) {
    fatal("DWARF: bitfield prefix offset overflow");
  }
  prefix_bytes += lookup->offset_to_containing_type;
  return ResolvedBitfield{lookup->member, lookup->containing_type, prefix_bytes};
}

struct TypeName {
  enum class Kind { Struct, Union, Enum, Other } kind{Kind::Other};
  std::string name;
};

TypeName parse_type_name(std::string_view type) {
  auto trim = [](std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
  };

  type = trim(type);
  constexpr std::string_view kStruct = "struct ";
  constexpr std::string_view kUnion = "union ";
  constexpr std::string_view kEnum = "enum ";

  if (type.rfind(kStruct, 0) == 0) {
    return {TypeName::Kind::Struct, std::string(trim(type.substr(kStruct.size())))};
  }
  if (type.rfind(kUnion, 0) == 0) {
    return {TypeName::Kind::Union, std::string(trim(type.substr(kUnion.size())))};
  }
  if (type.rfind(kEnum, 0) == 0) {
    return {TypeName::Kind::Enum, std::string(trim(type.substr(kEnum.size())))};
  }
  return {TypeName::Kind::Other, std::string(type)};
}

const Die* find_named_die(const DwarfContext& ctx, std::uint64_t tag, std::string_view name) {
  for (const auto& die : ctx.dies) {
    if (die.tag != tag) continue;
    const auto die_name = get_attr_string(ctx, die, DW_AT_name);
    if (!die_name || *die_name != name) continue;
    if (is_declaration(die)) continue;
    return &die;
  }
  return nullptr;
}

const Die* resolve_c_type(const DwarfContext& ctx, std::string_view c_type,
                          std::uint64_t expect_tag = 0) {
  const auto parsed = parse_type_name(c_type);
  if (!parsed.name.empty()) {
    if (parsed.kind == TypeName::Kind::Struct) {
      return find_named_die(ctx, DW_TAG_structure_type, parsed.name);
    }
    if (parsed.kind == TypeName::Kind::Union) {
      return find_named_die(ctx, DW_TAG_union_type, parsed.name);
    }
    if (parsed.kind == TypeName::Kind::Enum) {
      return find_named_die(ctx, DW_TAG_enumeration_type, parsed.name);
    }
  }

  // Try typedef first.
  const Die* typedef_die = find_named_die(ctx, DW_TAG_typedef, parsed.name);
  if (typedef_die) {
    auto ref = get_attr_ref(*typedef_die, DW_AT_type);
    if (!ref) fatal("DWARF: typedef missing DW_AT_type");
    auto* base = find_die(ctx, *ref);
    if (!base) fatal("DWARF: typedef target not found");
    base = resolve_type(ctx, *base);
    if (!base) fatal("DWARF: typedef chain resolution failed");
    if (expect_tag != 0 && base->tag != expect_tag) {
      fatal("DWARF: typedef resolved to unexpected tag");
    }
    return base;
  }

  if (expect_tag != 0) {
    return find_named_die(ctx, expect_tag, parsed.name);
  }

  if (const auto* any = find_named_die(ctx, DW_TAG_structure_type, parsed.name)) return any;
  if (const auto* any = find_named_die(ctx, DW_TAG_union_type, parsed.name)) return any;
  if (const auto* any = find_named_die(ctx, DW_TAG_enumeration_type, parsed.name)) return any;
  if (const auto* any = find_named_die(ctx, DW_TAG_base_type, parsed.name)) return any;
  return nullptr;
}

BitfieldGeometry extract_bitfield_geometry(const DwarfContext& ctx, const ManifestOptions& options,
                                           const ManifestType& type, std::string_view designator) {
  const auto* root = resolve_c_type(ctx, type.c_type, 0);
  if (!root) {
    fatal("DWARF: failed to locate type '" + type.c_type + "'");
  }
  if (root->tag != DW_TAG_structure_type && root->tag != DW_TAG_union_type) {
    fatal("DWARF: type '" + type.c_type + "' is not a struct/union");
  }

  const auto comps = parse_member_designator(designator);
  const auto resolved = resolve_bitfield(ctx, *root, comps, options.allow_anonymous_members);

  const auto* member = resolved.member;
  if (!member) {
    fatal("DWARF: missing bitfield member for '" + std::string(designator) + "'");
  }

  auto bit_size_opt = get_attr_u64(*member, DW_AT_bit_size);
  if (!bit_size_opt) {
    fatal("DWARF: member '" + std::string(designator) + "' is not a bitfield");
  }
  const auto bit_size = *bit_size_opt;
  if (bit_size == 0 || bit_size > 64) {
    fatal("DWARF: bitfield width unsupported for '" + std::string(designator) + "'");
  }

  std::uint64_t abs_bit_offset = 0;
  if (auto dbo = get_attr_u64(*member, DW_AT_data_bit_offset)) {
    if (resolved.prefix_bytes > std::numeric_limits<std::uint64_t>::max() / 8) {
      fatal("DWARF: bitfield prefix bit offset overflow");
    }
    const auto prefix_bits = resolved.prefix_bytes * 8;
    if (prefix_bits > std::numeric_limits<std::uint64_t>::max() - *dbo) {
      fatal("DWARF: bitfield absolute bit offset overflow");
    }
    abs_bit_offset = prefix_bits + *dbo;
  } else if (auto bo = get_attr_u64(*member, DW_AT_bit_offset)) {
    auto type_ref = get_attr_ref(*member, DW_AT_type);
    if (!type_ref) fatal("DWARF: bitfield missing base type");
    const auto* base_type = find_die(ctx, *type_ref);
    if (!base_type) fatal("DWARF: bitfield base type not found");
    base_type = resolve_type(ctx, *base_type);
    if (!base_type) fatal("DWARF: bitfield base type resolution failed");
    const auto base_size = type_byte_size(ctx, *base_type);
    if (!base_size) fatal("DWARF: bitfield base type size not found");
    const auto tbits = (*base_size) * 8;
    if (*bo + bit_size > tbits) {
      fatal("DWARF: invalid bit_offset for '" + std::string(designator) + "'");
    }
    const auto lsb_offset = tbits - *bo - bit_size;
    const auto member_loc = data_member_location(*member);
    if (resolved.prefix_bytes > std::numeric_limits<std::uint64_t>::max() / 8 ||
        member_loc > std::numeric_limits<std::uint64_t>::max() / 8) {
      fatal("DWARF: bitfield absolute offset overflow");
    }
    const auto prefix_bits = resolved.prefix_bytes * 8;
    const auto member_bits = member_loc * 8;
    if (prefix_bits > std::numeric_limits<std::uint64_t>::max() - member_bits) {
      fatal("DWARF: bitfield absolute offset overflow");
    }
    const auto base = prefix_bits + member_bits;
    if (base > std::numeric_limits<std::uint64_t>::max() - lsb_offset) {
      fatal("DWARF: bitfield absolute offset overflow");
    }
    abs_bit_offset = base + lsb_offset;
  } else {
    fatal("DWARF: bitfield '" + std::string(designator) + "' missing offset attributes");
  }

  const std::uint64_t byte_offset = abs_bit_offset / 8;
  const std::uint8_t bit_in_byte = static_cast<std::uint8_t>(abs_bit_offset % 8);
  const std::uint8_t span_bytes =
      static_cast<std::uint8_t>(((bit_in_byte + bit_size) + 7) / 8);
  if (span_bytes == 0 || span_bytes > 16) {
    fatal("DWARF: bitfield span_bytes unsupported for '" + std::string(designator) + "'");
  }

  auto type_ref = get_attr_ref(*member, DW_AT_type);
  if (!type_ref) fatal("DWARF: bitfield missing base type");
  const auto* base_type = find_die(ctx, *type_ref);
  if (!base_type) fatal("DWARF: bitfield base type not found");

  BitfieldGeometry geom;
  geom.abs_bit_offset = abs_bit_offset;
  geom.bit_size = static_cast<std::uint16_t>(bit_size);
  geom.is_signed = type_is_signed(ctx, *base_type);
  geom.byte_offset = byte_offset;
  geom.bit_in_byte = bit_in_byte;
  geom.span_bytes = span_bytes;
  return geom;
}

std::vector<EnumValue> extract_enum_values(const DwarfContext& ctx, const ManifestEnum& en) {
  const auto* enum_die = resolve_c_type(ctx, en.c_type, DW_TAG_enumeration_type);
  if (!enum_die) {
    fatal("DWARF: failed to locate enum type '" + en.c_type + "'");
  }
  if (is_declaration(*enum_die)) {
    fatal("DWARF: enum type '" + en.c_type + "' is declaration-only");
  }

  std::vector<EnumValue> values;
  auto it_children = ctx.children.find(enum_die->offset);
  if (it_children != ctx.children.end()) {
    for (auto idx : it_children->second) {
      const auto& child = ctx.dies[idx];
      if (child.tag != DW_TAG_enumerator) continue;
      const auto name = get_attr_string(ctx, child, DW_AT_name);
      if (!name || name->empty()) continue;
      if (auto u = get_attr_u64(child, DW_AT_const_value)) {
        values.push_back(EnumValue{*name, *u, false});
      } else if (auto s = get_attr_s64(child, DW_AT_const_value)) {
        values.push_back(EnumValue{*name, static_cast<std::uint64_t>(*s), true});
      } else {
        fatal("DWARF: enumerator missing const_value");
      }
    }
  }

  std::sort(values.begin(), values.end(), [](const EnumValue& a, const EnumValue& b) {
    return a.name < b.name;
  });

  return values;
}

}  // namespace

ExtractResult extract_dwarf(const std::vector<std::byte>& elf, const Manifest& manifest) {
  DwarfContext ctx = parse_dwarf(elf);
  ExtractResult result;

  for (const auto& type : manifest.types) {
    const auto policy = effective_bitfield_policy(manifest, type);
    if (policy != "geometry") continue;
    if (type.bitfields.empty()) continue;

    for (const auto& bf : type.bitfields) {
      auto geom = extract_bitfield_geometry(ctx, manifest.options, type, bf);
      result.bitfields[type.c_type].emplace(bf, std::move(geom));
    }
  }

  for (const auto& en : manifest.enums) {
    if (!en.extract_all) continue;
    result.enums_all.emplace(en.c_type, extract_enum_values(ctx, en));
  }

  return result;
}

}  // namespace iro::dwarf
