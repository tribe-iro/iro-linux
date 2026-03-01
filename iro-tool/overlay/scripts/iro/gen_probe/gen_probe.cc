// SPDX-License-Identifier: GPL-2.0-only
// IRO probe generator (gen_probe):
// - Reads an IML manifest and emits a probe C translation unit
// - Conforms to IRO-TOOL-SPEC-4.2 (§7, §8)

#include "../iro_common.hpp"
#include "../iro_manifest.hpp"

#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

namespace iro::gen {

using namespace iro;

// -----------------------------------------------------------------------------
// CLI Arguments
// -----------------------------------------------------------------------------

struct Args {
  fs::path manifest;
  fs::path out_c;
  bool dump{false};
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string_view a(argv[i]);
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) fatal("missing value after " + std::string(a));
      return argv[++i];
    };
    if (a == "--manifest") {
      args.manifest = next();
    } else if (a == "--out") {
      args.out_c = next();
    } else if (a == "--dump") {
      args.dump = true;
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage: gen_probe --manifest M --out C [--dump]\n";
      std::cout << "\nVersion: " << kGenProbeVersion << " (schema " << kLayoutSchemaMajor << "."
                << kLayoutSchemaMinor << ")\n";
      std::exit(0);
    } else {
      fatal("unknown arg: " + std::string(a));
    }
  }
  if (args.manifest.empty() || args.out_c.empty()) {
    fatal("manifest and out are required");
  }
  return args;
}

// -----------------------------------------------------------------------------
// C String Escaping
// -----------------------------------------------------------------------------

std::string c_escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

// -----------------------------------------------------------------------------
// Record Structures for Probe Generation
// -----------------------------------------------------------------------------

struct Record {
  enum Kind { Type = 1, Field = 2, BitfieldAccessor = 3, Const = 4, Enum = 5 };
  Kind kind;
  std::string type;
  std::string field;
  std::uint16_t flags{0};
  std::string value_expr;
};

std::string constant_value_expr(const ManifestConstant& c) {
  if (c.c_type) {
    return "((" + *c.c_type + ")(" + c.expr + "))";
  }
  return "(" + c.expr + ")";
}

std::vector<Record> expand_records(const Manifest& m) {
  std::vector<Record> out;
  std::unordered_map<std::uint64_t, std::string> seen_get_ids;
  std::unordered_map<std::uint64_t, std::string> seen_set_ids;

  for (const auto& t : m.types) {
    const auto policy = effective_bitfield_policy(m, t);

    // TYPE record
    out.push_back(Record{Record::Type, t.c_type, "", 0, "0"});

    // FIELD records
    for (const auto& f : t.fields) {
      out.push_back(Record{Record::Field, t.c_type, f, 0, ""});
    }

    // BITFIELD_ACCESSOR records (§8.4)
    if (policy == "accessor_shim") {
      for (const auto& bf : t.bitfields) {
        // Compute "get" accessor ID with collision resolution
        auto [get_id, unused_get_alt] = compute_accessor_id_with_collision_check(
            m, t.c_type, bf, AccessorKind::Get, seen_get_ids);
        (void)unused_get_alt;
        seen_get_ids[get_id] = t.c_type + "::" + bf + " (get)";

        // Compute "set" accessor ID with collision resolution
        auto [set_id, unused_set_alt] = compute_accessor_id_with_collision_check(
            m, t.c_type, bf, AccessorKind::Set, seen_set_ids);
        (void)unused_set_alt;
        seen_set_ids[set_id] = t.c_type + "::" + bf + " (set)";

        // Only emit one BITFIELD_ACCESSOR record per bitfield (using get ID)
        // The set ID would be used in shim generation, not in the note
        out.push_back(Record{Record::BitfieldAccessor, t.c_type, bf, 0,
                             "UINT64_C(" + std::to_string(get_id) + ")"});
      }
    }
  }

  for (const auto& c : m.constants) {
    out.push_back(Record{Record::Const, c.name, "", 0, constant_value_expr(c)});
  }

  for (const auto& e : m.enums) {
    if (e.extract_all) continue;
    for (const auto& v : e.values) {
      out.push_back(Record{Record::Enum, e.c_type, v, 0, v});
    }
  }

  return out;
}

// -----------------------------------------------------------------------------
// Byte Encoding Helpers for Probe C Output
// -----------------------------------------------------------------------------

std::string u8_byte(std::uint8_t b) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out = "(uint8_t)0x";
  out += kHex[(b >> 4) & 0x0f];
  out += kHex[b & 0x0f];
  return out;
}

std::string u16_bytes(std::uint16_t v) {
  std::ostringstream ss;
  ss << "(uint8_t)(" << v << " & 0xffu), (uint8_t)((" << v << " >> 8) & 0xffu)";
  return ss.str();
}

std::string u16_bytes_expr(const std::string& expr) {
  std::ostringstream ss;
  ss << "(uint8_t)((" << expr << ") & 0xffu), "
     << "(uint8_t)(((uint16_t)(" << expr << ") >> 8) & 0xffu)";
  return ss.str();
}

std::string u32_bytes(std::uint32_t v) {
  std::ostringstream ss;
  ss << "(uint8_t)(" << v << " & 0xffu), (uint8_t)((" << v << " >> 8) & 0xffu), "
     << "(uint8_t)((" << v << " >> 16) & 0xffu), (uint8_t)((" << v << " >> 24) & 0xffu)";
  return ss.str();
}

std::string u32_bytes_expr(const std::string& expr) {
  std::ostringstream ss;
  ss << "(uint8_t)((" << expr << ") & 0xffu), "
     << "(uint8_t)(((uint32_t)(" << expr << ") >> 8) & 0xffu), "
     << "(uint8_t)(((uint32_t)(" << expr << ") >> 16) & 0xffu), "
     << "(uint8_t)(((uint32_t)(" << expr << ") >> 24) & 0xffu)";
  return ss.str();
}

std::string u64_bytes(const std::string& expr) {
  std::ostringstream ss;
  ss << "(uint8_t)((" << expr << ") & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 8) & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 16) & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 24) & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 32) & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 40) & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 48) & 0xffu), "
     << "(uint8_t)(((uint64_t)(" << expr << ") >> 56) & 0xffu)";
  return ss.str();
}

// -----------------------------------------------------------------------------
// Descriptor Size Computation
// -----------------------------------------------------------------------------

struct DescSizes {
  std::uint32_t desc_sz;
  std::uint32_t set_pad;
  std::vector<std::uint32_t> record_sizes;
};

DescSizes compute_sizes(const Manifest& m, const std::vector<Record>& recs) {
  DescSizes sizes{};

  // Set name padding to 4-byte alignment
  const auto set_len = static_cast<std::uint32_t>(m.set.size());
  sizes.set_pad = (4 - (set_len % 4)) % 4;

  // Header (32) + set name + padding
  std::uint32_t total = checked_add_u32(32, set_len, "descriptor header");
  total = checked_add_u32(total, sizes.set_pad, "descriptor set padding");

  // Each record
  for (const auto& r : recs) {
    constexpr std::uint32_t kRecordHeaderSize = 40;
    const auto tlen = static_cast<std::uint32_t>(r.type.size());
    const auto flen = static_cast<std::uint32_t>(r.field.size());
    const std::uint32_t base = checked_add_u32(kRecordHeaderSize, tlen, "record base size");
    const std::uint32_t base_with_field = checked_add_u32(base, flen, "record with field");
    const std::uint32_t pad = (4 - (base_with_field % 4)) % 4;
    const std::uint32_t rsize = checked_add_u32(base_with_field, pad, "record size");
    sizes.record_sizes.push_back(rsize);
    total = checked_add_u32(total, rsize, "total descriptor size");
  }

  sizes.desc_sz = total;
  return sizes;
}

// -----------------------------------------------------------------------------
// Record Byte Emission
// -----------------------------------------------------------------------------

std::string emit_record_bytes(const Record& r, std::uint32_t rsize) {
  std::ostringstream ss;
  constexpr std::uint32_t kRecordHeaderSize = 40;
  const auto tlen = static_cast<std::uint32_t>(r.type.size());
  const auto flen = static_cast<std::uint32_t>(r.field.size());

  // Record header (40 bytes)
  ss << "    " << u32_bytes(rsize) << ", "                     // record_size
     << u16_bytes(static_cast<std::uint16_t>(r.kind)) << ", "; // kind
  if (r.kind == Record::Enum) {
    const std::string enum_flag_expr = "(((int64_t)(" + r.value_expr + ") < 0) ? 0x0002u : 0x0000u)";
    ss << u16_bytes_expr(enum_flag_expr) << ", "; // flags
  } else {
    ss << u16_bytes(r.flags) << ", "; // flags
  }
  ss
     << u32_bytes(tlen) << ", "                               // type_len
     << u32_bytes(flen) << ", "                               // field_len
     << (r.kind == Record::Const || r.kind == Record::Enum
             ? u64_bytes("0")
             : u64_bytes("sizeof(" + r.type + ")"))
     << ", " // sizeof_type
     << (r.kind == Record::Const || r.kind == Record::Enum
             ? u32_bytes(0)
             : u32_bytes_expr("_Alignof(" + r.type + ")"))
     << ", "              // alignof_type
     << u32_bytes(0) << ", ";  // reserved0

  // offset_or_id
  if (r.kind == Record::Field) {
    ss << u64_bytes("(uint64_t)offsetof(" + r.type + ", " + r.field + ")");
  } else if (r.kind == Record::BitfieldAccessor) {
    ss << u64_bytes(r.value_expr);
  } else if (r.kind == Record::Const) {
    ss << u64_bytes("(uint64_t)((__uint128_t)(" + r.value_expr + "))");
  } else if (r.kind == Record::Enum) {
    ss << u64_bytes("(uint64_t)((int64_t)(" + r.value_expr + "))");
  } else {
    ss << u64_bytes("0");
  }
  ss << ", ";

  // Type name bytes
  for (char c : std::string_view(r.type)) {
    ss << u8_byte(static_cast<std::uint8_t>(c)) << ", ";
  }

  // Field name bytes
  for (char c : std::string_view(r.field)) {
    ss << u8_byte(static_cast<std::uint8_t>(c)) << ", ";
  }

  // Padding
  const auto pad = (4 - ((kRecordHeaderSize + tlen + flen) % 4)) % 4;
  for (std::uint32_t i = 0; i < pad; ++i) {
    ss << "0, ";
  }

  ss << "\n";
  return ss.str();
}

// -----------------------------------------------------------------------------
// Probe C Generation
// -----------------------------------------------------------------------------

std::string emit_probe_c(const Manifest& m) {
  const auto recs = expand_records(m);
  const auto sizes = compute_sizes(m, recs);

  std::ostringstream c;

  // File header
  c << "/* Auto-generated by gen_probe " << kGenProbeVersion << " */\n";
  c << "/* IRO-TOOL-SPEC-4.2 conformant probe */\n";
  c << "#include <stddef.h>\n";
  c << "#include <stdint.h>\n";
  c << "#include <limits.h>\n";

  // Kernel headers from manifest
  for (const auto& inc : m.includes) {
    c << "#include <" << inc << ">\n";
  }

  // Hash header (generated by Kbuild)
  c << "#include \"layout_" << m.set << ".probe.hash.h\"\n";
  c << "#ifndef IRO_PROBE_INPUT_HASH64\n";
  c << "#error \"IRO_PROBE_INPUT_HASH64 not defined — ensure probe.hash.h is generated\"\n";
  c << "#endif\n";
  c << "\n";

  // Constant range checks (per §7.4.3)
  for (const auto& cst : m.constants) {
    const auto expr = constant_value_expr(cst);
    c << "_Static_assert((__uint128_t)(" << expr << ") <= UINT64_MAX, "
      << "\"IRO const out of range: " << cst.name << "\");\n";
  }
  c << "\n";

  // Note section
  c << "__attribute__((section(\".note.iro.layout\"),used,aligned(4)))\n";
  c << "const unsigned char iro_note[] = {\n";

  // ELF note header (n_namesz=4, n_descsz, n_type)
  c << "  /* ELF note header */\n";
  c << "  " << u32_bytes(4) << ", "                                 // n_namesz ("IRO\0")
    << u32_bytes(sizes.desc_sz) << ", "                             // n_descsz
    << u32_bytes(kNoteType) << ",\n";                               // n_type
  c << "  'I','R','O',0,\n";                                        // note name

  // Descriptor header (§8.3.1)
  c << "  /* Descriptor header */\n";
  c << "  " << u32_bytes(kDescMagic) << ", "                        // magic
    << u16_bytes(kLayoutSchemaMajor) << ", "                              // version_major
    << u16_bytes(kLayoutSchemaMinor) << ", "                              // version_minor
    << u32_bytes(32) << ", "                                        // header_size
    << u32_bytes(static_cast<std::uint32_t>(recs.size())) << ", "   // record_count
    << u64_bytes("IRO_PROBE_INPUT_HASH64") << ", "                  // input_hash64
    << u32_bytes(static_cast<std::uint32_t>(m.set.size())) << ", "  // set_name_len
    << u32_bytes(0) << ",\n";                                       // reserved

  // Set name
  c << "  /* Set name: \"" << m.set << "\" */\n";
  for (char ch : std::string_view(m.set)) {
    c << "  " << u8_byte(static_cast<std::uint8_t>(ch)) << ",\n";
  }
  for (std::uint32_t i = 0; i < sizes.set_pad; ++i) {
    c << "  0,\n";
  }

  // Records
  c << "  /* Records (" << recs.size() << ") */\n";
  for (std::size_t i = 0; i < recs.size(); ++i) {
    const auto& r = recs[i];
    c << "  /* Record " << i << ": ";
    switch (r.kind) {
      case Record::Type: c << "TYPE"; break;
      case Record::Field: c << "FIELD"; break;
      case Record::BitfieldAccessor: c << "BITFIELD_ACCESSOR"; break;
      case Record::Const: c << "CONST"; break;
      case Record::Enum: c << "ENUM"; break;
    }
    c << " " << r.type;
    if (!r.field.empty()) c << "::" << r.field;
    c << " */\n";
    c << emit_record_bytes(r, sizes.record_sizes[i]);
  }

  c << "};\n";

  return c.str();
}

// -----------------------------------------------------------------------------
// Dump Mode
// -----------------------------------------------------------------------------

void dump_manifest(const Manifest& m) {
  std::cout << "Manifest: " << m.set << "\n";
  std::cout << "Schema version: " << m.schema_version << "\n";
  std::cout << "Target architectures: ";
  for (std::size_t i = 0; i < m.target_arch.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << m.target_arch[i];
  }
  std::cout << "\n";
  std::cout << "Includes:\n";
  for (const auto& inc : m.includes) {
    std::cout << "  - " << inc << "\n";
  }
  std::cout << "Options:\n";
  std::cout << "  bitfield_policy: " << m.options.bitfield_policy << "\n";
  std::cout << "  allow_anonymous_members: "
            << (m.options.allow_anonymous_members ? "true" : "false") << "\n";
  std::cout << "  allow_nested_designators: "
            << (m.options.allow_nested_designators ? "true" : "false") << "\n";
  std::cout << "  allow_array_subscripts: "
            << (m.options.allow_array_subscripts ? "true" : "false") << "\n";
  std::cout << "  strict: " << (m.options.strict ? "true" : "false") << "\n";
  std::cout << "Types:\n";
  for (const auto& t : m.types) {
    std::cout << "  [" << t.manifest_name << "]\n";
    std::cout << "    c_type: " << t.c_type << "\n";
    std::cout << "    fields: [";
    for (std::size_t i = 0; i < t.fields.size(); ++i) {
      if (i) std::cout << ", ";
      std::cout << t.fields[i];
    }
    std::cout << "]\n";
    if (!t.bitfields.empty()) {
      std::cout << "    bitfields: [";
      for (std::size_t i = 0; i < t.bitfields.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << t.bitfields[i];
      }
      std::cout << "]\n";
    }
    if (t.options.bitfield_policy) {
      std::cout << "    options.bitfield_policy: " << *t.options.bitfield_policy << "\n";
    }
  }
  if (!m.enums.empty()) {
    std::cout << "Enums:\n";
    for (const auto& e : m.enums) {
      std::cout << "  [" << e.manifest_name << "]\n";
      std::cout << "    c_type: " << e.c_type << "\n";
      if (e.extract_all) {
        std::cout << "    extract_all: true\n";
      } else {
        std::cout << "    values: [";
        for (std::size_t i = 0; i < e.values.size(); ++i) {
          if (i) std::cout << ", ";
          std::cout << e.values[i];
        }
        std::cout << "]\n";
      }
    }
  }
  if (!m.constants.empty()) {
    std::cout << "Constants:\n";
    for (const auto& cst : m.constants) {
      std::cout << "  - " << cst.name << " = " << cst.expr;
      if (cst.c_type) std::cout << " (" << *cst.c_type << ")";
      std::cout << "\n";
    }
  }

  // Show generated records
  const auto recs = expand_records(m);
  std::cout << "\nGenerated records (" << recs.size() << "):\n";
  for (const auto& r : recs) {
    std::cout << "  - ";
    switch (r.kind) {
      case Record::Type: std::cout << "TYPE"; break;
      case Record::Field: std::cout << "FIELD"; break;
      case Record::BitfieldAccessor: std::cout << "BITFIELD_ACCESSOR"; break;
      case Record::Const: std::cout << "CONST"; break;
      case Record::Enum: std::cout << "ENUM"; break;
    }
    std::cout << " " << r.type;
    if (!r.field.empty()) std::cout << "::" << r.field;
    if (r.kind == Record::BitfieldAccessor) {
      std::cout << " (expr=" << r.value_expr << ")";
    }
    std::cout << "\n";
  }
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int real_main(int argc, char** argv) {
  // Verify SHA-256 implementation before any hash operations (bitfield accessor IDs)
  verify_sha256_implementation();

  const auto args = parse_args(argc, argv);

  // Parse manifest with full validation
  const auto manifest = parse_manifest_file(args.manifest);

  // Dump mode
  if (args.dump) {
    dump_manifest(manifest);
    return 0;
  }

  // Generate probe C
  const auto probe_c = emit_probe_c(manifest);

  // Write output (atomic, skip if unchanged)
  write_if_changed(args.out_c, probe_c);

  return 0;
}

}  // namespace iro::gen

int main(int argc, char** argv) {
  try {
    return iro::gen::real_main(argc, argv);
  } catch (const iro::ToolError& e) {
    std::cerr << "gen_probe: " << e.what() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "gen_probe: unexpected error: " << e.what() << "\n";
  }
  return 1;
}
