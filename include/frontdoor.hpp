#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
namespace fd {
using i64 = std::int64_t;
struct Query {
  int n = 0;
  std::vector<int> u, v, marks;
  std::vector<i64> weights;
};
struct Answer {
  bool feasible = false;
  bool complete = true;
  std::string status = "complete";
  std::uint64_t states = 0, fd_checks = 0, oracle_calls = 0, pruned_branches = 0;
  i64 value = 0;
  std::vector<int> selected;
  int cores_before = 0, terms_before = 0, cores_after = 0, terms_after = 0;
  int numeric_nodes = 0;
  std::uint64_t numeric_arcs = 0, logical_arcs = 0,
                materialized_hard_arcs = 0, cut_rounds = 0;
  bool implicit_cut = false;
  i64 heavy_terms = 0;
  double compile_s = 0, prune_s = 0, build_s = 0, solve_s = 0, recover_s = 0, total_s = 0;
};
Query read(std::istream&);
// Read a topology-only FDGBIN01 graph. X and Y are singleton endpoints,
// I is empty, and every other vertex is an allowed candidate with equal cost.
Query read_binary_full_domain(const std::string& path, int x, int y, i64 cost);
void write(std::ostream&, const Query&);
void validate(const Query&);
// Empty means the raw graph fits the signed 32-bit in-memory front end.
// A nonempty string explains the first static size limit that is exceeded.
std::string input_capacity_error(std::uint64_t nodes, std::uint64_t edges);
Answer solve(const Query&, const std::string& method);
Answer enumerate(const Query&, const std::string& method,
                 std::uint64_t max_states = 0, std::uint64_t timeout_ms = 0);
Query generate(int variables, int width, unsigned seed, const std::string& family);
bool valid_fd(const Query&, const std::vector<int>& selected);
void print(std::ostream&, const Answer&, const std::string& method, bool witness = true);
}
