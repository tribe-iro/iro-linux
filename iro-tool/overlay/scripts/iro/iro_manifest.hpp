// SPDX-License-Identifier: GPL-2.0-only
// IRO Manifest Language (IML) parser - shared between layout_parse and gen_probe.
// Implements the IML v1.5 subset of TOML 1.0 as defined in IRO-TOOL-SPEC-4.2.

#ifndef IRO_MANIFEST_HPP_
#define IRO_MANIFEST_HPP_

#include "iro_common.hpp"

#include <cinttypes>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace iro {

// -----------------------------------------------------------------------------
// Manifest Data Structures (§6)
// -----------------------------------------------------------------------------

struct ManifestOptions {
  std::string bitfield_policy{"geometry"};
  bool allow_anonymous_members{true};
  bool allow_nested_designators{true};
  bool allow_array_subscripts{true};
  bool strict{true};
};

struct ManifestTypeOptions {
  std::optional<std::string> bitfield_policy;
};

struct ManifestType {
  std::string manifest_name;  // key in [types.X]
  std::string c_type;         // exact C spelling
  std::vector<std::string> fields;
  std::vector<std::string> bitfields;
  ManifestTypeOptions options;
};

struct ManifestEnum {
  std::string manifest_name;  // key in [enums.X]
  std::string c_type;         // C type name
  std::vector<std::string> values;
  bool extract_all{false};
};

struct ManifestConstant {
  std::string name;
  std::string expr;
  std::optional<std::string> c_type;
};

struct Manifest {
  std::string schema_version;
  std::string set;
  std::vector<std::string> includes;
  std::vector<std::string> target_arch;  // optional (compat)
  ManifestOptions options;
  std::vector<ManifestType> types;
  std::vector<ManifestEnum> enums;
  std::vector<ManifestConstant> constants;

  // Raw bytes for hash computation
  std::vector<std::byte> raw_bytes;
};

inline std::string effective_bitfield_policy(const Manifest& m, const ManifestType& t) {
  if (t.options.bitfield_policy) return *t.options.bitfield_policy;
  return m.options.bitfield_policy;
}

// -----------------------------------------------------------------------------
// Member-Designator Parsing (§6.6)
// -----------------------------------------------------------------------------

struct DesignatorComponent {
  std::string name;
  std::optional<std::uint64_t> index;
};

inline std::vector<DesignatorComponent> parse_member_designator(std::string_view s) {
  if (s.empty()) {
    fatal("manifest: empty member designator");
  }

  std::vector<DesignatorComponent> out;
  std::size_t i = 0;

  auto parse_ident = [&](std::string& name) {
    if (i >= s.size()) fatal("manifest: invalid member designator '" + std::string(s) + "'");
    char c = s[i];
    if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
      fatal("manifest: invalid member designator '" + std::string(s) + "' (expected IDENT)");
    }
    std::size_t start = i;
    ++i;
    while (i < s.size()) {
      char ch = s[i];
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
        ++i;
        continue;
      }
      break;
    }
    name = std::string(s.substr(start, i - start));
  };

  while (i < s.size()) {
    if (s[i] == '-') {
      fatal("manifest: invalid member designator '" + std::string(s) + "' (arrow is forbidden)");
    }

    DesignatorComponent comp;
    parse_ident(comp.name);

    if (i < s.size() && s[i] == '[') {
      ++i;  // '['
      if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        fatal("manifest: invalid array subscript in designator '" + std::string(s) + "'");
      }
      std::uint64_t value = 0;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        const auto digit = static_cast<std::uint64_t>(s[i] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
          fatal("manifest: array subscript overflow in designator '" + std::string(s) + "'");
        }
        value = value * 10 + digit;
        ++i;
      }
      if (i >= s.size() || s[i] != ']') {
        fatal("manifest: unterminated array subscript in designator '" + std::string(s) + "'");
      }
      ++i;  // ']'
      comp.index = value;
    }

    out.push_back(std::move(comp));

    if (i == s.size()) break;
    if (s[i] != '.') {
      fatal("manifest: invalid member designator '" + std::string(s) + "'");
    }
    ++i;  // '.'
    if (i == s.size()) {
      fatal("manifest: invalid member designator '" + std::string(s) + "'");
    }
  }

  return out;
}

inline void validate_member_designator(std::string_view s, const ManifestOptions& options,
                                       bool is_bitfield) {
  const auto comps = parse_member_designator(s);

  if (!options.allow_nested_designators && comps.size() > 1) {
    fatal("manifest: nested designators are disabled but got '" + std::string(s) + "'",
          "set allow_nested_designators = true in [options]");
  }

  if (!options.allow_array_subscripts) {
    for (const auto& c : comps) {
      if (c.index) {
        fatal("manifest: array subscripts are disabled but got '" + std::string(s) + "'",
              "set allow_array_subscripts = true in [options]");
      }
    }
  }

  if (is_bitfield && comps.back().index) {
    fatal("manifest: bitfield designator cannot end with array subscript: '" + std::string(s) + "'");
  }
}

// -----------------------------------------------------------------------------
// IML Parser Implementation (TOML 1.0 subset)
// -----------------------------------------------------------------------------

namespace detail {

struct Cursor {
  std::string_view src;
  std::size_t pos{0};
  std::size_t line{1};
  std::size_t col{1};

  bool eof() const { return pos >= src.size(); }

  char peek() const { return eof() ? '\0' : src[pos]; }

  char get() {
    if (eof()) return '\0';
    char c = src[pos++];
    if (c == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
    return c;
  }

  void skip_ws_and_comments() {
    while (!eof()) {
      if (std::isspace(static_cast<unsigned char>(peek()))) {
        get();
        continue;
      }
      if (peek() == '#') {
        while (!eof() && get() != '\n') {}
        continue;
      }
      break;
    }
  }

  std::string location() const {
    return "line " + std::to_string(line) + ", col " + std::to_string(col);
  }
};

struct Value {
  using Array = std::vector<Value>;
  using Table = std::map<std::string, Value>;

  std::variant<std::string, bool, std::int64_t, Array, Table> v;

  Value() = default;
  Value(std::string s) : v(std::move(s)) {}
  Value(bool b) : v(b) {}
  Value(std::int64_t i) : v(i) {}
  Value(Array a) : v(std::move(a)) {}
  Value(Table t) : v(std::move(t)) {}
};

inline std::string parse_string(Cursor& cur) {
  if (cur.get() != '"') {
    fatal("expected opening quote for string at " + cur.location());
  }
  std::string out;
  while (!cur.eof()) {
    char c = cur.get();
    if (c == '"') return out;
    if (c == '\\') {
      if (cur.eof()) fatal("unterminated escape in string at " + cur.location());
      char esc = cur.get();
      switch (esc) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        default:
          fatal("unsupported escape '\\\\" + std::string(1, esc) + "' at " + cur.location());
      }
    } else if (c == '\n') {
      fatal("newline in string literal at " + cur.location());
    } else {
      out.push_back(c);
    }
  }
  fatal("unterminated string literal");
}

inline bool parse_bool(Cursor& cur) {
  if (cur.src.substr(cur.pos, 4) == "true") {
    cur.pos += 4;
    cur.col += 4;
    return true;
  }
  if (cur.src.substr(cur.pos, 5) == "false") {
    cur.pos += 5;
    cur.col += 5;
    return false;
  }
  fatal("expected boolean literal at " + cur.location());
}

inline std::int64_t parse_int(Cursor& cur) {
  bool neg = false;
  if (cur.peek() == '-') {
    neg = true;
    cur.get();
  }
  if (cur.eof() || !std::isdigit(static_cast<unsigned char>(cur.peek()))) {
    fatal("expected integer literal at " + cur.location());
  }
  std::int64_t value = 0;
  while (!cur.eof() && std::isdigit(static_cast<unsigned char>(cur.peek()))) {
    const int digit = cur.get() - '0';
    if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
      fatal("integer literal overflow at " + cur.location());
    }
    value = value * 10 + digit;
  }
  return neg ? -value : value;
}

inline std::string parse_bare_key(Cursor& cur) {
  std::string out;
  while (!cur.eof()) {
    char c = cur.peek();
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
      out.push_back(c);
      cur.get();
    } else {
      break;
    }
  }
  if (out.empty()) fatal("expected key at " + cur.location());
  return out;
}

inline std::vector<std::string> parse_dotted_key(Cursor& cur) {
  std::vector<std::string> parts;
  parts.push_back(parse_bare_key(cur));
  while (!cur.eof() && cur.peek() == '.') {
    cur.get();
    parts.push_back(parse_bare_key(cur));
  }
  return parts;
}

inline Value parse_value(Cursor& cur);

inline Value::Array parse_array(Cursor& cur) {
  if (cur.get() != '[') fatal("expected '[' at " + cur.location());
  Value::Array out;
  while (true) {
    cur.skip_ws_and_comments();
    if (cur.peek() == ']') {
      cur.get();
      return out;
    }
    out.push_back(parse_value(cur));
    cur.skip_ws_and_comments();
    if (cur.peek() == ',') {
      cur.get();
      cur.skip_ws_and_comments();
      if (cur.peek() == ']') {
        cur.get();
        return out;
      }
      continue;
    }
    if (cur.peek() == ']') {
      cur.get();
      return out;
    }
    fatal("expected ',' or ']' at " + cur.location());
  }
}

inline Value::Table parse_inline_table(Cursor& cur) {
  if (cur.get() != '{') fatal("expected '{' at " + cur.location());
  Value::Table out;
  while (true) {
    cur.skip_ws_and_comments();
    if (cur.peek() == '}') {
      cur.get();
      return out;
    }
    std::string key = parse_bare_key(cur);
    cur.skip_ws_and_comments();
    if (cur.get() != '=') fatal("expected '=' in inline table at " + cur.location());
    cur.skip_ws_and_comments();
    auto val = parse_value(cur);
    if (out.contains(key)) {
      fatal("duplicate key in inline table: '" + key + "'");
    }
    out.emplace(key, std::move(val));
    cur.skip_ws_and_comments();
    if (cur.peek() == ',') {
      cur.get();
      cur.skip_ws_and_comments();
      if (cur.peek() == '}') {
        cur.get();
        return out;
      }
      continue;
    }
    if (cur.peek() == '}') {
      cur.get();
      return out;
    }
    fatal("expected ',' or '}' in inline table at " + cur.location());
  }
}

inline Value parse_value(Cursor& cur) {
  if (cur.peek() == '"') return Value(parse_string(cur));
  if (cur.peek() == '[') return Value(parse_array(cur));
  if (cur.peek() == '{') return Value(parse_inline_table(cur));
  if (cur.peek() == 't' || cur.peek() == 'f') return Value(parse_bool(cur));
  if (cur.peek() == '-' || std::isdigit(static_cast<unsigned char>(cur.peek()))) {
    return Value(parse_int(cur));
  }
  fatal("unsupported value type at " + cur.location());
}

inline void consume_trailing(Cursor& cur) {
  while (!cur.eof()) {
    if (cur.peek() == '#') {
      while (!cur.eof() && cur.get() != '\n') {}
      return;
    }
    if (cur.peek() == '\n') {
      cur.get();
      return;
    }
    if (std::isspace(static_cast<unsigned char>(cur.peek()))) {
      cur.get();
      continue;
    }
    fatal("unexpected trailing characters at " + cur.location());
  }
}

inline std::string expect_string(const Value& v, std::string_view what) {
  if (!std::holds_alternative<std::string>(v.v)) {
    fatal(std::string(what) + ": expected string");
  }
  return std::get<std::string>(v.v);
}

inline bool expect_bool(const Value& v, std::string_view what) {
  if (!std::holds_alternative<bool>(v.v)) {
    fatal(std::string(what) + ": expected boolean");
  }
  return std::get<bool>(v.v);
}

inline std::vector<std::string> expect_string_array(const Value& v, std::string_view what) {
  if (!std::holds_alternative<Value::Array>(v.v)) {
    fatal(std::string(what) + ": expected array");
  }
  const auto& arr = std::get<Value::Array>(v.v);
  std::vector<std::string> out;
  out.reserve(arr.size());
  for (const auto& e : arr) {
    if (!std::holds_alternative<std::string>(e.v)) {
      fatal(std::string(what) + ": expected string array");
    }
    out.push_back(std::get<std::string>(e.v));
  }
  return out;
}

inline std::map<std::string, Value> expect_inline_table(const Value& v, std::string_view what) {
  if (!std::holds_alternative<Value::Table>(v.v)) {
    fatal(std::string(what) + ": expected inline table");
  }
  return std::get<Value::Table>(v.v);
}

}  // namespace detail

// Main parsing function with full validation per spec
inline Manifest parse_manifest(const std::string& src) {
  Manifest manifest;
  manifest.raw_bytes.assign(reinterpret_cast<const std::byte*>(src.data()),
                            reinterpret_cast<const std::byte*>(src.data()) + src.size());

  detail::Cursor cur{src};
  std::vector<std::string> current_table;
  std::vector<std::string> unknown_keys;

  struct TypeState {
    ManifestType type;
    bool c_type_set{false};
    bool fields_set{false};
    bool bitfields_set{false};
    bool bitfield_policy_set{false};
  };

  struct EnumState {
    ManifestEnum en;
    bool c_type_set{false};
    bool values_set{false};
    bool extract_all_set{false};
  };

  std::map<std::string, TypeState, std::less<>> types;
  std::vector<std::string> type_order;
  std::map<std::string, EnumState, std::less<>> enums;
  std::vector<std::string> enum_order;
  std::map<std::string, ManifestConstant, std::less<>> constants;
  std::vector<std::string> constant_order;

  auto remember_unknown = [&](std::string_view key) {
    unknown_keys.emplace_back(key);
  };

  auto ensure_type = [&](const std::string& name) -> TypeState& {
    auto it = types.find(name);
    if (it == types.end()) {
      TypeState state;
      state.type.manifest_name = name;
      types.emplace(name, state);
      type_order.push_back(name);
      it = types.find(name);
    }
    return it->second;
  };

  auto ensure_enum = [&](const std::string& name) -> EnumState& {
    auto it = enums.find(name);
    if (it == enums.end()) {
      EnumState state;
      state.en.manifest_name = name;
      enums.emplace(name, state);
      enum_order.push_back(name);
      it = enums.find(name);
    }
    return it->second;
  };

  auto ensure_constant = [&](const std::string& name) -> ManifestConstant& {
    auto it = constants.find(name);
    if (it == constants.end()) {
      ManifestConstant c;
      c.name = name;
      constants.emplace(name, c);
      constant_order.push_back(name);
      it = constants.find(name);
    }
    return it->second;
  };

  auto set_value = [&](const std::string& key, const detail::Value& val) {
    if (current_table.empty()) {
      if (key == "schema_version") {
        manifest.schema_version = detail::expect_string(val, "schema_version");
      } else if (key == "set") {
        manifest.set = detail::expect_string(val, "set");
      } else if (key == "includes") {
        manifest.includes = detail::expect_string_array(val, "includes");
      } else {
        remember_unknown(key);
      }
      return;
    }

    if (current_table.size() == 1 && current_table[0] == "target") {
      if (key == "arch") {
        manifest.target_arch = detail::expect_string_array(val, "target.arch");
      } else {
        remember_unknown("target." + key);
      }
      return;
    }

    if (current_table.size() == 1 && current_table[0] == "options") {
      if (key == "bitfield_policy") {
        manifest.options.bitfield_policy = detail::expect_string(val, "options.bitfield_policy");
      } else if (key == "allow_anonymous_members") {
        manifest.options.allow_anonymous_members =
            detail::expect_bool(val, "options.allow_anonymous_members");
      } else if (key == "allow_nested_designators") {
        manifest.options.allow_nested_designators =
            detail::expect_bool(val, "options.allow_nested_designators");
      } else if (key == "allow_array_subscripts") {
        manifest.options.allow_array_subscripts =
            detail::expect_bool(val, "options.allow_array_subscripts");
      } else if (key == "strict") {
        manifest.options.strict = detail::expect_bool(val, "options.strict");
      } else {
        remember_unknown("options." + key);
      }
      return;
    }

    if (current_table.size() == 2 && current_table[0] == "types") {
      auto& state = ensure_type(current_table[1]);
      if (key == "c_type") {
        if (state.c_type_set) {
          fatal("manifest: duplicate types." + current_table[1] + ".c_type");
        }
        state.type.c_type = detail::expect_string(val, "types." + current_table[1] + ".c_type");
        state.c_type_set = true;
      } else if (key == "fields") {
        if (state.fields_set) {
          fatal("manifest: duplicate types." + current_table[1] + ".fields");
        }
        state.type.fields = detail::expect_string_array(val, "types." + current_table[1] + ".fields");
        state.fields_set = true;
      } else if (key == "bitfields") {
        if (state.bitfields_set) {
          fatal("manifest: duplicate types." + current_table[1] + ".bitfields");
        }
        state.type.bitfields =
            detail::expect_string_array(val, "types." + current_table[1] + ".bitfields");
        state.bitfields_set = true;
      } else {
        remember_unknown("types." + current_table[1] + "." + key);
      }
      return;
    }

    if (current_table.size() == 3 && current_table[0] == "types" &&
        current_table[2] == "options") {
      auto& state = ensure_type(current_table[1]);
      if (key == "bitfield_policy") {
        if (state.bitfield_policy_set) {
          fatal("manifest: duplicate types." + current_table[1] + ".options.bitfield_policy");
        }
        state.type.options.bitfield_policy =
            detail::expect_string(val, "types." + current_table[1] + ".options.bitfield_policy");
        state.bitfield_policy_set = true;
      } else {
        remember_unknown("types." + current_table[1] + ".options." + key);
      }
      return;
    }

    if (current_table.size() == 2 && current_table[0] == "enums") {
      auto& state = ensure_enum(current_table[1]);
      if (key == "c_type") {
        if (state.c_type_set) {
          fatal("manifest: duplicate enums." + current_table[1] + ".c_type");
        }
        state.en.c_type = detail::expect_string(val, "enums." + current_table[1] + ".c_type");
        state.c_type_set = true;
      } else if (key == "values") {
        if (state.values_set) {
          fatal("manifest: duplicate enums." + current_table[1] + ".values");
        }
        state.en.values = detail::expect_string_array(val, "enums." + current_table[1] + ".values");
        state.values_set = true;
      } else if (key == "extract_all") {
        if (state.extract_all_set) {
          fatal("manifest: duplicate enums." + current_table[1] + ".extract_all");
        }
        state.en.extract_all = detail::expect_bool(val, "enums." + current_table[1] + ".extract_all");
        state.extract_all_set = true;
      } else {
        remember_unknown("enums." + current_table[1] + "." + key);
      }
      return;
    }

    if (current_table.size() == 1 && current_table[0] == "constants") {
      auto& c = ensure_constant(key);
      if (!c.expr.empty() || c.c_type) {
        fatal("manifest: duplicate constants." + key);
      }
      auto table = detail::expect_inline_table(val, "constants." + key);
      auto expr_it = table.find("expr");
      if (expr_it == table.end()) {
        fatal("manifest: constants." + key + " missing required expr");
      }
      c.expr = detail::expect_string(expr_it->second, "constants." + key + ".expr");
      auto type_it = table.find("type");
      if (type_it != table.end()) {
        c.c_type = detail::expect_string(type_it->second, "constants." + key + ".type");
      }
      for (const auto& [k, _] : table) {
        if (k != "expr" && k != "type") {
          remember_unknown("constants." + key + "." + k);
        }
      }
      return;
    }

    remember_unknown(key);
  };

  while (!cur.eof()) {
    cur.skip_ws_and_comments();
    if (cur.eof()) break;

    if (cur.peek() == '[') {
      cur.get();
      cur.skip_ws_and_comments();
      current_table = detail::parse_dotted_key(cur);
      cur.skip_ws_and_comments();
      if (cur.get() != ']') fatal("expected closing ']' at " + cur.location());
      detail::consume_trailing(cur);
      continue;
    }

    std::string key = detail::parse_bare_key(cur);
    cur.skip_ws_and_comments();
    if (cur.get() != '=') fatal("expected '=' after key at " + cur.location());
    cur.skip_ws_and_comments();

    const auto value = detail::parse_value(cur);
    set_value(key, value);
    detail::consume_trailing(cur);
  }

  // -------------------------------------------------------------------------
  // Validation (§6)
  // -------------------------------------------------------------------------

  if (manifest.set.empty()) {
    fatal("manifest: missing required 'set'");
  }
  require_safe_set_name(manifest.set);

  if (manifest.schema_version.empty()) {
    fatal("manifest: missing required 'schema_version'");
  }
  if (manifest.schema_version != kManifestSchemaVersionString) {
    fatal("manifest: unsupported manifest schema_version '" + manifest.schema_version +
              "' (expected '" + std::string(kManifestSchemaVersionString) + "')",
          "update manifest to schema_version = \"" + std::string(kManifestSchemaVersionString) + "\"");
  }

  if (manifest.includes.empty()) {
    fatal("manifest: missing required 'includes' list",
          "add at least one kernel header to includes = [...] ");
  }
  for (const auto& inc : manifest.includes) {
    require_utf8("manifest.includes[]", inc);
    if (inc.empty()) {
      fatal("manifest: empty string in includes list");
    }
  }

  if (!manifest.target_arch.empty()) {
    for (const auto& arch : manifest.target_arch) {
      if (arch != "x86_64" && arch != "aarch64") {
        fatal("manifest: unsupported target.arch value '" + arch + "'",
              "supported values: \"x86_64\", \"aarch64\"");
      }
    }
  }

  auto validate_policy = [](std::string_view policy, std::string_view where) {
    if (policy != "geometry" && policy != "deny" && policy != "accessor_shim") {
      fatal("manifest: bitfield_policy must be 'geometry', 'deny', or 'accessor_shim' (" +
            std::string(where) + ")");
    }
  };

  validate_policy(manifest.options.bitfield_policy, "options");

  manifest.types.reserve(type_order.size());
  for (const auto& name : type_order) {
    auto it = types.find(name);
    if (it == types.end()) continue;
    const auto& type = it->second.type;

    if (type.c_type.empty()) {
      fatal("manifest: type '" + name + "' missing required 'c_type'",
            "add c_type = \"struct " + name + "\" or appropriate C type spelling");
    }
    require_utf8("manifest.types." + name + ".c_type", type.c_type);

    if (type.options.bitfield_policy) {
      validate_policy(*type.options.bitfield_policy, "types." + name + ".options");
    }

    std::map<std::string, int, std::less<>> seen_fields;
    for (const auto& field : type.fields) {
      require_utf8("manifest.types." + name + ".fields[]", field);
      if (field.empty()) {
        fatal("manifest: empty field name in type '" + name + "'");
      }
      if (seen_fields.contains(field)) {
        fatal("manifest: duplicate field '" + field + "' in type '" + name + "'");
      }
      seen_fields[field] = 1;
      validate_member_designator(field, manifest.options, false);
    }

    if (!type.bitfields.empty()) {
      const auto policy = effective_bitfield_policy(manifest, type);
      if (policy == "deny") {
        fatal("manifest: type '" + name + "' requests bitfields but bitfield_policy is 'deny'",
              "set bitfield_policy = \"geometry\" in [options] or in [types." + name +
                  ".options] to enable bitfield geometry");
      }
      std::map<std::string, int, std::less<>> seen_bitfields;
      for (const auto& bf : type.bitfields) {
        require_utf8("manifest.types." + name + ".bitfields[]", bf);
        if (bf.empty()) {
          fatal("manifest: empty bitfield name in type '" + name + "'");
        }
        if (seen_bitfields.contains(bf)) {
          fatal("manifest: duplicate bitfield '" + bf + "' in type '" + name + "'");
        }
        seen_bitfields[bf] = 1;
        validate_member_designator(bf, manifest.options, true);
      }
    }

    manifest.types.push_back(type);
  }

  manifest.enums.reserve(enum_order.size());
  for (const auto& name : enum_order) {
    auto it = enums.find(name);
    if (it == enums.end()) continue;
    const auto& en = it->second.en;
    if (en.c_type.empty()) {
      fatal("manifest: enum '" + name + "' missing required 'c_type'");
    }
    require_utf8("manifest.enums." + name + ".c_type", en.c_type);
    if (en.extract_all && !en.values.empty()) {
      fatal("manifest: enum '" + name + "' cannot set extract_all with explicit values");
    }
    if (!en.extract_all && en.values.empty()) {
      fatal("manifest: enum '" + name + "' must set values[] or extract_all = true");
    }
    std::map<std::string, int, std::less<>> seen_vals;
    for (const auto& v : en.values) {
      require_utf8("manifest.enums." + name + ".values[]", v);
      if (v.empty()) {
        fatal("manifest: empty enum value in enum '" + name + "'");
      }
      if (seen_vals.contains(v)) {
        fatal("manifest: duplicate enum value '" + v + "' in enum '" + name + "'");
      }
      seen_vals[v] = 1;
    }
    manifest.enums.push_back(en);
  }

  manifest.constants.reserve(constant_order.size());
  for (const auto& name : constant_order) {
    auto it = constants.find(name);
    if (it == constants.end()) continue;
    const auto& c = it->second;
    require_utf8("manifest.constants." + name + ".expr", c.expr);
    if (c.expr.empty()) {
      fatal("manifest: constants." + name + " has empty expr");
    }
    if (c.c_type) {
      require_utf8("manifest.constants." + name + ".type", *c.c_type);
    }
    manifest.constants.push_back(c);
  }

  if (manifest.options.strict && !unknown_keys.empty()) {
    std::ostringstream oss;
    oss << "manifest: unknown keys:\n";
    for (const auto& k : unknown_keys) {
      oss << "  - " << k << "\n";
    }
    fatal(oss.str());
  }

  return manifest;
}

// Parse manifest from file
inline Manifest parse_manifest_file(const std::filesystem::path& path) {
  std::string content = read_text(path);
  Manifest m = parse_manifest(content);

  // Verify filename matches set name
  std::string stem = path.stem().string();
  if (stem != m.set) {
    fatal("manifest: set must match filename stem (expected '" + stem + "', got '" + m.set + "')",
          "rename file to '" + m.set + ".toml' or change set = \"" + stem + "\"");
  }

  return m;
}

// -----------------------------------------------------------------------------
// Bitfield Accessor ID Computation (§8.4.1, legacy)
// -----------------------------------------------------------------------------

enum class AccessorKind { Get, Set };

inline std::uint64_t compute_accessor_id(const Manifest& m, const std::string& c_type,
                                         const std::string& bitfield_name, AccessorKind kind,
                                         int alt = 0) {
  // Validate no NUL bytes in inputs
  auto require_no_nul = [](std::string_view what, std::string_view s) {
    if (s.find('\0') != std::string_view::npos) {
      fatal("invalid NUL in " + std::string(what));
    }
  };
  require_no_nul("set", m.set);
  require_no_nul("c_type", c_type);
  require_no_nul("bitfield_name", bitfield_name);

  // Build the hash input per §8.4.1
  std::string bytes;
  bytes.reserve(128 + m.set.size() + c_type.size() + bitfield_name.size());

  auto append_z = [&](std::string_view s) {
    bytes.append(s);
    bytes.push_back('\0');
  };

  append_z("IRO-BF-ID");
  append_z(std::to_string(kLayoutSchemaMajor));
  append_z(std::to_string(kLayoutSchemaMinor));
  append_z(m.set);
  append_z(c_type);
  append_z(bitfield_name);
  append_z(kind == AccessorKind::Get ? "get" : "set");

  // Collision handling suffix (§8.4.2)
  if (alt > 0) {
    append_z("ALT");
    append_z(std::to_string(alt));
  }

  auto h256 = sha256_string(bytes);
  return hash256_to_hash64(h256);
}

// Compute accessor ID with automatic collision resolution
// Returns pair of (id, alt_number_used)
template <typename SeenMap>
inline std::pair<std::uint64_t, int> compute_accessor_id_with_collision_check(
    const Manifest& m, const std::string& c_type, const std::string& bitfield_name,
    AccessorKind kind, const SeenMap& seen_ids) {
  constexpr int kMaxCollisionRetries = 16;

  for (int alt = 0; alt <= kMaxCollisionRetries; ++alt) {
    std::uint64_t id = compute_accessor_id(m, c_type, bitfield_name, kind, alt);
    if (seen_ids.find(id) == seen_ids.end()) {
      return {id, alt};
    }
  }

  fatal("bitfield accessor ID collision for " + c_type + "::" + bitfield_name + " after " +
            std::to_string(kMaxCollisionRetries) + " retries",
        "this indicates a hash collision; try renaming the bitfield");
}

}  // namespace iro

#endif  // IRO_MANIFEST_HPP_
