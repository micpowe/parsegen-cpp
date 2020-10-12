#include "parsegen_finite_automaton.hpp"

#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <utility>

#include "parsegen_chartab.hpp"

namespace parsegen {

finite_automaton::finite_automaton(
    int nsymbols_init, bool is_deterministic_init, int nstates_reserve)
    : table(nsymbols_init + (is_deterministic_init ? 0 : 2), nstates_reserve),
      is_deterministic(is_deterministic_init) {
  reserve(accepted_tokens, nstates_reserve);
}

int get_nstates(finite_automaton const& fa) { return get_nrows(fa.table); }

int get_nsymbols(finite_automaton const& fa) {
  return get_ncols(fa.table) - (fa.is_deterministic ? 0 : 2);
}

bool get_determinism(finite_automaton const& fa) { return fa.is_deterministic; }

int get_epsilon0(finite_automaton const& fa) {
  assert(!fa.is_deterministic);
  return get_ncols(fa.table) - 2;
}

int get_epsilon1(finite_automaton const& fa) {
  assert(!fa.is_deterministic);
  return get_ncols(fa.table) - 1;
}

int add_state(finite_automaton& fa) {
  auto state = get_nstates(fa);
  resize(fa.table, state + 1, get_ncols(fa.table));
  for (int j = 0; j < get_ncols(fa.table); ++j) {
    at(fa.table, state, j) = -1;
  }
  fa.accepted_tokens.push_back(-1);
  return state;
}

void add_transition(
    finite_automaton& fa, int from_state, int at_symbol, int to_state) {
  assert(0 <= to_state);
  assert(to_state < get_nstates(fa));
  assert(0 <= at_symbol);
  assert(
      at_symbol < get_ncols(fa.table));  // allow setting epsilon transitions
  assert(at(fa.table, from_state, at_symbol) == -1);
  at(fa.table, from_state, at_symbol) = to_state;
}

void add_accept(finite_automaton& fa, int state, int token) {
  assert(0 <= token);
  at(fa.accepted_tokens, state) = token;
}

void remove_accept(finite_automaton& fa, int state) {
  at(fa.accepted_tokens, state) = -1;
}

int step(finite_automaton const& fa, int state, int symbol) {
  assert(0 <= state);
  assert(state < get_nstates(fa));
  assert(0 <= symbol);
  assert(
      symbol < get_ncols(fa.table));  // allow getting epsilon transitions
  return at(fa.table, state, symbol);
}

int accepts(finite_automaton const& fa, int state) {
  return at(fa.accepted_tokens, state);
}

int get_nsymbols_eps(finite_automaton const& fa) { return get_ncols(fa.table); }

void append_states(finite_automaton& fa, finite_automaton const& other) {
  assert(get_nsymbols(other) == get_nsymbols(fa));
  auto other_determ = get_determinism(other);
  if (!other_determ) assert(!fa.is_deterministic);
  auto offset = get_nstates(fa);
  for (int other_state = 0; other_state < get_nstates(other); ++other_state) {
    auto my_state = add_state(fa);
    auto token = accepts(other, other_state);
    if (0 <= token) add_accept(fa, my_state, token);
  }
  for (int other_state = 0; other_state < get_nstates(other); ++other_state) {
    auto my_state = other_state + offset;
    for (int symbol = 0; symbol < get_nsymbols_eps(other); ++symbol) {
      auto other_next = step(other, other_state, symbol);
      if (other_next < 0) continue;
      auto my_next = other_next + offset;
      add_transition(fa, my_state, symbol, my_next);
    }
  }
}

finite_automaton finite_automaton::make_single_nfa(
    int nsymbols, int symbol, int token) {
  return finite_automaton::make_range_nfa(nsymbols, symbol, symbol, token);
}

finite_automaton finite_automaton::make_set_nfa(
    int nsymbols, std::set<int> const& accepted, int token) {
  finite_automaton out(nsymbols, true, 2);
  auto start_state = add_state(out);
  auto accept_state = add_state(out);
  for (auto i : accepted) {
    add_transition(out, start_state, i, accept_state);
  }
  add_accept(out, accept_state, token);
  return out;
}

finite_automaton finite_automaton::make_range_nfa(
    int nsymbols, int range_start, int range_end, int token) {
  assert(0 <= range_start);
  assert(range_start <= range_end);
  assert(range_end <= nsymbols);
  finite_automaton out(nsymbols, true, 2);
  auto start_state = add_state(out);
  auto accept_state = add_state(out);
  for (int i = range_start; i <= range_end; ++i) {
    add_transition(out, start_state, i, accept_state);
  }
  add_accept(out, accept_state, token);
  return out;
}

finite_automaton finite_automaton::unite(
    finite_automaton const& a, finite_automaton const& b) {
  auto nsymbols = get_nsymbols(a);
  finite_automaton out(nsymbols, false, 1 + get_nstates(a) + get_nstates(b));
  auto start_state = add_state(out);
  auto a_offset = get_nstates(out);
  append_states(out, a);
  auto b_offset = get_nstates(out);
  append_states(out, b);
  auto epsilon0 = get_epsilon0(out);
  auto epsilon1 = get_epsilon1(out);
  add_transition(out, start_state, epsilon0, a_offset);
  add_transition(out, start_state, epsilon1, b_offset);
  return out;
}

finite_automaton finite_automaton::concat(
    finite_automaton const& a, finite_automaton const& b, int token) {
  auto nsymbols = get_nsymbols(a);
  finite_automaton out(nsymbols, false, get_nstates(a) + get_nstates(b));
  append_states(out, a);
  auto b_offset = get_nstates(out);
  append_states(out, b);
  auto epsilon0 = get_epsilon0(out);
  for (int i = 0; i < get_nstates(a); ++i) {
    if (accepts(a, i) != -1) {
      add_transition(out, i, epsilon0, b_offset);
      remove_accept(out, i);
    }
  }
  for (int i = 0; i < get_nstates(b); ++i) {
    if (accepts(b, i) != -1) {
      add_accept(out, i + b_offset, token);
    }
  }
  return out;
}

finite_automaton finite_automaton::plus(finite_automaton const& a, int token) {
  finite_automaton out(get_nsymbols(a), false, get_nstates(a) + 1);
  append_states(out, a);
  auto new_accept_state = add_state(out);
  add_accept(out, new_accept_state, token);
  auto epsilon0 = get_epsilon0(out);
  auto epsilon1 = get_epsilon1(out);
  for (int i = 0; i < get_nstates(a); ++i) {
    if (accepts(a, i) != -1) {
      add_transition(out, i, epsilon0, new_accept_state);
      /* we follow a convention that accepting
         states should not have epsilon transitions */
      add_transition(out, i, epsilon1, 0);
      remove_accept(out, i);
    }
  }
  return out;
}

finite_automaton finite_automaton::maybe(finite_automaton const& a, int token) {
  finite_automaton out(get_nsymbols(a), false, get_nstates(a) + 2);
  auto new_start_state = add_state(out);
  auto offset = get_nstates(out);
  append_states(out, a);
  auto new_accept_state = add_state(out);
  auto epsilon0 = get_epsilon0(out);
  auto epsilon1 = get_epsilon1(out);
  add_transition(out, new_start_state, epsilon1, offset);
  /* form an epsilon0 linked list of new start state,
     all old accepting states, and new accepting state */
  auto last = new_start_state;
  for (int i = 0; i < get_nstates(a); ++i) {
    if (accepts(a, i) != -1) {
      add_transition(out, last, epsilon0, i + offset);
      remove_accept(out, i + offset);
      last = i + offset;
    }
  }
  add_transition(out, last, epsilon0, new_accept_state);
  add_accept(out, new_accept_state, token);
  return out;
}

finite_automaton finite_automaton::star(finite_automaton const& a, int token) {
  return maybe(plus(a, token), token);
}

using StateSet = std::set<int>;

static StateSet step(
    StateSet const& ss, int symbol, finite_automaton const& fa) {
  StateSet next_ss;
  for (auto state : ss) {
    auto next_state = step(fa, state, symbol);
    if (next_state != -1) next_ss.insert(next_state);
  }
  return next_ss;
}

using StateQueue = std::queue<int>;

static StateSet get_epsilon_closure(StateSet ss, finite_automaton const& fa) {
  StateQueue q;
  for (auto state : ss) q.push(state);
  auto epsilon0 = get_epsilon0(fa);
  auto epsilon1 = get_epsilon1(fa);
  while (!q.empty()) {
    auto state = q.front();
    q.pop();
    for (auto epsilon = epsilon0; epsilon <= epsilon1; ++epsilon) {
      auto next_state = step(fa, state, epsilon);
      if (next_state == -1) continue;
      if (!ss.count(next_state)) {
        ss.insert(next_state);
        q.push(next_state);
      }
    }
  }
  return ss;
}

using StateSetPtr = StateSet*;

struct state_set_ptr_compare {
  bool operator()(StateSetPtr const& a, StateSetPtr const& b) const {
    return *a < *b;
  }
};

using StateSetPtr2State = std::map<StateSetPtr, int, state_set_ptr_compare>;
using StateSetUniqPtrVector = std::vector<std::unique_ptr<StateSet>>;

static void emplace_back(StateSetUniqPtrVector& ssupv, StateSet& ss) {
  ssupv.push_back(std::unique_ptr<StateSet>(new StateSet(std::move(ss))));
}

/* powerset construction, NFA -> DFA */
finite_automaton finite_automaton::make_deterministic(
    finite_automaton const& nfa) {
  if (get_determinism(nfa)) return nfa;
  StateSetPtr2State ssp2s;
  StateSetUniqPtrVector ssupv;
  finite_automaton out(get_nsymbols(nfa), true, 0);
  StateSet start_ss;
  start_ss.insert(0);
  start_ss = get_epsilon_closure(start_ss, nfa);
  emplace_back(ssupv, start_ss);
  ssp2s[ssupv.back().get()] = add_state(out);
  int front = 0;
  while (front < int(ssupv.size())) {
    auto state = front;
    auto& ss = *at(ssupv, front);
    ++front;
    for (int symbol = 0; symbol < get_nsymbols(nfa); ++symbol) {
      auto next_ss = parsegen::step(ss, symbol, nfa);
      if (next_ss.empty()) continue;
      next_ss = get_epsilon_closure(next_ss, nfa);
      int next_state;
      auto it = ssp2s.find(&next_ss);
      if (it == ssp2s.end()) {
        next_state = add_state(out);
        emplace_back(ssupv, next_ss);
        ssp2s[ssupv.back().get()] = next_state;
      } else {
        next_state = it->second;
      }
      add_transition(out, state, symbol, next_state);
    }
    int min_accepted = -1;
    for (auto nfa_state : ss) {
      auto nfa_token = accepts(nfa, nfa_state);
      if (nfa_token == -1) continue;
      if (min_accepted == -1 || nfa_token < min_accepted) {
        min_accepted = nfa_token;
      }
    }
    if (min_accepted != -1) add_accept(out, state, min_accepted);
  }
  return out;
}

struct state_row_compare {
  Table<int> const& table;
  std::vector<int> const& accepted;
  bool operator()(int const& a, int const& b) const {
    auto aa = at(accepted, a);
    auto ab = at(accepted, b);
    if (aa != ab) return aa < ab;
    for (int symbol = 0, ncols = get_ncols(table); symbol < ncols; ++symbol) {
      auto ea = at(table, a, symbol);
      auto eb = at(table, b, symbol);
      if (ea != eb) return ea < eb;
    }
    return false;
  }
};

using StateRow2SimpleState = std::map<int, int, state_row_compare>;

finite_automaton finite_automaton::simplify_once(finite_automaton const& fa) {
  StateRow2SimpleState sr2ss({fa.table, fa.accepted_tokens});
  int nsimple = 0;
  for (int state = 0; state < get_nstates(fa); ++state) {
    auto res = sr2ss.insert(std::make_pair(state, nsimple));
    if (res.second) {
      ++nsimple;
    }
  }
  finite_automaton out(get_nsymbols(fa), get_determinism(fa), nsimple);
  for (int simple = 0; simple < nsimple; ++simple) {
    add_state(out);
  }
  std::vector<bool> did_simple(size_t(nsimple), false);
  for (int state = 0; state < get_nstates(fa); ++state) {
    assert(sr2ss.count(state));
    auto simple = sr2ss[state];
    if (at(did_simple, simple)) continue;
    for (int symbol = 0; symbol < get_nsymbols_eps(fa); ++symbol) {
      auto next_state = step(fa, state, symbol);
      if (next_state == -1) continue;
      assert(sr2ss.count(next_state));
      auto next_simple = sr2ss[next_state];
      add_transition(out, simple, symbol, next_simple);
    }
    auto token = accepts(fa, state);
    if (token != -1) {
      add_accept(out, simple, token);
    }
    at(did_simple, simple) = true;
  }
  return out;
}

finite_automaton finite_automaton::simplify(finite_automaton const& fa) {
  finite_automaton out = fa;
  int nstates_new = get_nstates(fa);
  int nstates;
  int i = 0;
  do {
    nstates = nstates_new;
    out = finite_automaton::simplify_once(out);
    ++i;
    nstates_new = get_nstates(out);
  } while (nstates_new < nstates);
  if (i > 2) std::cerr << "simplify() actually took multiple steps!\n";
  return out;
}

finite_automaton make_char_nfa(bool is_deterministic_init, int nstates_reserve) {
  return finite_automaton(
      parsegen::NCHARS, is_deterministic_init, nstates_reserve);
}

void add_char_transition(
    finite_automaton& fa, int from_state, char at_char, int to_state) {
  add_transition(fa, from_state, get_symbol(at_char), to_state);
}

template <typename T, bool is_signed = std::numeric_limits<T>::is_signed>
struct is_symbol_helper;

template <typename T>
struct is_symbol_helper<T, true> {
  static bool eval(T c) {
    if (c < 0) return false;
    return 0 <= parsegen::chartab[int(c)];
  }
};

template <typename T>
struct is_symbol_helper<T, false> {
  static bool eval(T c) {
    if (c >= OMEGA_H_CHARTAB_SIZE) return false;
    return 0 <= parsegen::chartab[int(c)];
  }
};

bool is_symbol(char c) { return is_symbol_helper<char>::eval(c); }

template <typename T, bool is_signed = std::numeric_limits<T>::is_signed>
struct get_symbol_helper;

template <typename T>
struct get_symbol_helper<T, true> {
  static int eval(T c) {
    assert(0 <= c);
    int symbol = parsegen::chartab[int(c)];
    assert(0 <= symbol);
    return symbol;
  }
};

template <typename T>
struct get_symbol_helper<T, false> {
  static int eval(T c) {
    int symbol = parsegen::chartab[int(c)];
    assert(0 <= symbol);
    return symbol;
  }
};

int get_symbol(char c) { return get_symbol_helper<char>::eval(c); }

char get_char(int symbol) {
  assert(0 <= symbol);
  assert(symbol < parsegen::NCHARS);
  return inv_chartab[symbol];
}

finite_automaton make_char_set_nfa(std::set<char> const& accepted, int token) {
  std::set<int> symbol_set;
  for (auto c : accepted) symbol_set.insert(get_symbol(c));
  return finite_automaton::make_set_nfa(parsegen::NCHARS, symbol_set, token);
}

finite_automaton make_char_range_nfa(
    char range_start, char range_end, int token) {
  return finite_automaton::make_range_nfa(
      parsegen::NCHARS, get_symbol(range_start), get_symbol(range_end), token);
}

finite_automaton make_char_single_nfa(char symbol_char, int token) {
  return finite_automaton::make_range_nfa(
      parsegen::NCHARS, get_symbol(symbol_char), get_symbol(symbol_char), token);
}

std::set<char> negate_set(std::set<char> const& s) {
  std::set<char> out;
  for (int symbol = 0; symbol < NCHARS; ++symbol) {
    auto c = inv_chartab[symbol];
    if (!s.count(c)) out.insert(c);
  }
  return out;
}

std::ostream& operator<<(std::ostream& os, finite_automaton const& fa) {
  if (get_determinism(fa))
    os << "dfa ";
  else
    os << "nfa ";
  os << get_nstates(fa) << " states " << get_nsymbols(fa) << " symbols\n";
  for (int state = 0; state < get_nstates(fa); ++state) {
    for (int symbol = 0; symbol < get_nsymbols(fa); ++symbol) {
      auto next_state = step(fa, state, symbol);
      if (next_state != -1)
        os << "(" << state << ", " << symbol << ") -> " << next_state << '\n';
    }
    if (!get_determinism(fa)) {
      for (int symbol = get_epsilon0(fa); symbol <= get_epsilon1(fa);
           ++symbol) {
        auto next_state = step(fa, state, symbol);
        if (next_state != -1)
          os << "(" << state << ", eps" << (symbol - get_epsilon0(fa))
             << ") -> " << next_state << '\n';
      }
    }
    auto token = accepts(fa, state);
    if (token != -1) os << state << " accepts " << token << '\n';
  }
  return os;
}

}  // end namespace parsegen
