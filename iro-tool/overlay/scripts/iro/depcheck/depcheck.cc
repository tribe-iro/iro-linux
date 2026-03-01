// SPDX-License-Identifier: GPL-2.0-only
// IRO depcheck: ensures C++ TUs do not include forbidden kernel headers.
// Conforms to IRO-TOOL-SPEC-4.2 §15.

#include "../iro_common.hpp"

#include <algorithm>
#include <sstream>

namespace iro::depcheck {

using namespace iro;

// -----------------------------------------------------------------------------
// Default Allow/Deny Prefixes (§15.1)
// -----------------------------------------------------------------------------

const std::vector<std::string> kDefaultAllowPrefixes = {
    "include/generated/iro/",
    "iro/include/",
};

const std::vector<std::string> kDefaultDenyPrefixes = {
    "include/linux/",
    "arch/",
    "include/asm",
    "include/generated/asm",
};

// -----------------------------------------------------------------------------
// CLI Arguments
// -----------------------------------------------------------------------------

struct Args {
  std::vector<fs::path> depfiles;
  std::string srctree;
  std::string objtree;
  std::vector<std::string> allow_prefixes;
  std::vector<std::string> deny_prefixes;
  bool verbose{false};
};

Args parse_args(int argc, char** argv) {
  Args args;

  for (int i = 1; i < argc; ++i) {
    std::string_view a(argv[i]);
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) fatal("missing value for " + std::string(a));
      return argv[++i];
    };

    if (a == "--depfile") {
      args.depfiles.emplace_back(next());
    } else if (a == "--srctree") {
      args.srctree = next();
    } else if (a == "--objtree") {
      args.objtree = next();
    } else if (a == "--allow") {
      args.allow_prefixes.push_back(next());
    } else if (a == "--deny") {
      args.deny_prefixes.push_back(next());
    } else if (a == "--verbose" || a == "-v") {
      args.verbose = true;
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage: depcheck --depfile F [--depfile F2] --srctree S --objtree O\n";
      std::cout << "                [--allow PREFIX] [--deny PREFIX] [-v|--verbose]\n";
      std::cout << "\nVersion: " << kDepcheckVersion << "\n";
      std::cout << "\nDefault allow prefixes:\n";
      for (const auto& p : kDefaultAllowPrefixes) {
        std::cout << "  " << p << "\n";
      }
      std::cout << "\nDefault deny prefixes:\n";
      for (const auto& p : kDefaultDenyPrefixes) {
        std::cout << "  " << p << "\n";
      }
      std::exit(0);
    } else {
      fatal("unknown argument: " + std::string(a));
    }
  }

  if (args.depfiles.empty()) {
    fatal("at least one --depfile required");
  }

  // Apply defaults if no custom allow/deny specified
  if (args.allow_prefixes.empty()) {
    args.allow_prefixes = kDefaultAllowPrefixes;
  }
  if (args.deny_prefixes.empty()) {
    args.deny_prefixes = kDefaultDenyPrefixes;
  }

  return args;
}

// -----------------------------------------------------------------------------
// Depfile Parsing
// -----------------------------------------------------------------------------

std::vector<std::string> parse_dep_tokens(const std::string& text) {
  // Concatenate lines with trailing backslash
  std::string joined;
  joined.reserve(text.size());
  std::istringstream is(text);
  std::string line;

  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\\') {
      line.pop_back();
      joined += line;
      continue;
    }
    joined += line;
    joined.push_back('\n');
  }

  // Tokenize with Make-style escaping
  std::vector<std::string> tokens;
  std::string current;

  for (std::size_t i = 0; i < joined.size(); ++i) {
    const char c = joined[i];
    if (c == '\\' && i + 1 < joined.size()) {
      // Backslash escapes next byte
      current.push_back(joined[++i]);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(c);
  }

  if (!current.empty()) {
    tokens.push_back(current);
  }

  return tokens;
}

// -----------------------------------------------------------------------------
// Path Matching
// -----------------------------------------------------------------------------

bool matches_prefix(const std::string& path, const std::vector<std::string>& prefixes) {
  for (const auto& prefix : prefixes) {
    if (path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0) {
      return true;
    }
  }
  return false;
}

// -----------------------------------------------------------------------------
// Depfile Checking
// -----------------------------------------------------------------------------

struct CheckResult {
  std::vector<std::string> forbidden_includes;
  std::vector<std::string> checked_paths;
};

CheckResult check_depfile(const fs::path& depfile_path, const Args& args) {
  CheckResult result;

  // Read with size limit
  const auto txt = read_text_with_limit(depfile_path, kMaxDepfileBytes);

  auto tokens = parse_dep_tokens(txt);
  if (tokens.empty()) {
    return result;
  }

  // Skip rule header: find ":" or token ending with ":"
  auto it = tokens.begin();
  for (; it != tokens.end(); ++it) {
    if (*it == ":" || it->ends_with(':')) break;
  }
  if (it == tokens.end()) {
    fatal("malformed depfile (missing ':'): " + depfile_path.string(),
          "check that the compiler generated a valid depfile");
  }
  ++it;  // Skip the ':' token
  tokens.erase(tokens.begin(), it);

  // Check each prerequisite path
  for (auto& tok : tokens) {
    if (tok == ":") continue;  // Ignore stray colons

    // Strip srctree prefix if present
    if (!args.srctree.empty() && tok.compare(0, args.srctree.size(), args.srctree) == 0) {
      tok = tok.substr(args.srctree.size());
      if (!tok.empty() && tok[0] == '/') {
        tok = tok.substr(1);
      }
    }

    // Strip objtree prefix if present
    if (!args.objtree.empty() && tok.compare(0, args.objtree.size(), args.objtree) == 0) {
      tok = tok.substr(args.objtree.size());
      if (!tok.empty() && tok[0] == '/') {
        tok = tok.substr(1);
      }
    }

    // Normalize path
    auto norm = normalize_path(tok);
    if (norm.empty()) continue;

    // Validate path is valid UTF-8
    if (!is_valid_utf8(norm)) {
      fatal("depfile path is not valid UTF-8: " + tok);
    }

    result.checked_paths.push_back(norm);

    // Check if allowed first (takes precedence)
    if (matches_prefix(norm, args.allow_prefixes)) {
      continue;
    }

    // Check if denied
    if (matches_prefix(norm, args.deny_prefixes)) {
      result.forbidden_includes.push_back(norm);
    }
  }

  return result;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int real_main(int argc, char** argv) {
  const auto args = parse_args(argc, argv);

  std::vector<std::string> all_violations;

  for (const auto& depfile : args.depfiles) {
    const auto result = check_depfile(depfile, args);

    if (args.verbose) {
      std::cerr << "depcheck: checked " << result.checked_paths.size() << " paths in "
                << depfile.string() << "\n";
    }

    for (const auto& forbidden : result.forbidden_includes) {
      all_violations.push_back(depfile.string() + ": " + forbidden);
    }
  }

  if (!all_violations.empty()) {
    std::ostringstream msg;
    msg << "forbidden include(s) detected:\n";
    for (const auto& v : all_violations) {
      msg << "  " << v << "\n";
    }
    fatal(msg.str(),
          "IRO C++ code must not include raw kernel headers; use iro/include/ shadow headers");
  }

  return 0;
}

}  // namespace iro::depcheck

int main(int argc, char** argv) {
  try {
    return iro::depcheck::real_main(argc, argv);
  } catch (const iro::ToolError& e) {
    std::cerr << "depcheck: " << e.what() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "depcheck: unexpected error: " << e.what() << "\n";
  }
  return 1;
}
