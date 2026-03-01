// SPDX-License-Identifier: GPL-2.0-only
// IRO layout parser (layout_parse):
// - Parses .note.iro.layout ELF note from probe object
// - Validates descriptor + manifest requests
// - Emits deterministic headers (.h/.hpp), .meta, and .stamp
//
// Conforms to IRO-TOOL-SPEC-4.2

#include "../iro_common.hpp"
#include "../iro_manifest.hpp"
#include "dwarf_reader.hpp"

#include <elf.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>

namespace iro::tool {

using namespace iro;

// -----------------------------------------------------------------------------
// CLI Arguments
// -----------------------------------------------------------------------------

struct Args {
  fs::path manifest;
  fs::path probe_obj;
  fs::path probe_cmd;
  fs::path outdir;
  bool emit{false};
  bool verify{false};
  bool dump{false};
  bool hash64_only{false};
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string_view a(argv[i]);
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) fatal(std::string("missing value for ") + name);
      return argv[++i];
    };

    if (a == "--manifest") {
      args.manifest = next("--manifest");
    } else if (a == "--probe") {
      args.probe_obj = next("--probe");
    } else if (a == "--probe-cmd") {
      args.probe_cmd = next("--probe-cmd");
    } else if (a == "--outdir") {
      args.outdir = next("--outdir");
    } else if (a == "--emit") {
      args.emit = true;
    } else if (a == "--verify") {
      args.verify = true;
    } else if (a == "--dump") {
      args.dump = true;
    } else if (a == "--hash64") {
      args.hash64_only = true;
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage: layout_parse --manifest M --probe O --probe-cmd C --outdir D "
                   "[--emit|--verify] [--dump]\n";
      std::cout << "       layout_parse --manifest M --probe-cmd C --hash64\n";
      std::cout << "\nVersion: " << kLayoutParseVersion << " (schema " << kLayoutSchemaMajor << "."
                << kLayoutSchemaMinor << ")\n";
      std::exit(0);
    } else {
      fatal("unknown argument: " + std::string(a));
    }
  }

  if (!args.emit && !args.verify && !args.dump && !args.hash64_only) {
    args.emit = true;
  }
  if (args.manifest.empty() || args.probe_cmd.empty()) {
    fatal("manifest and probe-cmd are required");
  }
  if (!args.hash64_only && args.probe_obj.empty()) {
    fatal("probe is required for emit/verify/dump");
  }
  if ((args.emit || args.verify) && args.outdir.empty()) {
    fatal("outdir is required for emit/verify");
  }
  return args;
}

// -----------------------------------------------------------------------------
// ELF Parsing Helpers
// -----------------------------------------------------------------------------

template <class T>
T read_obj(std::span<const std::byte> buf, std::size_t offset) {
  if (offset + sizeof(T) > buf.size()) {
    fatal("ELF parse overflow");
  }
  T obj{};
  std::memcpy(&obj, buf.data() + offset, sizeof(T));
  return obj;
}

std::uint32_t read_le_u32(std::span<const std::byte> buf, std::size_t offset) {
  if (offset + 4 > buf.size()) fatal("parse overflow");
  return std::uint32_t(std::to_integer<std::uint32_t>(buf[offset])) |
         (std::uint32_t(std::to_integer<std::uint32_t>(buf[offset + 1])) << 8) |
         (std::uint32_t(std::to_integer<std::uint32_t>(buf[offset + 2])) << 16) |
         (std::uint32_t(std::to_integer<std::uint32_t>(buf[offset + 3])) << 24);
}

// -----------------------------------------------------------------------------
// Note Record Structures
// -----------------------------------------------------------------------------

struct NoteRecord {
  enum class Kind : std::uint16_t {
    Type = 1,
    Field = 2,
    BitfieldAccessor = 3,
    Const = 4,
    Enum = 5,
  };

  Kind kind;
  std::uint16_t flags;
  std::string type_name;
  std::string field_name;
  std::uint64_t sizeof_type;
  std::uint32_t alignof_type;
  std::uint64_t offset_or_id;
};

struct Descriptor {
  std::string set_name;
  std::uint64_t input_hash64;
  std::vector<NoteRecord> records;
};

inline constexpr std::uint16_t kFlagFieldOptional = 0x0001u;
inline constexpr std::uint16_t kFlagEnumSignedValue = 0x0002u;
inline constexpr std::uint16_t kAllowedRecordFlags = kFlagFieldOptional | kFlagEnumSignedValue;

// -----------------------------------------------------------------------------
// Descriptor Parsing (§8.3)
// -----------------------------------------------------------------------------

Descriptor parse_descriptor(std::span<const std::byte> desc) {
  if (desc.size() > kMaxDescSize) fatal("descriptor too large");
  if (desc.size() < 32) fatal("descriptor too small for header");

  std::size_t off = 0;

  auto read_u16 = [&](std::size_t o) -> std::uint16_t {
    if (o + 2 > desc.size()) fatal("descriptor read overflow (u16)");
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned>(desc[o]) |
        (std::to_integer<unsigned>(desc[o + 1]) << 8));
  };
  auto read_u32 = [&](std::size_t o) -> std::uint32_t {
    if (o + 4 > desc.size()) fatal("descriptor read overflow (u32)");
    return std::uint32_t(std::to_integer<std::uint32_t>(desc[o])) |
           (std::uint32_t(std::to_integer<std::uint32_t>(desc[o + 1])) << 8) |
           (std::uint32_t(std::to_integer<std::uint32_t>(desc[o + 2])) << 16) |
           (std::uint32_t(std::to_integer<std::uint32_t>(desc[o + 3])) << 24);
  };
  auto read_u64 = [&](std::size_t o) -> std::uint64_t {
    if (o + 8 > desc.size()) fatal("descriptor read overflow (u64)");
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
      v |= (std::uint64_t(std::to_integer<std::uint64_t>(desc[o + i])) << (i * 8));
    }
    return v;
  };

  // Parse header (§8.3.1)
  const auto magic = read_u32(off);
  off += 4;
  const auto version_major = read_u16(off);
  off += 2;
  const auto version_minor = read_u16(off);
  off += 2;
  const auto header_size = read_u32(off);
  off += 4;
  const auto record_count = read_u32(off);
  off += 4;
  const auto input_hash64 = read_u64(off);
  off += 8;
  const auto set_name_len = read_u32(off);
  off += 4;
  const auto reserved = read_u32(off);
  off += 4;

  if (magic != kDescMagic) {
    fatal("descriptor magic mismatch (expected 0x" + format_hex64(kDescMagic) + ", got 0x" +
          format_hex64(magic) + ")");
  }
  if (version_major != kLayoutSchemaMajor || version_minor != kLayoutSchemaMinor) {
    fatal("descriptor schema mismatch (expected " + std::to_string(kLayoutSchemaMajor) + "." +
              std::to_string(kLayoutSchemaMinor) + ", got " + std::to_string(version_major) + "." +
              std::to_string(version_minor) + ")",
          "regenerate probe with matching gen_probe version");
  }
  if (header_size != 32) {
    fatal("descriptor header_size mismatch (expected 32, got " + std::to_string(header_size) + ")");
  }
  if (record_count > kMaxRecordCount) {
    fatal("too many records (" + std::to_string(record_count) + " > " +
          std::to_string(kMaxRecordCount) + ")");
  }
  if (set_name_len == 0 || set_name_len > kMaxSetNameLen) {
    fatal("invalid set name length: " + std::to_string(set_name_len));
  }
  if (reserved != 0) {
    fatal("descriptor reserved field non-zero");
  }
  if (checked_add(off, set_name_len, "descriptor set name end") > desc.size()) {
    fatal("descriptor set name overflow");
  }

  Descriptor out;
  out.input_hash64 = input_hash64;
  out.set_name = std::string(reinterpret_cast<const char*>(desc.data() + off), set_name_len);
  require_utf8("descriptor.set_name", out.set_name);
  require_safe_set_name(out.set_name);
  off += set_name_len;
  const std::size_t set_pad = (4 - (set_name_len % 4)) % 4;
  if (checked_add(off, set_pad, "descriptor set_name padding") > desc.size()) {
    fatal("descriptor set name padding overflow");
  }
  off += set_pad;

  if (record_count == 0) {
    fatal("descriptor contains no records");
  }

  // Parse records (§8.3.2)
  for (std::uint32_t idx = 0; idx < record_count; ++idx) {
    constexpr std::size_t kRecordHeaderSize = 40;
    if (off + kRecordHeaderSize > desc.size()) {
      fatal("record " + std::to_string(idx) + " header overflow");
    }

    const auto record_size = read_u32(off);
    off += 4;
    const auto kind = read_u16(off);
    off += 2;
    const auto flags = read_u16(off);
    off += 2;
    const auto type_len = read_u32(off);
    off += 4;
    const auto field_len = read_u32(off);
    off += 4;
    const auto sizeof_type = read_u64(off);
    off += 8;
    const auto alignof_type = read_u32(off);
    off += 4;
    const auto reserved0 = read_u32(off);
    off += 4;
    const auto offset_or_id = read_u64(off);
    off += 8;

    if (record_size < kRecordHeaderSize) {
      fatal("record " + std::to_string(idx) + ": record_size too small (" +
            std::to_string(record_size) + " < " + std::to_string(kRecordHeaderSize) + ")");
    }
    if (record_size % 4 != 0) {
      fatal("record " + std::to_string(idx) + ": record_size must be multiple of 4");
    }
    if (reserved0 != 0) {
      fatal("record " + std::to_string(idx) + ": reserved0 non-zero");
    }
    if (flags & ~kAllowedRecordFlags) {
      fatal("record " + std::to_string(idx) + ": flags contain reserved bits");
    }
    if (kind < 1 || kind > 5) {
      fatal("record " + std::to_string(idx) + ": unsupported kind value " + std::to_string(kind));
    }
    if (type_len > kMaxStringLen || field_len > kMaxStringLen) {
      fatal("record " + std::to_string(idx) + ": string too long");
    }

    const std::size_t payload_bytes = record_size - kRecordHeaderSize;
    if (checked_add(off, payload_bytes, "record end") > desc.size()) {
      fatal("record " + std::to_string(idx) + ": overflow");
    }
    if (payload_bytes < type_len + field_len) {
      fatal("record " + std::to_string(idx) + ": payload underflow");
    }

    const auto type_name =
        std::string(reinterpret_cast<const char*>(desc.data() + off), type_len);
    off += type_len;
    const auto field_name =
        std::string(reinterpret_cast<const char*>(desc.data() + off), field_len);
    off += field_len;

    require_utf8("record.type_name", type_name);
    require_utf8("record.field_name", field_name);

    const auto kind_enum = static_cast<NoteRecord::Kind>(kind);

    if ((kind_enum == NoteRecord::Kind::Const || kind_enum == NoteRecord::Kind::Enum) &&
        (sizeof_type != 0 || alignof_type != 0)) {
      fatal("record " + std::to_string(idx) + ": CONST/ENUM must have sizeof/align = 0");
    }
    if ((kind_enum == NoteRecord::Kind::Type || kind_enum == NoteRecord::Kind::Field ||
         kind_enum == NoteRecord::Kind::BitfieldAccessor) &&
        alignof_type == 0) {
      fatal("record " + std::to_string(idx) + ": alignof_type is zero");
    }

    if (kind_enum == NoteRecord::Kind::Type && field_len != 0) {
      fatal("record " + std::to_string(idx) + ": TYPE record must have empty field_name");
    }
    if (kind_enum == NoteRecord::Kind::Const && field_len != 0) {
      fatal("record " + std::to_string(idx) + ": CONST record must have empty field_name");
    }
    if (kind_enum == NoteRecord::Kind::Field && field_len == 0) {
      fatal("record " + std::to_string(idx) + ": FIELD record must have non-empty field_name");
    }
    if (kind_enum == NoteRecord::Kind::BitfieldAccessor && field_len == 0) {
      fatal("record " + std::to_string(idx) + ": BITFIELD record must have non-empty field_name");
    }
    if (kind_enum == NoteRecord::Kind::Enum && field_len == 0) {
      fatal("record " + std::to_string(idx) + ": ENUM record must have non-empty field_name");
    }
    if (kind_enum == NoteRecord::Kind::Field && (flags & ~kFlagFieldOptional) != 0) {
      fatal("record " + std::to_string(idx) + ": FIELD record uses unsupported flags");
    }
    if (kind_enum == NoteRecord::Kind::Enum && (flags & ~kFlagEnumSignedValue) != 0) {
      fatal("record " + std::to_string(idx) + ": ENUM record uses unsupported flags");
    }
    if (kind_enum != NoteRecord::Kind::Field && kind_enum != NoteRecord::Kind::Enum && flags != 0) {
      fatal("record " + std::to_string(idx) + ": only FIELD/ENUM records may carry flags");
    }
    if (kind_enum == NoteRecord::Kind::Type && offset_or_id != 0) {
      fatal("record " + std::to_string(idx) + ": TYPE record must have offset_or_id = 0");
    }

    // Skip padding
    const std::size_t padded = kRecordHeaderSize + type_len + field_len;
    const std::size_t pad = (record_size > padded) ? (record_size - padded) : 0;
    off += pad;

    NoteRecord rec{
        .kind = static_cast<NoteRecord::Kind>(kind),
        .flags = flags,
        .type_name = type_name,
        .field_name = field_name,
        .sizeof_type = sizeof_type,
        .alignof_type = alignof_type,
        .offset_or_id = offset_or_id,
    };
    out.records.push_back(std::move(rec));
  }

  // Validate trailing bytes
  for (std::size_t i = off; i < desc.size(); ++i) {
    if (std::to_integer<std::uint8_t>(desc[i]) != 0) {
      fatal("descriptor has non-zero trailing bytes at offset " + std::to_string(i));
    }
  }

  return out;
}

// -----------------------------------------------------------------------------
// ELF Note Section Location
// -----------------------------------------------------------------------------

std::span<const std::byte> find_note_section(const std::vector<std::byte>& buf,
                                             std::string_view expected_arch) {
  if (buf.size() < sizeof(Elf64_Ehdr)) {
    fatal("object too small for ELF header");
  }

  const auto eh = read_obj<Elf64_Ehdr>(buf, 0);

  if (eh.e_ident[EI_MAG0] != ELFMAG0 || eh.e_ident[EI_MAG1] != ELFMAG1 ||
      eh.e_ident[EI_MAG2] != ELFMAG2 || eh.e_ident[EI_MAG3] != ELFMAG3) {
    fatal("not an ELF file");
  }
  if (eh.e_ident[EI_CLASS] != ELFCLASS64) {
    fatal("only ELF64 is supported",
          "32-bit architectures (including aarch64 ILP32) are not supported");
  }
  if (eh.e_ident[EI_DATA] != ELFDATA2LSB) {
    fatal("only little-endian probe objects are supported",
          "big-endian architectures are not supported by this tool");
  }

  // Cross-compilation safety: validate e_machine matches expected arch
  validate_elf_machine(eh.e_machine, expected_arch);

  const std::size_t shoff = eh.e_shoff;
  const std::size_t shentsize = eh.e_shentsize;
  const std::size_t shnum = eh.e_shnum;
  const std::size_t shstrndx = eh.e_shstrndx;

  if (shoff == 0 || shentsize < sizeof(Elf64_Shdr)) {
    fatal("invalid section header table");
  }
  if (shnum == 0) {
    fatal("no section headers present");
  }
  if (shoff > buf.size()) {
    fatal("section header offset out of range");
  }

  const auto sht_bytes = checked_mul(shentsize, shnum, "section header table size");
  if (checked_add(shoff, sht_bytes, "section header table end") > buf.size()) {
    fatal("section header table extends past end of file");
  }

  auto get_sh = [&](std::size_t idx) -> Elf64_Shdr {
    const std::size_t off =
        checked_add(shoff, checked_mul(idx, shentsize, "section header offset"),
                    "section header offset");
    return read_obj<Elf64_Shdr>(buf, off);
  };

  if (shstrndx >= shnum) {
    fatal("invalid shstrndx");
  }

  const auto shstr = get_sh(shstrndx);
  if (checked_add(shstr.sh_offset, shstr.sh_size, "shstrtab end") > buf.size()) {
    fatal("shstrtab overflow");
  }

  const std::span<const char> shstrtab(
      reinterpret_cast<const char*>(buf.data() + shstr.sh_offset), shstr.sh_size);

  auto get_name = [&](std::size_t off) -> std::string_view {
    if (off >= shstrtab.size()) fatal("section name offset out of range");
    const char* p = shstrtab.data() + off;
    return std::string_view(p, strnlen(p, shstrtab.size() - off));
  };

  for (std::size_t i = 0; i < shnum; ++i) {
    const auto sh = get_sh(i);
    const auto name = get_name(sh.sh_name);
    if ((sh.sh_type == SHT_NOTE || sh.sh_type == SHT_PROGBITS) &&
        name == ".note.iro.layout") {
      if (checked_add(sh.sh_offset, sh.sh_size, "note section end") > buf.size()) {
        fatal("note section overflow");
      }
      if (sh.sh_size > kMaxDescSize + 4096) {
        fatal("note section too large");
      }
      return std::span<const std::byte>(buf.data() + sh.sh_offset, sh.sh_size);
    }
  }

  fatal("failed to locate .note.iro.layout section",
        "ensure probe was compiled correctly and contains the IRO note");
}

Descriptor parse_note(const std::vector<std::byte>& buf, std::string_view expected_arch) {
  const auto note_sec = find_note_section(buf, expected_arch);
  std::size_t off = 0;
  auto align4 = [](std::size_t x) { return (x + 3u) & ~std::size_t(3); };

  while (off + 12 <= note_sec.size()) {
    const auto namesz = static_cast<std::size_t>(read_le_u32(note_sec, off));
    const auto descsz = static_cast<std::size_t>(read_le_u32(note_sec, off + 4));
    const auto ntype = read_le_u32(note_sec, off + 8);
    off += 12;

    if (namesz == 0 || descsz == 0) {
      fatal("invalid note header sizes");
    }
    if (descsz > kMaxDescSize) {
      fatal("note descriptor too large");
    }

    const auto need = checked_add(align4(namesz), align4(descsz), "note payload");
    if (checked_add(off, need, "note end") > note_sec.size()) {
      fatal("note overflow");
    }

    const char* name = reinterpret_cast<const char*>(note_sec.data() + off);
    std::string_view name_view(name, namesz ? strnlen(name, namesz) : 0);
    off += align4(namesz);

    const std::span<const std::byte> desc(note_sec.data() + off, descsz);
    off += align4(descsz);

    if (name_view == "IRO" && ntype == kNoteType) {
      return parse_descriptor(desc);
    }
  }

  fatal("IRO note not found in section");
}

// -----------------------------------------------------------------------------
// probe.cmd Parsing (§6.2)
// -----------------------------------------------------------------------------

struct ProbeCmdInfo {
  std::string cc;
  std::string target;
  std::string arch;
  std::optional<std::string> cwd;
  std::string argv_hex;
  std::vector<std::string> env;
  std::vector<std::string> unknown_lines;
};

ProbeCmdInfo parse_probe_cmd(std::string_view txt) {
  ProbeCmdInfo info;
  std::istringstream is{std::string(txt)};
  std::string line;

  while (std::getline(is, line)) {
    if (line.empty()) continue;
    const auto pos = line.find('=');
    if (pos == std::string::npos) {
      info.unknown_lines.push_back(line);
      continue;
    }
    const auto key = line.substr(0, pos);
    const auto val = line.substr(pos + 1);
    if (key == "cc")
      info.cc = val;
    else if (key == "target")
      info.target = val;
    else if (key == "arch")
      info.arch = val;
    else if (key == "cwd")
      info.cwd = val;
    else if (key == "argv")
      info.argv_hex = val;
    else if (key == "env")
      info.env.push_back(val);
    else
      info.unknown_lines.push_back(line);
  }

  if (info.cc.empty() || info.target.empty() || info.arch.empty() || info.argv_hex.empty()) {
    fatal("probe.cmd: missing required keys (cc/target/arch/argv)");
  }
  if (info.argv_hex.size() % 2 != 0) {
    fatal("probe.cmd: argv hex length must be even");
  }
  if (!is_lower_hex(info.argv_hex)) {
    fatal("probe.cmd: argv must be lowercase hex");
  }

  const auto argv_bytes = decode_hex(info.argv_hex);
  if (argv_bytes.empty() || argv_bytes.back() != 0) {
    fatal("probe.cmd: argv must be NUL-terminated");
  }

  return info;
}

// -----------------------------------------------------------------------------
// Input Hash Computation (§6.5)
// -----------------------------------------------------------------------------

struct InputHash {
  std::array<std::uint8_t, 32> h256;
  std::uint64_t h64;
};

InputHash compute_input_hash(const std::vector<std::byte>& manifest_bytes,
                             const std::vector<std::byte>& probe_cmd_bytes) {
  std::ostringstream tool_identity;
  tool_identity << "schema_major=" << kLayoutSchemaMajor << "\n";
  tool_identity << "schema_minor=" << kLayoutSchemaMinor << "\n";
  tool_identity << "layout_parse_version=" << kLayoutParseVersion << "\n";
  tool_identity << "gen_probe_version=" << kGenProbeVersion << "\n";

  const auto tool_bytes_str = tool_identity.str();
  std::span<const std::byte> tool_span(
      reinterpret_cast<const std::byte*>(tool_bytes_str.data()), tool_bytes_str.size());

  const auto h256 = sha256_concat({manifest_bytes, probe_cmd_bytes, tool_span});
  return {h256, hash256_to_hash64(h256)};
}

// -----------------------------------------------------------------------------
// Semantic Model
// -----------------------------------------------------------------------------

struct TypeModel {
  std::string c_type;
  std::uint64_t sizeof_type{0};
  std::uint32_t alignof_type{0};
  bool has_type_record{false};
  std::map<std::string, std::uint64_t, std::less<>> fields;
  std::map<std::string, std::uint64_t, std::less<>> bitfield_accessors;
  std::map<std::string, dwarf::BitfieldGeometry, std::less<>> bitfield_geometry;
};

struct Model {
  Descriptor desc;
  std::map<std::string, TypeModel, std::less<>> types;
  std::map<std::string, std::uint64_t, std::less<>> constants;
  std::map<std::string, std::map<std::string, dwarf::EnumValue, std::less<>>, std::less<>> enums;
};

Model build_model(const Descriptor& desc) {
  Model model{desc, {}, {}, {}};

  for (const auto& rec : desc.records) {
    switch (rec.kind) {
      case NoteRecord::Kind::Type: {
        auto& type = model.types[rec.type_name];
        type.c_type = rec.type_name;
        if (type.has_type_record) {
          if (type.sizeof_type != rec.sizeof_type || type.alignof_type != rec.alignof_type) {
            fatal("conflicting TYPE record for '" + rec.type_name + "'");
          }
        } else {
          type.has_type_record = true;
          type.sizeof_type = rec.sizeof_type;
          type.alignof_type = rec.alignof_type;
        }
        break;
      }
      case NoteRecord::Kind::Field: {
        auto& type = model.types[rec.type_name];
        type.c_type = rec.type_name;
        auto it = type.fields.find(rec.field_name);
        if (it != type.fields.end() && it->second != rec.offset_or_id) {
          fatal("conflicting FIELD record for '" + rec.type_name + "::" + rec.field_name + "'");
        }
        type.fields.emplace(rec.field_name, rec.offset_or_id);
        break;
      }
      case NoteRecord::Kind::BitfieldAccessor: {
        auto& type = model.types[rec.type_name];
        type.c_type = rec.type_name;
        auto it = type.bitfield_accessors.find(rec.field_name);
        if (it != type.bitfield_accessors.end() && it->second != rec.offset_or_id) {
          fatal("conflicting BITFIELD_ACCESSOR record for '" + rec.type_name + "::" +
                rec.field_name + "'");
        }
        type.bitfield_accessors.emplace(rec.field_name, rec.offset_or_id);
        break;
      }
      case NoteRecord::Kind::Const: {
        auto it = model.constants.find(rec.type_name);
        if (it != model.constants.end() && it->second != rec.offset_or_id) {
          fatal("conflicting CONST record for '" + rec.type_name + "'");
        }
        model.constants.emplace(rec.type_name, rec.offset_or_id);
        break;
      }
      case NoteRecord::Kind::Enum: {
        auto& values = model.enums[rec.type_name];
        auto it = values.find(rec.field_name);
        const bool is_signed = (rec.flags & kFlagEnumSignedValue) != 0;
        if (it != values.end() &&
            (it->second.raw != rec.offset_or_id || it->second.is_signed != is_signed)) {
          fatal("conflicting ENUM record for '" + rec.type_name + "::" + rec.field_name + "'");
        }
        values.emplace(rec.field_name, dwarf::EnumValue{rec.field_name, rec.offset_or_id, is_signed});
        break;
      }
    }
  }

  return model;
}

void validate_against_manifest(const Manifest& manifest, const Model& model) {
  if (manifest.set != model.desc.set_name) {
    fatal("set name mismatch: manifest '" + manifest.set + "', desc '" + model.desc.set_name + "'");
  }

  for (const auto& mtype : manifest.types) {
    auto it = model.types.find(mtype.c_type);
    if (it == model.types.end() || !it->second.has_type_record) {
      fatal("missing TYPE record for '" + mtype.c_type + "'",
            "ensure the type is defined and the probe compiled successfully");
    }

    const auto& type = it->second;

    for (const auto& field : mtype.fields) {
      if (!type.fields.contains(field)) {
        fatal("missing FIELD record for '" + mtype.c_type + "::" + field + "'",
              "check that field exists in the struct or add required headers to includes");
      }
    }

    const auto policy = effective_bitfield_policy(manifest, mtype);
    for (const auto& bf : mtype.bitfields) {
      if (policy == "accessor_shim") {
        if (!type.bitfield_accessors.contains(bf)) {
          fatal("missing BITFIELD_ACCESSOR record for '" + mtype.c_type + "::" + bf + "'",
                "check that bitfield exists or verify bitfield_policy = \"accessor_shim\"");
        }
      } else if (policy == "geometry") {
        if (!type.bitfield_geometry.contains(bf)) {
          fatal("missing bitfield geometry for '" + mtype.c_type + "::" + bf + "'",
                "ensure probe was compiled with -g and DWARF sections are present");
        }
      }
    }
  }

  for (const auto& cst : manifest.constants) {
    if (!model.constants.contains(cst.name)) {
      fatal("missing CONST record for '" + cst.name + "'",
            "check that constant expression is valid and included headers are correct");
    }
  }

  for (const auto& en : manifest.enums) {
    auto it = model.enums.find(en.c_type);
    if (it == model.enums.end()) {
      fatal("missing ENUM records for '" + en.c_type + "'",
            "check that enum is defined and included headers are correct");
    }
    if (en.extract_all) {
      if (it->second.empty()) {
        fatal("extract_all failed for enum '" + en.c_type + "' (no enumerators found)");
      }
      continue;
    }
    for (const auto& v : en.values) {
      if (!it->second.contains(v)) {
        fatal("missing ENUM record for '" + en.c_type + "::" + v + "'",
              "check that enumerator is visible in the probe TU");
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Header Rendering
// -----------------------------------------------------------------------------

struct RequestedType {
  std::string c_type;
  std::string bitfield_policy;
  std::vector<std::string> fields;
  std::vector<std::string> bitfields;
};

std::vector<RequestedType> normalize_requests(const Manifest& manifest) {
  std::vector<RequestedType> out;
  std::map<std::string, std::size_t, std::less<>> index;
  std::map<std::string, std::map<std::string, bool, std::less<>>, std::less<>> fields_seen;
  std::map<std::string, std::map<std::string, bool, std::less<>>, std::less<>> bitfields_seen;

  for (const auto& mtype : manifest.types) {
    const auto policy = effective_bitfield_policy(manifest, mtype);
    auto it = index.find(mtype.c_type);
    if (it == index.end()) {
      index[mtype.c_type] = out.size();
      out.push_back(RequestedType{mtype.c_type, policy, {}, {}});
      fields_seen[mtype.c_type] = {};
      bitfields_seen[mtype.c_type] = {};
      it = index.find(mtype.c_type);
    } else if (out[it->second].bitfield_policy != policy) {
      fatal("manifest: conflicting bitfield_policy for type '" + mtype.c_type + "'");
    }

    auto& dst = out[it->second];
    for (const auto& f : mtype.fields) {
      if (!fields_seen[mtype.c_type].contains(f)) {
        fields_seen[mtype.c_type][f] = true;
        dst.fields.push_back(f);
      }
    }
    for (const auto& bf : mtype.bitfields) {
      if (!bitfields_seen[mtype.c_type].contains(bf)) {
        bitfields_seen[mtype.c_type][bf] = true;
        dst.bitfields.push_back(bf);
      }
    }
  }

  return out;
}

void check_identifier_collisions(const Manifest& manifest, const std::vector<RequestedType>& req) {
  std::map<std::string, std::vector<std::string>, std::less<>> types_by_escaped;

  for (const auto& t : req) {
    types_by_escaped[escape_identifier(t.c_type)].push_back(t.c_type);
  }

  std::ostringstream msg;
  bool any = false;

  for (const auto& [escaped, originals] : types_by_escaped) {
    if (originals.size() > 1) {
      any = true;
      msg << "type collision: " << escaped << " <- ";
      for (std::size_t i = 0; i < originals.size(); ++i) {
        if (i) msg << ", ";
        msg << "'" << originals[i] << "'";
      }
      msg << "\n";
    }
  }

  for (const auto& t : req) {
    std::map<std::string, std::vector<std::string>, std::less<>> fields_by_escaped;
    for (const auto& f : t.fields) {
      fields_by_escaped[escape_identifier(f)].push_back(f);
    }
    for (const auto& [escaped, originals] : fields_by_escaped) {
      if (originals.size() > 1) {
        any = true;
        msg << "field collision in '" << t.c_type << "': " << escaped << " <- ";
        for (std::size_t i = 0; i < originals.size(); ++i) {
          if (i) msg << ", ";
          msg << "'" << originals[i] << "'";
        }
        msg << "\n";
      }
    }
  }

  for (const auto& t : req) {
    std::map<std::string, std::vector<std::string>, std::less<>> bitfields_by_escaped;
    for (const auto& bf : t.bitfields) {
      bitfields_by_escaped[escape_identifier(bf)].push_back(bf);
    }
    for (const auto& [escaped, originals] : bitfields_by_escaped) {
      if (originals.size() > 1) {
        any = true;
        msg << "bitfield collision in '" << t.c_type << "': " << escaped << " <- ";
        for (std::size_t i = 0; i < originals.size(); ++i) {
          if (i) msg << ", ";
          msg << "'" << originals[i] << "'";
        }
        msg << "\n";
      }
    }
  }

  std::map<std::string, std::vector<std::string>, std::less<>> consts_by_escaped;
  for (const auto& cst : manifest.constants) {
    consts_by_escaped[escape_identifier(cst.name)].push_back(cst.name);
  }
  for (const auto& [escaped, originals] : consts_by_escaped) {
    if (originals.size() > 1) {
      any = true;
      msg << "constant collision: " << escaped << " <- ";
      for (std::size_t i = 0; i < originals.size(); ++i) {
        if (i) msg << ", ";
        msg << "'" << originals[i] << "'";
      }
      msg << "\n";
    }
  }

  std::map<std::string, std::vector<std::string>, std::less<>> enums_by_escaped;
  for (const auto& en : manifest.enums) {
    enums_by_escaped[escape_identifier(en.c_type)].push_back(en.c_type);
  }
  for (const auto& [escaped, originals] : enums_by_escaped) {
    if (originals.size() > 1) {
      any = true;
      msg << "enum type collision: " << escaped << " <- ";
      for (std::size_t i = 0; i < originals.size(); ++i) {
        if (i) msg << ", ";
        msg << "'" << originals[i] << "'";
      }
      msg << "\n";
    }
  }

  for (const auto& en : manifest.enums) {
    std::map<std::string, std::vector<std::string>, std::less<>> values_by_escaped;
    for (const auto& v : en.values) {
      values_by_escaped[escape_identifier(v)].push_back(v);
    }
    for (const auto& [escaped, originals] : values_by_escaped) {
      if (originals.size() > 1) {
        any = true;
        msg << "enum value collision in '" << en.c_type << "': " << escaped << " <- ";
        for (std::size_t i = 0; i < originals.size(); ++i) {
          if (i) msg << ", ";
          msg << "'" << originals[i] << "'";
        }
        msg << "\n";
      }
    }
  }

  if (any) {
    fatal("identifier collision(s):\n" + msg.str(),
          "rename fields/types to avoid collision after escaping");
  }
}

struct EmitEntry {
  std::string type_name;
  std::uint16_t kind;  // 1=TYPE, 2=FIELD
  std::string field_name;
};

std::vector<EmitEntry> build_emit_plan(const std::vector<RequestedType>& req) {
  std::vector<EmitEntry> out;

  for (const auto& t : req) {
    out.push_back(EmitEntry{t.c_type, 1, ""});
    for (const auto& f : t.fields) {
      out.push_back(EmitEntry{t.c_type, 2, f});
    }
  }

  return out;
}

std::string render_header_h(const Manifest& manifest, const Model& model, std::uint64_t hash64,
                            const std::array<std::uint8_t, 32>& hash256) {
  std::ostringstream oss;

  oss << "// Auto-generated by IRO layout_parse " << kLayoutParseVersion << "\n";
  oss << "// input_hash256=" << to_hex(hash256) << "\n";
  oss << "#ifndef IRO_LAYOUT_" << escape_identifier(manifest.set) << "_H_\n";
  oss << "#define IRO_LAYOUT_" << escape_identifier(manifest.set) << "_H_\n\n";
  oss << "#define IRO_LAYOUT_SCHEMA_MAJOR " << kLayoutSchemaMajor << "\n";
  oss << "#define IRO_LAYOUT_SCHEMA_MINOR " << kLayoutSchemaMinor << "\n";

  const auto set_id = escape_identifier(manifest.set);
  oss << "#define IRO_LAYOUT_INPUT_HASH64__" << set_id << " 0x" << std::hex << std::setw(16)
      << std::setfill('0') << hash64 << "ull\n";
  oss << std::dec;

  const auto req = normalize_requests(manifest);
  check_identifier_collisions(manifest, req);

  oss << "\n";

  for (const auto& t : req) {
    const auto type_id = escape_identifier(t.c_type);
    const auto model_it = model.types.find(t.c_type);
    if (model_it == model.types.end()) {
      fatal("internal: missing model type for emission");
    }
    const auto& type_model = model_it->second;

    oss << "#define IRO_SIZEOF__" << type_id << " " << type_model.sizeof_type << "ull\n";
    oss << "#define IRO_ALIGNOF__" << type_id << " "
        << static_cast<std::uint64_t>(type_model.alignof_type) << "ull\n";
    for (const auto& field_name : t.fields) {
      const auto field_id = escape_identifier(field_name);
      const auto offset = type_model.fields.at(field_name);
      oss << "#define IRO_OFFSETOF__" << type_id << "__" << field_id << " " << offset << "ull\n";
    }

    if (t.bitfield_policy == "geometry") {
      for (const auto& bf : t.bitfields) {
        const auto bf_id = escape_identifier(bf);
        const auto& geom = type_model.bitfield_geometry.at(bf);
        oss << "#define IRO_BF_ABS_BIT_OFFSET__" << type_id << "__" << bf_id << " "
            << geom.abs_bit_offset << "ull\n";
        oss << "#define IRO_BF_BIT_SIZE__" << type_id << "__" << bf_id << " "
            << static_cast<std::uint64_t>(geom.bit_size) << "ull\n";
        oss << "#define IRO_BF_IS_SIGNED__" << type_id << "__" << bf_id << " "
            << static_cast<std::uint64_t>(geom.is_signed) << "ull\n";
        oss << "#define IRO_BF_BYTE_OFFSET__" << type_id << "__" << bf_id << " "
            << geom.byte_offset << "ull\n";
        oss << "#define IRO_BF_BIT_IN_BYTE__" << type_id << "__" << bf_id << " "
            << static_cast<std::uint64_t>(geom.bit_in_byte) << "ull\n";
        oss << "#define IRO_BF_SPAN_BYTES__" << type_id << "__" << bf_id << " "
            << static_cast<std::uint64_t>(geom.span_bytes) << "ull\n";
      }
    } else if (t.bitfield_policy == "accessor_shim") {
      for (const auto& bf : t.bitfields) {
        const auto bf_id = escape_identifier(bf);
        const auto id = type_model.bitfield_accessors.at(bf);
        oss << "#define IRO_BF_ACCESSOR_ID__" << type_id << "__" << bf_id << " 0x" << std::hex
            << std::setw(16) << std::setfill('0') << id << "ull\n"
            << std::dec;
      }
    }
  }

  if (!manifest.constants.empty()) {
    oss << "\n";
    for (const auto& cst : manifest.constants) {
      const auto id = escape_identifier(cst.name);
      const auto value = model.constants.at(cst.name);
      oss << "#define IRO_CONST__" << id << " " << value << "ull\n";
    }
  }

  if (!manifest.enums.empty()) {
    oss << "\n";
    for (const auto& en : manifest.enums) {
      const auto enum_id = escape_identifier(en.c_type);
      auto it = model.enums.find(en.c_type);
      if (it == model.enums.end()) {
        fatal("internal: missing enum values for emission");
      }
      std::vector<std::pair<std::string, dwarf::EnumValue>> values;
      if (en.extract_all) {
        values.reserve(it->second.size());
        for (const auto& [name, val] : it->second) {
          values.push_back({name, val});
        }
        std::sort(values.begin(), values.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
      } else {
        values.reserve(en.values.size());
        for (const auto& name : en.values) {
          values.push_back({name, it->second.at(name)});
        }
      }

      std::map<std::string, std::vector<std::string>, std::less<>> escaped_values;
      for (const auto& [name, _] : values) {
        escaped_values[escape_identifier(name)].push_back(name);
      }
      for (const auto& [escaped, originals] : escaped_values) {
        if (originals.size() > 1) {
          std::ostringstream err;
          err << "enum value collision in '" << en.c_type << "': " << escaped << " <- ";
          for (std::size_t i = 0; i < originals.size(); ++i) {
            if (i) err << ", ";
            err << "'" << originals[i] << "'";
          }
          fatal(err.str());
        }
      }

      for (const auto& [name, ev] : values) {
        const auto val_id = escape_identifier(name);
        const auto sval = static_cast<std::int64_t>(ev.raw);
        if (ev.is_signed && sval < 0) {
          oss << "#define IRO_ENUM__" << enum_id << "__" << val_id << " (" << sval << "ll)\n";
        } else {
          oss << "#define IRO_ENUM__" << enum_id << "__" << val_id << " " << ev.raw << "ull\n";
        }
      }
    }
  }

  oss << "\n#endif  // IRO_LAYOUT_" << escape_identifier(manifest.set) << "_H_\n";
  return oss.str();
}

std::string render_header_hpp(const Manifest& manifest, const Model& model) {
  const auto set_id = escape_identifier(manifest.set);
  std::ostringstream oss;

  oss << "// Auto-generated by IRO layout_parse " << kLayoutParseVersion << "\n";
  oss << "#pragma once\n\n";
  oss << "#include <generated/iro/layout_" << manifest.set << ".h>\n";
  oss << "#include <cstddef>\n";
  oss << "#include <cstdint>\n\n";

  oss << "namespace iro::bf {\n\n";
  oss << "template <class U>\n";
  oss << "constexpr U extract_bits(const unsigned char* base, unsigned byte_offset,\n";
  oss << "                         unsigned bit_in_byte, unsigned bit_size,\n";
  oss << "                         unsigned span_bytes) {\n";
  oss << "  unsigned __int128 acc = 0;\n";
  oss << "  for (unsigned i = 0; i < span_bytes; ++i) {\n";
  oss << "    acc |= (static_cast<unsigned __int128>(base[byte_offset + i]) << (i * 8));\n";
  oss << "  }\n";
  oss << "  acc >>= bit_in_byte;\n";
  oss << "  const unsigned __int128 mask = (bit_size == 128)\n";
  oss << "      ? static_cast<unsigned __int128>(~0)\n";
  oss << "      : ((static_cast<unsigned __int128>(1) << bit_size) - 1);\n";
  oss << "  return static_cast<U>(acc & mask);\n";
  oss << "}\n\n";

  oss << "template <class U>\n";
  oss << "constexpr void insert_bits(unsigned char* base, unsigned byte_offset,\n";
  oss << "                           unsigned bit_in_byte, unsigned bit_size,\n";
  oss << "                           unsigned span_bytes, U value) {\n";
  oss << "  unsigned __int128 acc = 0;\n";
  oss << "  for (unsigned i = 0; i < span_bytes; ++i) {\n";
  oss << "    acc |= (static_cast<unsigned __int128>(base[byte_offset + i]) << (i * 8));\n";
  oss << "  }\n";
  oss << "  const unsigned __int128 mask =\n";
  oss << "      ((bit_size == 128)\n";
  oss << "           ? static_cast<unsigned __int128>(~0)\n";
  oss << "           : ((static_cast<unsigned __int128>(1) << bit_size) - 1))\n";
  oss << "      << bit_in_byte;\n";
  oss << "  acc = (acc & ~mask) | ((static_cast<unsigned __int128>(value) << bit_in_byte) & mask);\n";
  oss << "  for (unsigned i = 0; i < span_bytes; ++i) {\n";
  oss << "    base[byte_offset + i] = static_cast<unsigned char>((acc >> (i * 8)) & 0xffu);\n";
  oss << "  }\n";
  oss << "}\n\n";
  oss << "}  // namespace iro::bf\n\n";

  oss << "namespace iro::layout::" << set_id << " {\n\n";
  oss << "inline constexpr std::uint16_t schema_major = " << kLayoutSchemaMajor << ";\n";
  oss << "inline constexpr std::uint16_t schema_minor = " << kLayoutSchemaMinor << ";\n";
  oss << "inline constexpr std::uint64_t input_hash64 = IRO_LAYOUT_INPUT_HASH64__" << set_id
      << ";\n\n";

  const auto req = normalize_requests(manifest);

  for (const auto& t : req) {
    const auto type_id = escape_identifier(t.c_type);
    oss << "struct " << type_id << " {\n";
    oss << "  static constexpr std::size_t size = IRO_SIZEOF__" << type_id << ";\n";
    oss << "  static constexpr std::size_t align = IRO_ALIGNOF__" << type_id << ";\n";
    for (const auto& field_name : t.fields) {
      const auto field_id = escape_identifier(field_name);
      oss << "  static constexpr std::size_t " << field_id << "_offset = IRO_OFFSETOF__"
          << type_id << "__" << field_id << ";\n";
    }
    if (t.bitfield_policy == "geometry") {
      for (const auto& bf : t.bitfields) {
        const auto bf_id = escape_identifier(bf);
        oss << "  static constexpr std::uint64_t " << bf_id << "_abs_bit_offset = "
            << "IRO_BF_ABS_BIT_OFFSET__" << type_id << "__" << bf_id << ";\n";
        oss << "  static constexpr std::uint16_t " << bf_id << "_bit_size = "
            << "IRO_BF_BIT_SIZE__" << type_id << "__" << bf_id << ";\n";
        oss << "  static constexpr std::uint8_t " << bf_id << "_is_signed = "
            << "IRO_BF_IS_SIGNED__" << type_id << "__" << bf_id << ";\n";
        oss << "  static constexpr std::uint64_t " << bf_id << "_byte_offset = "
            << "IRO_BF_BYTE_OFFSET__" << type_id << "__" << bf_id << ";\n";
        oss << "  static constexpr std::uint8_t " << bf_id << "_bit_in_byte = "
            << "IRO_BF_BIT_IN_BYTE__" << type_id << "__" << bf_id << ";\n";
        oss << "  static constexpr std::uint8_t " << bf_id << "_span_bytes = "
            << "IRO_BF_SPAN_BYTES__" << type_id << "__" << bf_id << ";\n";
      }
    } else if (t.bitfield_policy == "accessor_shim") {
      for (const auto& bf : t.bitfields) {
        const auto bf_id = escape_identifier(bf);
        oss << "  static constexpr std::uint64_t " << bf_id << "_accessor_id = "
            << "IRO_BF_ACCESSOR_ID__" << type_id << "__" << bf_id << ";\n";
      }
    }
    oss << "};\n\n";
  }

  if (!manifest.constants.empty()) {
    oss << "namespace constants {\n";
    for (const auto& cst : manifest.constants) {
      const auto id = escape_identifier(cst.name);
      oss << "inline constexpr std::uint64_t " << id << " = IRO_CONST__" << id << ";\n";
    }
    oss << "}  // namespace constants\n\n";
  }

  if (!manifest.enums.empty()) {
    oss << "namespace enums {\n";
    for (const auto& en : manifest.enums) {
      const auto enum_id = escape_identifier(en.c_type);
      oss << "namespace " << enum_id << " {\n";
      auto it = model.enums.find(en.c_type);
      if (it == model.enums.end()) {
        fatal("internal: missing enum values for emission");
      }
      if (en.extract_all) {
        std::vector<std::pair<std::string, dwarf::EnumValue>> values;
        values.reserve(it->second.size());
        for (const auto& [name, val] : it->second) {
          values.push_back({name, val});
        }
        std::sort(values.begin(), values.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [name, _] : values) {
          const auto val_id = escape_identifier(name);
          oss << "inline constexpr auto " << val_id << " = IRO_ENUM__" << enum_id << "__"
              << val_id << ";\n";
        }
      } else {
        for (const auto& name : en.values) {
          const auto val_id = escape_identifier(name);
          oss << "inline constexpr auto " << val_id << " = IRO_ENUM__" << enum_id << "__"
              << val_id << ";\n";
        }
      }
      oss << "}  // namespace " << enum_id << "\n";
    }
    oss << "}  // namespace enums\n\n";
  }

  oss << "}  // namespace iro::layout::" << set_id << "\n";
  return oss.str();
}

std::string render_meta(const Manifest& manifest, const Descriptor& desc, const InputHash& hash,
                        const std::array<std::uint8_t, 32>& manifest_hash256,
                        const std::array<std::uint8_t, 32>& probe_cmd_hash256,
                        const ProbeCmdInfo& probe_cmd) {
  std::ostringstream oss;

  oss << "set=" << manifest.set << "\n";
  oss << "schema=" << kLayoutSchemaMajor << "." << kLayoutSchemaMinor << "\n";
  oss << "input_hash64=0x" << std::hex << std::setw(16) << std::setfill('0') << hash.h64 << "\n";
  oss << "note_input_hash64=0x" << std::hex << std::setw(16) << std::setfill('0')
      << desc.input_hash64 << "\n";
  oss << "input_hash256=" << to_hex(hash.h256) << "\n";
  oss << "manifest_hash256=" << to_hex(manifest_hash256) << "\n";
  oss << "probe_cmd_hash256=" << to_hex(probe_cmd_hash256) << "\n";
  oss << "probe_cc=" << probe_cmd.cc << "\n";
  oss << "probe_target=" << probe_cmd.target << "\n";
  oss << "probe_arch=" << probe_cmd.arch << "\n";
  if (probe_cmd.cwd) oss << "probe_cwd=" << *probe_cmd.cwd << "\n";
  oss << "probe_argv_hex_len=" << std::dec << probe_cmd.argv_hex.size() << "\n";
  for (const auto& e : probe_cmd.env) oss << "probe_env=" << e << "\n";
  for (const auto& l : probe_cmd.unknown_lines) oss << "probe_unknown=" << l << "\n";
  oss << "layout_parse_version=" << kLayoutParseVersion << "\n";
  oss << "gen_probe_version=" << kGenProbeVersion << "\n";

  return oss.str();
}

std::string render_stamp(const Manifest& manifest) {
  std::ostringstream oss;
  oss << "IRO layout ready: " << manifest.set << "\n";
  return oss.str();
}

// -----------------------------------------------------------------------------
// Output Paths
// -----------------------------------------------------------------------------

struct OutputPaths {
  fs::path h;
  fs::path hpp;
  fs::path meta;
  fs::path stamp;
};

OutputPaths compute_output_paths(const Manifest& manifest, const fs::path& outdir) {
  const fs::path gen_dir = outdir / "include/generated/iro";
  return OutputPaths{
      .h = gen_dir / ("layout_" + manifest.set + ".h"),
      .hpp = gen_dir / ("layout_" + manifest.set + ".hpp"),
      .meta = gen_dir / ("layout_" + manifest.set + ".meta"),
      .stamp = gen_dir / ("layout_" + manifest.set + ".stamp"),
  };
}

void emit_outputs(const Manifest& manifest, const Model& model, const InputHash& hash,
                  const std::array<std::uint8_t, 32>& manifest_hash256,
                  const std::array<std::uint8_t, 32>& probe_cmd_hash256,
                  const ProbeCmdInfo& probe_cmd, const fs::path& outdir) {
  const auto paths = compute_output_paths(manifest, outdir);
  const auto h = render_header_h(manifest, model, hash.h64, hash.h256);
  const auto hpp = render_header_hpp(manifest, model);
  const auto meta =
      render_meta(manifest, model.desc, hash, manifest_hash256, probe_cmd_hash256, probe_cmd);
  const auto stamp = render_stamp(manifest);

  const bool need_h = !fs::exists(paths.h) || read_text(paths.h) != h;
  const bool need_hpp = !fs::exists(paths.hpp) || read_text(paths.hpp) != hpp;
  const bool need_meta = !fs::exists(paths.meta) || read_text(paths.meta) != meta;
  const bool need_stamp = !fs::exists(paths.stamp) || read_text(paths.stamp) != stamp;
  const bool any_changed = need_h || need_hpp || need_meta || need_stamp;

  if (!any_changed) return;

  // Per §14.4: write all outputs atomically together
  if (need_h) write_if_changed(paths.h, h);
  if (need_hpp) write_if_changed(paths.hpp, hpp);
  if (need_meta) write_if_changed(paths.meta, meta);

  // Stamp is always updated last as the barrier
  fs::create_directories(paths.stamp.parent_path());
  atomic_write(paths.stamp, stamp);
}

void verify_outputs(const Manifest& manifest, const Model& model, const InputHash& hash,
                    const std::array<std::uint8_t, 32>& manifest_hash256,
                    const std::array<std::uint8_t, 32>& probe_cmd_hash256,
                    const ProbeCmdInfo& probe_cmd, const fs::path& outdir) {
  const auto paths = compute_output_paths(manifest, outdir);

  auto check = [](const fs::path& p, const std::string& expected) {
    if (!fs::exists(p)) {
      fatal("verify: missing expected file " + p.string());
    }
    const auto current = read_text(p);
    if (current != expected) {
      fatal("verify: content mismatch for " + p.string(),
            "regenerate with --emit or investigate drift");
    }
  };

  check(paths.h, render_header_h(manifest, model, hash.h64, hash.h256));
  check(paths.hpp, render_header_hpp(manifest, model));
  check(paths.meta,
        render_meta(manifest, model.desc, hash, manifest_hash256, probe_cmd_hash256, probe_cmd));
  check(paths.stamp, render_stamp(manifest));
}

// -----------------------------------------------------------------------------
// Dump Mode
// -----------------------------------------------------------------------------

void dump(const Descriptor& desc) {
  std::cout << "Set: " << desc.set_name << "\n";
  std::cout << "Hash64: 0x" << std::hex << std::setw(16) << std::setfill('0') << desc.input_hash64
            << std::dec << "\n";
  std::cout << "Records: " << desc.records.size() << "\n";

  for (const auto& r : desc.records) {
    std::cout << " - kind=";
    switch (r.kind) {
      case NoteRecord::Kind::Type:
        std::cout << "TYPE";
        break;
      case NoteRecord::Kind::Field:
        std::cout << "FIELD";
        break;
      case NoteRecord::Kind::BitfieldAccessor:
        std::cout << "BITFIELD";
        break;
      case NoteRecord::Kind::Const:
        std::cout << "CONST";
        break;
      case NoteRecord::Kind::Enum:
        std::cout << "ENUM";
        break;
    }
    std::cout << " type='" << r.type_name << "'";
    if (!r.field_name.empty()) std::cout << " field='" << r.field_name << "'";
    if (r.kind == NoteRecord::Kind::Field) std::cout << " offset=" << r.offset_or_id;
    if (r.kind == NoteRecord::Kind::BitfieldAccessor) {
      std::cout << " accessor_id=0x" << std::hex << std::setw(16) << std::setfill('0')
                << r.offset_or_id << std::dec;
    }
    if (r.kind == NoteRecord::Kind::Const || r.kind == NoteRecord::Kind::Enum) {
      std::cout << " value=" << r.offset_or_id;
    }
    std::cout << " sizeof=" << r.sizeof_type << " align=" << r.alignof_type << "\n";
  }
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int real_main(int argc, char** argv) {
  // Verify SHA-256 implementation before any hash operations
  verify_sha256_implementation();

  const auto args = parse_args(argc, argv);

  // Parse manifest
  const auto manifest = parse_manifest_file(args.manifest);

  // Read and parse probe.cmd
  const auto probe_cmd_bytes = read_bytes(args.probe_cmd);
  const auto probe_cmd_hash256 = sha256_bytes(
      std::span<const std::byte>(probe_cmd_bytes.data(), probe_cmd_bytes.size()));
  const auto probe_cmd_text =
      std::string(reinterpret_cast<const char*>(probe_cmd_bytes.data()), probe_cmd_bytes.size());
  const auto probe_cmd = parse_probe_cmd(probe_cmd_text);

  // Compute input hash
  const auto input_hash = compute_input_hash(manifest.raw_bytes, probe_cmd_bytes);

  // Validate arch compatibility
  if (!manifest.target_arch.empty() &&
      std::find(manifest.target_arch.begin(), manifest.target_arch.end(), probe_cmd.arch) ==
          manifest.target_arch.end()) {
    fatal("probe.cmd arch '" + probe_cmd.arch + "' not permitted by manifest target.arch",
          "add \"" + probe_cmd.arch + "\" to [target].arch in manifest");
  }

  // Hash-only mode
  if (args.hash64_only) {
    std::cout << "0x" << format_hex64(input_hash.h64) << "\n";
    return 0;
  }

  // Parse probe object (with cross-compilation safety check)
  const auto probe_obj_bytes = read_bytes(args.probe_obj);
  const auto desc = parse_note(probe_obj_bytes, probe_cmd.arch);
  auto model = build_model(desc);

  bool need_dwarf = false;
  for (const auto& type : manifest.types) {
    const auto policy = effective_bitfield_policy(manifest, type);
    if (policy == "geometry" && !type.bitfields.empty()) {
      need_dwarf = true;
      break;
    }
  }
  if (!need_dwarf) {
    for (const auto& en : manifest.enums) {
      if (en.extract_all) {
        need_dwarf = true;
        break;
      }
    }
  }

  if (need_dwarf) {
    const auto dwarf_result = dwarf::extract_dwarf(probe_obj_bytes, manifest);
    for (const auto& [type_name, fields] : dwarf_result.bitfields) {
      auto it_type = model.types.find(type_name);
      if (it_type == model.types.end()) {
        fatal("DWARF: missing TYPE record for '" + type_name + "'");
      }
      auto& type = it_type->second;
      for (const auto& [field, geom] : fields) {
        auto it = type.bitfield_geometry.find(field);
        if (it != type.bitfield_geometry.end()) {
          const auto& existing = it->second;
          if (existing.abs_bit_offset != geom.abs_bit_offset ||
              existing.bit_size != geom.bit_size || existing.is_signed != geom.is_signed) {
            fatal("conflicting DWARF bitfield geometry for '" + type_name + "::" + field + "'");
          }
        } else {
          type.bitfield_geometry.emplace(field, geom);
        }
      }
    }
    for (const auto& [enum_type, values] : dwarf_result.enums_all) {
      auto& out = model.enums[enum_type];
      for (const auto& v : values) {
        auto it = out.find(v.name);
        if (it != out.end() &&
            (it->second.raw != v.raw || it->second.is_signed != v.is_signed)) {
          fatal("conflicting DWARF enum value for '" + enum_type + "::" + v.name + "'");
        }
        out.emplace(v.name, v);
      }
    }
  }

  // Validate hash
  if (input_hash.h64 != desc.input_hash64) {
    fatal("input_hash64 mismatch: computed 0x" + format_hex64(input_hash.h64) + " vs note 0x" +
              format_hex64(desc.input_hash64),
          "rebuild probe to resync");
  }

  // Validate manifest against model
  validate_against_manifest(manifest, model);

  // Dump mode
  if (args.dump) {
    dump(desc);
  }

  // Compute manifest hash for meta
  const auto manifest_hash256 = sha256_bytes(
      std::span<const std::byte>(manifest.raw_bytes.data(), manifest.raw_bytes.size()));

  // Emit mode
  if (args.emit) {
    emit_outputs(manifest, model, input_hash, manifest_hash256, probe_cmd_hash256, probe_cmd,
                 args.outdir);
  }

  // Verify mode
  if (args.verify) {
    verify_outputs(manifest, model, input_hash, manifest_hash256, probe_cmd_hash256, probe_cmd,
                   args.outdir);
  }

  return 0;
}

}  // namespace iro::tool

int main(int argc, char** argv) {
  try {
    return iro::tool::real_main(argc, argv);
  } catch (const iro::ToolError& e) {
    std::fprintf(stderr, "layout_parse: %s\n", e.what());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "layout_parse: unexpected error: %s\n", e.what());
  }
  return 1;
}
