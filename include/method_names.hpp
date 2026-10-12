#pragma once

#include <cctype>
#include <stdexcept>
#include <string>

namespace fd {

struct CliMethod {
  std::string internal;
  std::string label;
};

// CLI naming only: the solver implementations and their internal IDs stay fixed.
// Only paper names are public; internal IDs are not accepted as aliases.
inline CliMethod parse_cli_method(const std::string& name) {
  struct Names { const char* paper; const char* internal; };
  static const Names names[] = {
    {"FDCut", "component"}, {"Brute", "enum"}, {"Prune", "enum-prune"},
    {"ListJ", "jenum"}, {"ListW", "wenum"}, {"PruneW", "wbb"},
    {"base", "base"}, {"sweep", "sweep"}
  };
  auto lowercase = [](std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
  };
  const auto key = lowercase(name);
  for (const auto& entry : names) {
    if (key == lowercase(entry.paper)) return {entry.internal, entry.paper};
  }
  throw std::runtime_error("unknown method: " + name +
    "; expected FDCut, Brute, Prune, ListJ, ListW, PruneW (advanced: base, sweep)");
}

} // namespace fd
