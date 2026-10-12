#include "frontdoor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fd {
namespace {
using Clock = std::chrono::steady_clock;
using Bits = std::vector<unsigned char>;

// Independent original-DAG implementations of the constrained constructors
// used by the enumeration baselines. They deliberately do not call the FDCut
// compiler, any reduction kernel, or HPF.
class Constructors {
 public:
  explicit Constructors(const Query& query)
      : q(query), children(q.n), parents(q.n), x(q.n), y(q.n) {
    std::vector<int> indegree(q.n), ready;
    for (std::size_t e = 0; e < q.u.size(); ++e) {
      children[q.u[e]].push_back(q.v[e]);
      parents[q.v[e]].push_back(q.u[e]);
      ++indegree[q.v[e]];
    }
    for (int v = 0; v < q.n; ++v) if (!indegree[v]) ready.push_back(v);
    for (std::size_t at = 0; at < ready.size(); ++at)
      for (int w : children[ready[at]]) if (!--indegree[w]) ready.push_back(w);
    if (ready.size() != static_cast<std::size_t>(q.n))
      throw std::runtime_error("input must be a DAG");
    for (int v = 0; v < q.n; ++v) {
      x[v] = static_cast<unsigned char>(bool(q.marks[v] & 1));
      y[v] = static_cast<unsigned char>(bool(q.marks[v] & 2));
    }
  }

  bool wienoebst(const Bits& required, const Bits& allowed,
                 std::vector<int>& answer) const {
    // Wienoebst, van der Zander, and Liskiewicz (AAAI 2024), Algorithms 1--2.
    const Bits reached = backdoor_reached_from_x();
    Bits zi(q.n), forbidden(q.n), visited_inc(q.n), visited_out(q.n),
        continue_later(q.n);
    for (int v = 0; v < q.n; ++v) {
      zi[v] = static_cast<unsigned char>(allowed[v] && !reached[v]);
      forbidden[v] = static_cast<unsigned char>(!zi[v]);
    }

    // Iterative form of the paper's recursive forbidden-vertex traversal.
    std::vector<std::pair<int, unsigned char>> stack;
    for (int v = 0; v < q.n; ++v) if (y[v]) stack.emplace_back(v, 1);
    while (!stack.empty()) {
      const auto state = stack.back();
      stack.pop_back();
      const int v = state.first;
      const bool arrived_out = bool(state.second);
      auto& visited = arrived_out ? visited_out[v] : visited_inc[v];
      if (visited) continue;
      visited = 1;
      forbidden[v] = 1;
      if (!x[v]) {
        for (int w : children[v])
          if (!visited_inc[w]) stack.emplace_back(w, 0);
        if (arrived_out) {
          for (int w : parents[v]) {
            if (visited_out[w]) continue;
            if (forbidden[w]) stack.emplace_back(w, 1);
            else continue_later[w] = 1;
          }
        }
      }
      if (continue_later[v] && !visited_out[v]) stack.emplace_back(v, 1);
    }

    Bits z(q.n);
    for (int v = 0; v < q.n; ++v) {
      z[v] = static_cast<unsigned char>(!forbidden[v]);
      if (required[v] && !z[v]) return false;
    }
    if (!intercepts(z)) return false;
    answer = members(z);
    return true;
  }

  bool jeong(const Bits& required, const Bits& allowed,
             std::vector<int>& answer) const {
    // Jeong, Tian, and Bareinboim (NeurIPS 2022), FindFDSet. The structure
    // follows the authors' MIT-licensed reference implementation at commit
    // d16dcf5b8354cb333e7f8aeda393e2aabec6141c. Moral graphs are rebuilt
    // instead of incrementally remoralized, preserving the algorithm's
    // decisions while keeping this port compact and independent.
    const Bits reached = backdoor_reached_from_x();
    Bits r1 = allowed;
    for (int v = 0; v < q.n; ++v) if (allowed[v]) {
      if (reached[v]) {
        if (required[v]) return false;
        r1[v] = 0;
      }
    }

    Bits r2 = r1;
    for (int v = 0; v < q.n; ++v) if (r1[v] && !jeong_get_dep(v, r1)) {
      if (required[v]) return false;
      r2[v] = 0;
    }
    if (!intercepts(r2)) return false;
    answer = members(r2);
    return true;
  }

  // Section 3.1: restrict the search domain, never the original causal DAG.
  // Required nodes outside An(Y) must remain; an FD2-invalid required node
  // makes the entire query infeasible (it must not be silently discarded).
  bool candidates(Bits& phi) const {
    Bits ancestors = y;
    std::vector<int> pending;
    for (int v = 0; v < q.n; ++v) if (y[v]) pending.push_back(v);
    while (!pending.empty()) {
      const int v = pending.back(); pending.pop_back();
      for (int p : parents[v]) if (!ancestors[p]) {
        ancestors[p] = 1; pending.push_back(p);
      }
    }
    const Bits reached = backdoor_reached_from_x();
    phi.assign(q.n, 0);
    for (int v = 0; v < q.n; ++v) {
      const bool required = bool(q.marks[v] & 4);
      if (required && reached[v]) return false;
      phi[v] = (q.marks[v] & 8) && (required || ancestors[v]) && !reached[v];
    }
    return true;
  }

 private:
  const Query& q;
  std::vector<std::vector<int>> children, parents;
  Bits x, y;

  static std::vector<int> members(const Bits& bits) {
    std::vector<int> result;
    for (int v = 0; v < static_cast<int>(bits.size()); ++v)
      if (bits[v]) result.push_back(v);
    return result;
  }

  Bits backdoor_reached_from_x() const {
    // Bayes-Ball with an empty conditioning set in G_X, where outgoing edges
    // of X are deleted. State 0 arrives through an incoming edge and state 1
    // through an outgoing edge, matching Algorithm 5 of the AAAI paper.
    Bits visited_inc(q.n), visited_out(q.n), reached(q.n);
    std::vector<std::pair<int, unsigned char>> stack;
    for (int v = 0; v < q.n; ++v) if (x[v]) stack.emplace_back(v, 1);
    while (!stack.empty()) {
      const auto state = stack.back();
      stack.pop_back();
      const int v = state.first;
      const bool arrived_out = bool(state.second);
      auto& visited = arrived_out ? visited_out[v] : visited_inc[v];
      if (visited) continue;
      visited = 1;
      reached[v] = 1;
      if (!x[v])
        for (int w : children[v]) if (!visited_inc[w]) stack.emplace_back(w, 0);
      if (arrived_out)
        for (int w : parents[v])
          if (!x[w] && !visited_out[w]) stack.emplace_back(w, 1);
    }
    return reached;
  }

  bool intercepts(const Bits& z) const {
    Bits seen = x;
    std::vector<int> stack;
    for (int v = 0; v < q.n; ++v) if (x[v]) stack.push_back(v);
    while (!stack.empty()) {
      const int v = stack.back();
      stack.pop_back();
      if (y[v]) return false;
      for (int w : children[v]) {
        if (z[w] || seen[w]) continue;
        seen[w] = 1;
        stack.push_back(w);
      }
    }
    return true;
  }

  Bits ancestors_of(int target) const {
    Bits active(q.n);
    std::vector<int> stack;
    for (int v = 0; v < q.n; ++v) if (x[v] || y[v] || v == target) {
      active[v] = 1;
      stack.push_back(v);
    }
    while (!stack.empty()) {
      const int v = stack.back();
      stack.pop_back();
      for (int p : parents[v]) if (!active[p]) {
        active[p] = 1;
        stack.push_back(p);
      }
    }
    return active;
  }

  std::vector<std::vector<int>> moralize(const Bits& active,
                                         const Bits& cut) const {
    std::vector<std::vector<int>> moral(q.n);
    auto link = [&](int a, int b) {
      if (a == b || x[a] || x[b]) return;
      moral[a].push_back(b);
      moral[b].push_back(a);
    };
    for (int v = 0; v < q.n; ++v) if (active[v]) {
      std::vector<int> live_parents;
      for (int p : parents[v]) if (active[p] && !cut[p]) {
        live_parents.push_back(p);
        link(p, v);
      }
      for (std::size_t i = 0; i < live_parents.size(); ++i)
        for (std::size_t j = i + 1; j < live_parents.size(); ++j)
          link(live_parents[i], live_parents[j]);
    }
    for (auto& adjacent : moral) {
      std::sort(adjacent.begin(), adjacent.end());
      adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
    }
    return moral;
  }

  bool jeong_get_dep(int target, const Bits& r1) const {
    const Bits active = ancestors_of(target);
    Bits cut(q.n), zprime(q.n), visited(q.n);
    cut[target] = 1;
    visited[target] = 1;
    auto moral = moralize(active, cut);
    std::vector<int> pending{target};
    while (!pending.empty()) {
      const int u = pending.back();
      pending.pop_back();
      if (y[u]) return false;

      Bits nr(q.n);
      for (int v : moral[u]) if (r1[v] && !visited[v]) nr[v] = 1;
      Bits next_cut = cut;
      for (int v = 0; v < q.n; ++v) if (zprime[v] || nr[v]) next_cut[v] = 1;
      auto next_moral = moralize(active, next_cut);

      Bits next(q.n);
      for (int v : next_moral[u]) if (!visited[v]) next[v] = 1;
      for (int v = 0; v < q.n; ++v)
        if (nr[v] && !parents[v].empty() && !visited[v]) next[v] = 1;
      for (int v = 0; v < q.n; ++v) if (nr[v]) zprime[v] = 1;
      for (int v = 0; v < q.n; ++v) if (next[v] && !visited[v]) {
        visited[v] = 1;
        pending.push_back(v);
      }
      cut.swap(next_cut);
      moral.swap(next_moral);
    }
    return true;
  }
};

bool expired(Clock::time_point start, std::uint64_t timeout_ms) {
  return timeout_ms &&
      std::chrono::duration<double, std::milli>(Clock::now() - start).count() >=
          static_cast<double>(timeout_ms);
}

void interrupt(Answer& answer, const char* status) {
  answer.complete = false;
  answer.status = status;
}

void accept(Answer& answer, const Query& q, const std::vector<int>& selected) {
  i64 value = 0;
  for (int v : selected) value += q.weights[v];
  if (!answer.feasible || value < answer.value) {
    answer.feasible = true;
    answer.value = value;
    answer.selected = selected;
  }
}

Answer direct_enumeration(const Query& q, const Constructors& constructors,
                          const Bits& phi, bool prune, std::uint64_t max_states,
                          std::uint64_t timeout_ms, Clock::time_point start) {
  std::vector<int> optional, selected;
  Bits chosen(q.n);
  i64 cost = 0;
  for (int v = 0; v < q.n; ++v) {
    if (q.marks[v] & 4) {
      selected.push_back(v);
      chosen[v] = 1;
      cost += q.weights[v];
    } else if (phi[v]) {
      optional.push_back(v);
    }
  }

  Answer answer;
  struct Frame { std::size_t depth; int phase; bool included; };
  std::vector<Frame> stack{{0, 0, false}};
  auto pop = [&]() {
    if (stack.back().included) {
      const int v = optional[stack.back().depth - 1];
      cost -= q.weights[v];
      chosen[v] = 0;
      selected.pop_back();
    }
    stack.pop_back();
  };
  while (!stack.empty()) {
    if (expired(start, timeout_ms)) { interrupt(answer, "timeout"); break; }
    auto& frame = stack.back();
    if (frame.phase == 0) {
      if (max_states && answer.states >= max_states) {
        interrupt(answer, "state_limit");
        break;
      }
      ++answer.states;
      if (prune && answer.feasible && cost >= answer.value) {
        ++answer.pruned_branches;
        pop();
        continue;
      }
      if (frame.depth == optional.size()) {
        ++answer.fd_checks;
        ++answer.oracle_calls;
        std::vector<int> witness;
        if (constructors.wienoebst(chosen, chosen, witness))
          accept(answer, q, selected);
        pop();
        continue;
      }
      frame.phase = 1;
      stack.push_back({frame.depth + 1, 0, false});
    } else if (frame.phase == 1) {
      const int v = optional[frame.depth];
      const std::size_t depth = frame.depth + 1;
      frame.phase = 2;
      selected.push_back(v);
      chosen[v] = 1;
      cost += q.weights[v];
      stack.push_back({depth, 0, true});
    } else {
      pop();
    }
  }
  return answer;
}

Answer constrained_enumeration(const Query& q, const Constructors& constructors,
                               const Bits& phi,
                               const std::string& method,
                               std::uint64_t max_states,
                               std::uint64_t timeout_ms,
                               Clock::time_point start) {
  const bool jeong = method == "jenum";
  const bool branch_and_bound = method == "wbb";
  Bits required(q.n), allowed(q.n);
  std::vector<int> optional;
  i64 required_cost = 0;
  for (int v = 0; v < q.n; ++v) {
    required[v] = static_cast<unsigned char>(bool(q.marks[v] & 4));
    allowed[v] = phi[v];
    if (required[v]) required_cost += q.weights[v];
    else if (allowed[v]) optional.push_back(v);
  }

  Answer answer;
  if (branch_and_bound) {
    Bits original_allowed(q.n);
    for (int v = 0; v < q.n; ++v) original_allowed[v] = bool(q.marks[v] & 8);
    std::vector<int> witness;
    ++answer.oracle_calls;
    if (!constructors.wienoebst(required, original_allowed, witness)) return answer;
    accept(answer, q, witness);
  }
  enum Change : unsigned char { root, excluded, included };
  struct Frame { std::size_t depth; int phase; Change change; };
  std::vector<Frame> stack{{0, 0, root}};
  auto pop = [&]() {
    const Frame frame = stack.back();
    if (frame.change != root) {
      const int v = optional[frame.depth - 1];
      if (frame.change == excluded) allowed[v] = 1;
      else {
        required[v] = 0;
        required_cost -= q.weights[v];
      }
    }
    stack.pop_back();
  };

  while (!stack.empty()) {
    if (expired(start, timeout_ms)) { interrupt(answer, "timeout"); break; }
    auto& frame = stack.back();
    if (frame.phase == 0) {
      if (max_states && answer.states >= max_states) {
        interrupt(answer, "state_limit");
        break;
      }
      ++answer.states;
      if (branch_and_bound && answer.feasible && required_cost >= answer.value) {
        ++answer.pruned_branches;
        pop();
        continue;
      }

      ++answer.oracle_calls;
      std::vector<int> witness;
      const bool feasible = jeong ? constructors.jeong(required, allowed, witness)
                                  : constructors.wienoebst(required, allowed, witness);
      if (!feasible) {
        ++answer.pruned_branches;
        pop();
        continue;
      }
      if (frame.depth == optional.size()) {
        ++answer.fd_checks;
        accept(answer, q, witness);
        pop();
        continue;
      }

      const int v = optional[frame.depth];
      const std::size_t depth = frame.depth + 1;
      frame.phase = 1;
      if (branch_and_bound) {
        required[v] = 1;
        required_cost += q.weights[v];
        stack.push_back({depth, 0, included});
      } else {
        allowed[v] = 0;
        stack.push_back({depth, 0, excluded});
      }
    } else if (frame.phase == 1) {
      const int v = optional[frame.depth];
      const std::size_t depth = frame.depth + 1;
      frame.phase = 2;
      if (branch_and_bound) {
        allowed[v] = 0;
        stack.push_back({depth, 0, excluded});
      } else {
        required[v] = 1;
        required_cost += q.weights[v];
        stack.push_back({depth, 0, included});
      }
    } else {
      pop();
    }
  }
  return answer;
}
}  // namespace

Answer enumerate(const Query& q, const std::string& method,
                 std::uint64_t max_states, std::uint64_t timeout_ms) {
  const auto start = Clock::now();
  validate(q);
  if (method != "enum" && method != "enum-prune" && method != "jenum" &&
      method != "wenum" && method != "wbb")
    throw std::runtime_error("unknown enumeration method");
  Constructors constructors(q);
  Bits phi;
  Answer answer;
  if (constructors.candidates(phi)) {
    answer = method == "enum" || method == "enum-prune"
      ? direct_enumeration(q, constructors, phi, method == "enum-prune",
                           max_states, timeout_ms, start)
      : constrained_enumeration(q, constructors, phi, method, max_states,
                                timeout_ms, start);
  }
  answer.solve_s = answer.total_s =
      std::chrono::duration<double>(Clock::now() - start).count();
  return answer;
}
}  // namespace fd
