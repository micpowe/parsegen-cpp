#include "parsegen_regex.hpp"

#include <iostream>
#include <sstream>
#include <cctype>

#include "parsegen_build_parser.hpp"
#include "parsegen_chartab.hpp"
#include "parsegen_parser.hpp"
#include "parsegen_set.hpp"
#include "parsegen_std_vector.hpp"
#include "parsegen_string.hpp"
#include "parsegen_finite_automaton.hpp"
#include "parsegen_error.hpp"

namespace parsegen {
namespace regex {

language build_language() {
  /* The top produtions were from the "grep.y" YACC grammar in the source
     code for Plan 9's grep utility, see here:
https://github.com/wangeguo/plan9/blob/master/sys/src/cmd/grep/grep.y
     The "set" related productions
     are from a grammar intended to be used by ProLog to parse Perl's regular
     expressions, see here:
http://www.cs.sfu.ca/~cameron/Teaching/384/99-3/regexp-plg.html */
  language out;
  auto& prods = out.productions;
  prods.resize(NPRODS);
  prods[PROD_REGEX] = {"regex", {"union"}};
  prods[PROD_UNION_DECAY] = {"union", {"concat"}};
  prods[PROD_UNION] = {"union", {"union", "|", "concat"}};  // union
  prods[PROD_CONCAT_DECAY] = {"concat", {"qualified"}};
  prods[PROD_CONCAT] = {"concat", {"concat", "qualified"}};  // concatenation
  prods[PROD_QUAL_DECAY] = {"qualified", {"single"}};
  prods[PROD_STAR] = {"qualified", {"qualified", "*"}};
  prods[PROD_PLUS] = {"qualified", {"qualified", "+"}};
  prods[PROD_MAYBE] = {"qualified", {"qualified", "?"}};
  prods[PROD_SINGLE_CHAR] = {"single", {"char"}};
  prods[PROD_ANY] = {"single", {"."}};  // any
  prods[PROD_SINGLE_SET] = {"single", {"set"}};
  prods[PROD_PARENS_UNION] = {"single", {"(", "union", ")"}};
  prods[PROD_SET_POSITIVE] = {"set", {"positive-set"}};
  prods[PROD_SET_NEGATIVE] = {"set", {"negative-set"}};
  prods[PROD_POSITIVE_SET] = {"positive-set", {"[", "set-items", "]"}};
  prods[PROD_NEGATIVE_SET] = {"negative-set", {"[", "^", "set-items", "]"}};
  prods[PROD_SET_ITEMS_DECAY] = {"set-items", {"set-item"}};
  prods[PROD_SET_ITEMS_ADD] = {"set-items", {"set-items", "set-item"}};
  prods[PROD_SET_ITEM_CHAR] = {"set-item", {"char"}};
  prods[PROD_SET_ITEM_RANGE] = {"set-item", {"range"}};
  prods[PROD_RANGE] = {"range", {"char", "-", "char"}};
  out.tokens.resize(NTOKS);
  /* either one of the non-meta characters, or anything preceded by the escape
   * slash */
  out.tokens[TOK_CHAR] = {
      "char", "[^\\\\\\.\\[\\]\\(\\)\\|\\-\\^\\*\\+\\?]|\\\\."};
  out.tokens[TOK_DOT] = {".", "\\."};
  out.tokens[TOK_LRANGE] = {"[", "\\]"};
  out.tokens[TOK_RRANGE] = {"]", "\\]"};
  out.tokens[TOK_LPAREN] = {"(", "\\("};
  out.tokens[TOK_RPAREN] = {")", "\\)"};
  out.tokens[TOK_UNION] = {"|", "\\|"};
  out.tokens[TOK_RANGE] = {"-", "\\-"};
  out.tokens[TOK_NEGATE] = {"^", "\\^"};
  out.tokens[TOK_STAR] = {"*", "\\*"};
  out.tokens[TOK_PLUS] = {"+", "\\+"};
  out.tokens[TOK_MAYBE] = {"?", "\\?"};
  return out;
}

/* bootstrap ! This lexer is used to build the parser_tables that read
   regular expressions themselves, so it can't depend on that parser ! */
finite_automaton build_lexer() {
  std::string meta_chars_str = ".[]()|-^*+?";
  std::set<int> all_chars;
  for (int i = 0; i < NCHARS; ++i) all_chars.insert(i);
  auto nonmeta_chars = all_chars;
  for (auto meta_char : meta_chars_str) {
    auto it = nonmeta_chars.find(get_symbol(meta_char));
    nonmeta_chars.erase(it);
  }
  auto lex_nonmeta =
      finite_automaton::make_set_nfa(NCHARS, nonmeta_chars, TOK_CHAR);
  auto lex_slash = make_char_single_nfa('\\');
  auto lex_any = finite_automaton::make_set_nfa(NCHARS, all_chars);
  auto lex_escaped = finite_automaton::concat(lex_slash, lex_any, TOK_CHAR);
  auto lex_char = finite_automaton::unite(lex_nonmeta, lex_escaped);
  finite_automaton lex_metachars;
  for (int i = 0; i < isize(meta_chars_str); ++i) {
    int token = TOK_CHAR + i + 1;
    auto lex_metachar = make_char_single_nfa(at(meta_chars_str, i), token);
    if (i)
      lex_metachars = finite_automaton::unite(lex_metachars, lex_metachar);
    else
      lex_metachars = lex_metachar;
  }
  auto out = finite_automaton::unite(lex_char, lex_metachars);
  return finite_automaton::simplify(finite_automaton::make_deterministic(out));
}

parser_tables_ptr ask_parser_tables() {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
  static parser_tables_ptr ptr;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  if (ptr.use_count() == 0) {
    auto lang = regex::ask_language();
    auto grammar = build_grammar(*lang);
    auto parser = accept_parser(build_lalr1_parser(grammar));
    auto lexer = regex::build_lexer();
    indentation indent_info;
    indent_info.is_sensitive = false;
    indent_info.indent_token = -1;
    indent_info.dedent_token = -1;
    ptr.reset(new parser_tables{parser, lexer, indent_info});
  }
  return ptr;
}

language_ptr ask_language() {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
  static language_ptr ptr;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  if (ptr.use_count() == 0) {
    ptr.reset(new language(build_language()));
  }
  return ptr;
}

finite_automaton build_dfa(
    std::string const& name, std::string const& regex, int token) {
  auto parser = regex::parser(token);
  try {
    return std::any_cast<finite_automaton>(parser.parse_string(regex, name));
  } catch (const parse_error& e) {
    std::stringstream ss;
    ss << e.what() << '\n';
    ss << "error: couldn't build DFA for token \"" << name << "\" regex \""
       << regex << "\"\n";
    ss << "repeating with debug_parser:\n";
    debug_parser debug_parser(regex::ask_parser_tables(), ss);
    debug_parser.parse_string(regex, name);
    throw parse_error(ss.str());
  }
}

regex::parser::parser(int result_token_in)
    : parsegen::parser(regex::ask_parser_tables()),
      result_token(result_token_in) {}

std::any regex::parser::shift(int token, std::string& text) {
  if (token != TOK_CHAR) {
    return std::any();
  }
  if (size(text) == 1) {
    return std::any(text[0]);
  } else if (size(text) == 2) {
    assert(text[0] == '\\');
    return std::any(text[1]);
  } else {
    std::cerr << "BUG: regex char text is \"" << text << "\"\n";
    abort();
  }
}

std::any regex::parser::reduce(int production, std::vector<std::any>& rhs) {
  switch (production) {
    case PROD_REGEX:
      return finite_automaton::simplify(finite_automaton::make_deterministic(
          std::any_cast<finite_automaton&&>(std::move(at(rhs, 0)))));
    case PROD_UNION_DECAY:
    case PROD_CONCAT_DECAY:
    case PROD_QUAL_DECAY:
    case PROD_SET_ITEMS_DECAY:
    case PROD_SET_ITEM_RANGE:
      return at(rhs, 0);
    case PROD_UNION:
      return finite_automaton::unite(std::any_cast<finite_automaton&&>(std::move(at(rhs, 0))),
          std::any_cast<finite_automaton&&>(std::move(at(rhs, 2))));
    case PROD_CONCAT: {
      auto& a_any = at(rhs, 0);
      auto& b_any = at(rhs, 1);
      auto a = std::any_cast<finite_automaton&&>(std::move(a_any));
      auto b = std::any_cast<finite_automaton&&>(std::move(b_any));
      return finite_automaton::concat(a, b, result_token);
    }
    case PROD_STAR:
      return finite_automaton::star(
          std::any_cast<finite_automaton&&>(std::move(at(rhs, 0))), result_token);
    case PROD_PLUS:
      return finite_automaton::plus(
          std::any_cast<finite_automaton&&>(std::move(at(rhs, 0))), result_token);
    case PROD_MAYBE:
      return finite_automaton::maybe(
          std::any_cast<finite_automaton&&>(std::move(at(rhs, 0))), result_token);
    case PROD_SINGLE_CHAR:
      return make_char_single_nfa(std::any_cast<char>(at(rhs, 0)), result_token);
    case PROD_ANY:
      return finite_automaton::make_range_nfa(
          NCHARS, 0, NCHARS - 1, result_token);
    case PROD_SINGLE_SET:
      return make_char_set_nfa(
          std::any_cast<std::set<char>&&>(std::move(at(rhs, 0))), result_token);
    case PROD_PARENS_UNION:
      return at(rhs, 1);
    case PROD_SET_POSITIVE:
      return at(rhs, 0);
    case PROD_SET_NEGATIVE:
      return negate_set(std::any_cast<std::set<char>&&>(std::move(at(rhs, 0))));
    case PROD_POSITIVE_SET:
      return at(rhs, 1);
    case PROD_NEGATIVE_SET:
      return at(rhs, 2);
    case PROD_SET_ITEMS_ADD:
      return unite(std::any_cast<std::set<char>&&>(std::move(at(rhs, 0))),
          std::any_cast<std::set<char>&&>(std::move(at(rhs, 1))));
    case PROD_SET_ITEM_CHAR:
      return std::set<char>({std::any_cast<char>(at(rhs, 0))});
    case PROD_RANGE: {
      std::set<char> set;
      for (char c = std::any_cast<char>(at(rhs, 0));
           c <= std::any_cast<char>(at(rhs, 2)); ++c) {
        set.insert(c);
      }
      return std::any(std::move(set));
    }
  }
  std::cerr << "BUG: unexpected production " << production << '\n';
  abort();
}

bool has_range(std::set<char> const& s, char first, char last)
{
  for (char c = first; c <= last; ++c) {
    if (s.count(c) == 0) {
      return false;
    }
  }
  return true;
}

void remove_range(std::set<char>& s, char first, char last)
{
  for (char c = first; c <= last; ++c) {
    s.erase(c);
  }
}

bool matches(std::string const& r, std::string const& t)
{
  return accepts(build_dfa("first arg of matches", r, 0), t, 0);
}

std::string internal_from_charset(std::set<char> s)
{
  std::string result;
  if (has_range(s, 'a', 'z')) {
    remove_range(s, 'a', 'z');
    result += "a-z";
  }
  if (has_range(s, 'A', 'Z')) {
    remove_range(s, 'A', 'Z');
    result += "A-Z";
  }
  if (has_range(s, '0', '9')) {
    remove_range(s, '0', '9');
    result += "0-9";
  }
  std::string const specials(".[]()|-^*+?");
  for (char const c : s) {
    if (specials.find(c) != std::string::npos) {
      result += '\\';
    }
    result += c;
  }
  return result;
}

std::string from_charset(std::set<char> const& s)
{
  if (s.empty()) return "\b";
  if (s.size() == 1) return std::string(1, *(s.begin()));
  std::string const positive_contents =
    internal_from_charset(s);
  std::string const negative_contents =
    internal_from_charset(negate_set(s));
  if (positive_contents.size() <= negative_contents.size()) {
    return std::string("[") + positive_contents + "]";
  } else {
    return std::string("[^") + negative_contents + "]";
  }
}

class regex_in_progress {
 public:
  virtual ~regex_in_progress() = default;
  virtual std::string print() const = 0;
  virtual std::unique_ptr<regex_in_progress> copy() const = 0;
  virtual bool operator==(regex_in_progress const&) const = 0;
  virtual bool needs_parentheses() const = 0;
};

std::unique_ptr<regex_in_progress> concat(
    std::unique_ptr<regex_in_progress> const& a,
    std::unique_ptr<regex_in_progress> const& b);
std::unique_ptr<regex_in_progress> either(
    std::unique_ptr<regex_in_progress> const& a,
    std::unique_ptr<regex_in_progress> const& b);

class regex_null : public regex_in_progress {
 public:
  virtual std::string print() const override
  {
    return "NULL";
  }
  virtual std::unique_ptr<regex_in_progress> copy() const override
  {
    return std::make_unique<regex_null>();
  }
  virtual bool operator==(regex_in_progress const& other) const override
  {
    return typeid(other) == typeid(regex_null);
  }
  virtual bool needs_parentheses() const override
  {
    return false;
  }
};

class regex_epsilon : public regex_in_progress {
 public:
  virtual std::string print() const override
  {
    return "epsilon";
  }
  virtual std::unique_ptr<regex_in_progress> copy() const override
  {
    return std::make_unique<regex_epsilon>();
  }
  virtual bool operator==(regex_in_progress const& other) const override
  {
    return typeid(other) == typeid(regex_epsilon const);
  }
  virtual bool needs_parentheses() const override
  {
    return true;
  }
};

class regex_charset : public regex_in_progress {
  std::set<char> characters;
 public:
  regex_charset() = default;
  explicit regex_charset(char c)
  {
    characters.insert(c);
  }
  explicit regex_charset(std::set<char> const& s)
  {
    characters = s;
  }
  virtual std::string print() const override
  {
    return from_charset(characters);
  }
  virtual std::unique_ptr<regex_in_progress> copy() const override
  {
    return std::make_unique<regex_charset>(*this);
  }
  std::unique_ptr<regex_charset> either_with(regex_charset const& other) const
  {
    auto result = std::make_unique<regex_charset>();
    for (auto c : characters) {
      result->characters.insert(c);
    }
    for (auto c : other.characters) {
      result->characters.insert(c);
    }
    return result;
  }
  virtual bool operator==(regex_in_progress const& other) const override
  {
    if (typeid(other) != typeid(regex_charset)) return false;
    return dynamic_cast<regex_charset const&>(other).characters == characters;
  }
  virtual bool needs_parentheses() const override
  {
    return false;
  }
};

class regex_either : public regex_in_progress {
  std::vector<std::unique_ptr<regex_in_progress>> subexpressions;
 public:
  virtual std::string print() const override
  {
    bool had_non_epsilon = false;
    bool has_epsilon = false;
    std::string result;
    for (auto& se_ptr : subexpressions) {
      auto& se = *se_ptr;
      if (typeid(se) == typeid(regex_epsilon)) {
        has_epsilon = true;
      } else {
        if (had_non_epsilon) result += "|";
        result += se.print();
        had_non_epsilon = true;
      }
    }
    if (has_epsilon) {
      if (internal_needs_parentheses()) {
        result = std::string("(") + result + ")";
      }
      result += "?";
    }
    return result;
  }
  virtual std::unique_ptr<regex_in_progress> copy() const override
  {
    auto result = std::make_unique<regex_either>();
    for (auto& ptr : subexpressions) {
      result->subexpressions.push_back(ptr->copy());
    }
    return result;
  }
  void insert(regex_in_progress const& other)
  {
    for (auto& existing : subexpressions) {
      if (*existing == other) return;
    }
    subexpressions.push_back(other.copy());
  }
  void insert(std::unique_ptr<regex_in_progress> const& other)
  {
    for (auto& existing : subexpressions) {
      if (*existing == *other) return;
    }
    subexpressions.push_back(other->copy());
  }
  [[nodiscard]] std::unique_ptr<regex_in_progress> either_with(std::unique_ptr<regex_in_progress> const& other) const
  {
    auto& other_ref = *other;
    if (typeid(other) == typeid(regex_either)) {
      auto& other_either = dynamic_cast<regex_either const&>(other_ref);
      std::unique_ptr<regex_in_progress> result = copy();
      for (auto& other_se : other_either.subexpressions) {
        result = either(result, other_se);
      }
      return result;
    }
    auto result = std::make_unique<regex_either>();
    bool did_combine = false;
    for (auto& se : subexpressions) {
      auto combined_ptr = either(se, other);
      auto& combined_ref = *combined_ptr;
      if (typeid(combined_ref) != typeid(regex_either)) {
        result->insert(combined_ptr);
        did_combine = true;
      } else {
        result->insert(se);
      }
    }
    if (!did_combine) {
      result->insert(other);
    }
    return result;
  }
  virtual bool operator==(regex_in_progress const& other) const override
  {
    if (typeid(other) != typeid(regex_either)) return false;
    auto& other_either = dynamic_cast<regex_either const&>(other);
    if (subexpressions.size() != other_either.subexpressions.size()) return false;
    for (auto& a : other_either.subexpressions) {
      bool found = false;
      for (auto& b : subexpressions) {
        if (*a == *b) found = true;
      }
      if (!found) return false;
    }
    return true;
  }
  bool internal_needs_parentheses() const
  {
    std::size_t non_epsilon_count = 0;
    bool single_needs_parens = false;
    for (auto& se_ptr : subexpressions) {
      auto& se = *se_ptr;
      if (typeid(se) != typeid(regex_epsilon)) {
        single_needs_parens = se.needs_parentheses();
        ++non_epsilon_count;
      }
    }
    return (non_epsilon_count > 1) || single_needs_parens;
  }
  virtual bool needs_parentheses() const override
  {
    for (auto& se_ptr : subexpressions) {
      auto& se = *se_ptr;
      if (typeid(se) == typeid(regex_epsilon)) {
        return false;
      }
    }
    return internal_needs_parentheses();
  }
  bool has_epsilon() const
  {
    for (auto& se_ptr : subexpressions) {
      auto& se = *se_ptr;
      if (typeid(se) == typeid(regex_epsilon)) {
        return true;
      }
    }
    return false;
  }
  bool is_question() const {
    if (subexpressions.size() != 2) return false;
    return has_epsilon();
  }
  std::unique_ptr<regex_in_progress> only_subexpression() const
  {
    for (auto& se_ptr : subexpressions) {
      auto& se = *se_ptr;
      if (typeid(se) != typeid(regex_epsilon)) {
        return se.copy();
      }
    }
    return nullptr;
  }
  std::unique_ptr<regex_in_progress> remove_epsilon() const
  {
    if (subexpressions.size() == 2) return only_subexpression();
    auto result = std::make_unique<regex_either>();
    for (auto& se_ptr : subexpressions) {
      auto& se = *se_ptr;
      if (typeid(se) != typeid(regex_epsilon)) {
        result->insert(se);
      }
    }
    return result;
  }
};

class regex_concat : public regex_in_progress {
  std::vector<std::unique_ptr<regex_in_progress>> subexpressions;
 public:
  virtual std::string print() const override
  {
    std::string result;
    for (std::size_t i = 0; i < subexpressions.size(); ++i) {
      if (subexpressions[i]->needs_parentheses()) {
        result += std::string("(") + subexpressions[i]->print() + ")";
      } else {
        result += subexpressions[i]->print();
      }
    }
    return result;
  }
  virtual std::unique_ptr<regex_in_progress> copy() const override
  {
    auto result = std::make_unique<regex_concat>();
    for (auto& ptr : subexpressions) {
      result->subexpressions.push_back(ptr->copy());
    }
    return result;
  }
  virtual bool operator==(regex_in_progress const& other) const override
  {
    if (typeid(other) != typeid(regex_concat)) return false;
    auto& other_concat = dynamic_cast<regex_concat const&>(other);
    if (subexpressions.size() != other_concat.subexpressions.size()) return false;
    for (std::size_t i = 0; i < subexpressions.size(); ++i) {
      if (!(*subexpressions[i] == *other_concat.subexpressions[i])) return false;
    }
    return true;
  }
  void add(regex_in_progress const& other) {
    if (typeid(other) == typeid(regex_concat)) {
      for (auto& se : dynamic_cast<regex_concat const&>(other).subexpressions) {
        subexpressions.push_back(se->copy());
      }
    } else {
      subexpressions.push_back(other.copy());
    }
  }
  std::size_t get_common_prefix_size(regex_concat const& other) const
  {
    std::size_t const max_size = other.subexpressions.size();
    std::size_t size;
    for (size = 0; size < max_size; ++size) {
      if (!(*(subexpressions[size]) == *(other.subexpressions[size]))) break;
    }
    return size;
  }
  std::size_t get_common_suffix_size(regex_concat const& other) const
  {
    std::size_t const max_size = other.subexpressions.size();
    std::size_t size;
    for (size = 0; size < max_size; ++size) {
      if (!(*(subexpressions[subexpressions.size() - 1 - size]) ==
            *(other.subexpressions[other.subexpressions.size() - 1 - size]))) {
        break;
      }
    }
    return size;
  }
  bool starts_with(regex_in_progress const& other) const
  {
    return *(subexpressions[0]) == other;
  }
  bool ends_with(regex_in_progress const& other) const
  {
    return *(subexpressions[subexpressions.size() - 1]) == other;
  }
  std::unique_ptr<regex_in_progress> get_first_n(std::size_t n) const
  {
    if (n == 0) {
      return std::make_unique<regex_epsilon>();
    } else if (n == 1) {
      return subexpressions[0]->copy();
    } else {
      auto prefix_concat = std::make_unique<regex_concat>();
      for (std::size_t i = 0; i < n; ++i) {
        prefix_concat->add(*(subexpressions[i]));
      }
      return prefix_concat;
    }
  }
  std::unique_ptr<regex_in_progress> get_last_n(std::size_t n) const
  {
    if (n == 0) {
      return std::make_unique<regex_epsilon>();
    } else if (n == 1) {
      return subexpressions[subexpressions.size() - 1]->copy();
    } else {
      auto suffix_concat = std::make_unique<regex_concat>();
      std::size_t const offset = subexpressions.size() - n;
      for (std::size_t i = 0; i < n; ++i) {
        suffix_concat->add(*(subexpressions[offset + i]));
      }
      return suffix_concat;
    }
  }
  std::unique_ptr<regex_in_progress> either_with(std::unique_ptr<regex_in_progress> const& other_ptr) const {
    auto& other = *other_ptr;
    if (typeid(other) == typeid(regex_concat)) {
      auto& other_concat = dynamic_cast<regex_concat const&>(other);
      if (other_concat.subexpressions.size() > subexpressions.size()) {
        return other_concat.either_with(this->copy());
      }
      std::size_t const common_suffix_size = get_common_suffix_size(other_concat);
      if (common_suffix_size > 0) {
        auto my_prefix = get_first_n(subexpressions.size() - common_suffix_size);
        auto other_prefix = other_concat.get_first_n(other_concat.subexpressions.size() - common_suffix_size);
        auto suffix = get_last_n(common_suffix_size);
        return concat(either(my_prefix, other_prefix), suffix);
      }
      std::size_t const common_prefix_size = get_common_prefix_size(other_concat);
      if (common_prefix_size > 0) {
        auto my_suffix = get_last_n(subexpressions.size() - common_prefix_size);
        auto other_suffix = other_concat.get_last_n(other_concat.subexpressions.size() - common_prefix_size);
        auto prefix = get_first_n(common_prefix_size);
        return concat(prefix, either(my_suffix, other_suffix));
      } else {
        return nullptr;
      }
    } else {
      if (ends_with(other)) {
        std::size_t const prefix_size = subexpressions.size() - 1;
        return concat(either(this->get_first_n(prefix_size), std::make_unique<regex_epsilon>()), other_ptr);
      } else if (starts_with(other)) {
        std::size_t const suffix_size = subexpressions.size() - 1;
        return concat(other_ptr, either(this->get_last_n(suffix_size), std::make_unique<regex_epsilon>()));
      } else {
        return nullptr;
      }
    }
  }
  std::unique_ptr<regex_in_progress> append_with(std::unique_ptr<regex_in_progress> const& other_ptr) const
  {
    auto& other = *other_ptr;
    if (typeid(other) == typeid(regex_concat)) {
      auto result = copy();
      for (auto& se_ptr : dynamic_cast<regex_concat const&>(other).subexpressions) {
        result = concat(result, se_ptr);
      }
      return result;
    }
    // try to combine the last term with the incoming term
    auto all_but_last = get_first_n(subexpressions.size() - 1);
    auto last = get_last_n(1);
    auto combined_ptr = concat(last, other_ptr);
    auto& combined_ref = *combined_ptr;
    if (typeid(combined_ref) == typeid(regex_concat)) {
      // it failed to combine into anything
      auto result = std::make_unique<regex_concat>();
      result->add(*all_but_last);
      result->add(combined_ref);
      return result;
    } else {
      return concat(all_but_last, combined_ptr);
    }
  }
  std::unique_ptr<regex_in_progress> prepend_with(std::unique_ptr<regex_in_progress> const& other_ptr) const
  {
    auto& other = *other_ptr;
    if (typeid(other) == typeid(regex_concat)) {
      auto result = copy();
      auto& ses = dynamic_cast<regex_concat const&>(other).subexpressions;
      for (std::size_t i = 0; i < ses.size(); ++i) {
        result = concat(ses[ses.size() - 1 - i], result);
      }
      return result;
    }
    // try to combine the last term with the incoming term
    auto all_but_first = get_last_n(subexpressions.size() - 1);
    auto first = get_first_n(1);
    auto combined_ptr = concat(other_ptr, first);
    auto& combined_ref = *combined_ptr;
    if (typeid(combined_ref) == typeid(regex_concat)) {
      // it failed to combine into anything
      auto result = std::make_unique<regex_concat>();
      result->add(combined_ref);
      result->add(*all_but_first);
      return result;
    } else {
      return concat(combined_ptr, all_but_first);
    }
  }
  virtual bool needs_parentheses() const override
  {
    return true;
  }
};

class regex_star : public regex_in_progress {
  std::unique_ptr<regex_in_progress> subexpression;
 public:
  regex_star() = default;
  explicit regex_star(std::unique_ptr<regex_in_progress>&& se_in):subexpression(std::move(se_in)) {}
  virtual std::string print() const override
  {
    std::string result = subexpression->print();
    if (subexpression->needs_parentheses()) {
      result = std::string("(") + result + ")";
    }
    result += "*";
    return result;
  }
  virtual std::unique_ptr<regex_in_progress> copy() const override
  {
    auto result = std::make_unique<regex_star>();
    result->subexpression = subexpression->copy();
    return result;
  }
  virtual bool operator==(regex_in_progress const& other) const override
  {
    if (typeid(other) != typeid(regex_star)) return false;
    return *(dynamic_cast<regex_star const&>(other).subexpression) == *subexpression;
  }
  virtual bool needs_parentheses() const override
  {
    return false;
  }
  std::unique_ptr<regex_in_progress> const& get_subexpression() const
  {
    return subexpression;
  }
  std::unique_ptr<regex_in_progress> concat_with(regex_in_progress const& other) const
  {
    if (typeid(other) == typeid(regex_star)) {
      auto& other_star = dynamic_cast<regex_star const&>(other);
      if (*subexpression == *(other_star.subexpression)) {
        return copy();
      }
    }
    if (typeid(other) == typeid(regex_either)) {
      auto& other_either = dynamic_cast<regex_either const&>(other);
      if (other_either.is_question()) {
        auto question_subexpression = other_either.only_subexpression();
        if (*subexpression == *question_subexpression) {
          return std::make_unique<regex_star>(std::move(question_subexpression));
        }
      }
    }
    return nullptr;
  }
  std::unique_ptr<regex_in_progress> either_with(regex_in_progress const& other) const
  {
    if (typeid(other) == typeid(regex_epsilon)) {
      return copy();
    }
    if (typeid(other) == typeid(regex_star)) {
      regex_star const& other_star = dynamic_cast<regex_star const&>(other);
      if (*subexpression == *(other_star.subexpression)) {
        return copy();
      }
    }
    if (typeid(other) == typeid(regex_either)) {
      regex_either const& other_either = dynamic_cast<regex_either const&>(other);
      if (other_either.is_question()) {
        auto se_ptr = other_either.only_subexpression();
        if (*se_ptr == *subexpression) {
          return copy();
        }
      }
    }
    return nullptr;
  }
};

std::unique_ptr<regex_in_progress> either(
    std::unique_ptr<regex_in_progress> const& a,
    std::unique_ptr<regex_in_progress> const& b)
{
  auto& a_ref = *a;
  auto& b_ref = *b;
  if (a_ref == b_ref) return a_ref.copy();
  if (typeid(a_ref) == typeid(regex_null)) {
    return b->copy();
  }
  if (typeid(b_ref) == typeid(regex_null)) {
    return a->copy();
  }
  if (typeid(a_ref) == typeid(regex_star)) {
    auto result = dynamic_cast<regex_star const&>(a_ref).either_with(b_ref);
    if (result) return result;
  }
  if (typeid(b_ref) == typeid(regex_star)) {
    auto result = dynamic_cast<regex_star const&>(b_ref).either_with(a_ref);
    if (result) return result;
  }
  if (typeid(a_ref) == typeid(regex_concat)) {
    auto result = dynamic_cast<regex_concat const&>(a_ref).either_with(b);
    if (result) return result;
  }
  if (typeid(b_ref) == typeid(regex_concat)) {
    auto result = dynamic_cast<regex_concat const&>(b_ref).either_with(a);
    if (result) return result;
  }
  if (typeid(a_ref) == typeid(regex_either)) {
    return dynamic_cast<regex_either const&>(a_ref).either_with(b);
  }
  if (typeid(b_ref) == typeid(regex_either)) {
    return dynamic_cast<regex_either const&>(b_ref).either_with(a);
  }
  if ((typeid(a_ref) == typeid(regex_charset)) && (typeid(b_ref) == typeid(regex_charset))) {
    return dynamic_cast<regex_charset const&>(a_ref).either_with(dynamic_cast<regex_charset const&>(b_ref));
  }
  if ((typeid(a_ref) == typeid(regex_epsilon)) && (typeid(b_ref) == typeid(regex_epsilon))) {
    return std::make_unique<regex_epsilon>();
  }
  auto result = std::make_unique<regex_either>();
  result->insert(a_ref);
  result->insert(b_ref);
  return result;
}

std::unique_ptr<regex_in_progress> star(std::unique_ptr<regex_in_progress> const& a)
{
  auto& a_ref = *a;
  if (typeid(a_ref) == typeid(regex_null)) return std::make_unique<regex_null>();
  if (typeid(a_ref) == typeid(regex_epsilon)) return std::make_unique<regex_epsilon>();
  if (typeid(a_ref) == typeid(regex_star)) return a_ref.copy();
  if (typeid(a_ref) == typeid(regex_either)) {
    auto& either_ref = dynamic_cast<regex_either const&>(a_ref);
    if (either_ref.has_epsilon()) {
      return star(either_ref.remove_epsilon());
    }
  }
  return std::make_unique<regex_star>(a_ref.copy());
}

std::unique_ptr<regex_in_progress> concat(
    std::unique_ptr<regex_in_progress> const& a,
    std::unique_ptr<regex_in_progress> const& b)
{
  auto& a_ref = *a;
  auto& b_ref = *b;
  if (typeid(a_ref) == typeid(regex_null)) return std::make_unique<regex_null>();
  if (typeid(b_ref) == typeid(regex_null)) return std::make_unique<regex_null>();
  if (typeid(a_ref) == typeid(regex_epsilon)) return b->copy();
  if (typeid(b_ref) == typeid(regex_epsilon)) return a->copy();
  if (typeid(a_ref) == typeid(regex_star)) {
    auto result = dynamic_cast<regex_star const&>(a_ref).concat_with(b_ref);
    if (result) return result;
  }
  if (typeid(b_ref) == typeid(regex_star)) {
    auto result = dynamic_cast<regex_star const&>(b_ref).concat_with(a_ref);
    if (result) return result;
  }
  if (typeid(a_ref) == typeid(regex_concat)) {
    return dynamic_cast<regex_concat const&>(a_ref).append_with(b);
  }
  if (typeid(b_ref) == typeid(regex_concat)) {
    return dynamic_cast<regex_concat const&>(b_ref).prepend_with(a);
  }
  auto result = std::make_unique<regex_concat>(); 
  result->add(a_ref);
  result->add(b_ref);
  return result;
}

/*
 Brzozowski, Janusz A., and Edward J. McCluskey.
 "Signal flow graph techniques for
  sequential circuit state diagrams."
  IEEE Transactions on Electronic Computers 2 (1963): 67-76.

  Delgado, Manuel, and José Morais.
  "Approximation to the smallest regular expression
   for a given regular language."
  International Conference on Implementation
  and Application of Automata.
  Springer, Berlin, Heidelberg, 2004.

  https://cs.stackexchange.com/questions/2016/how-to-convert-finite-automata-to-regular-expressions
  */

void update_path(int i, int j, int k, std::vector<std::vector<std::unique_ptr<regex_in_progress>>>& L)
{
  L[i][j] = either(L[i][j], concat(L[i][k], concat(star(L[k][k]), L[k][j])));
}

std::string from_automaton(finite_automaton const& fa)
{
  int const nstates = get_nstates(fa);
  int const nsymbols = get_nsymbols(fa);
  assert(fa.is_deterministic);
  std::vector<std::vector<std::unique_ptr<regex_in_progress>>> L(nstates + 1);
  for (int i = 0; i < (nstates + 1); ++i) {
    L[i].resize(nstates + 1);
    for (int j = 0; j < (nstates + 1); ++j) {
      if (i == j) L[i][j] = std::make_unique<regex_epsilon>();
      else L[i][j] = std::make_unique<regex_null>();
    }
  }
  for (int i = 0; i < nstates; ++i) {
    for (int s = 0; s < nsymbols; ++s) {
      int const j = step(fa, i, s);
      if (j < 0) continue;
      L[i][j] = either(L[i][j], std::make_unique<regex_charset>(get_char(s)));
    }
  }
  // create a single accepting state with epsilon transitions
  for (int i = 0; i < nstates; ++i) {
    if (accepts(fa, i) != -1) {
      L[i][nstates] = std::make_unique<regex_epsilon>();
    }
  }
  std::vector<bool> vertex_exists(nstates + 1, true);
  for (int step = 0; step < (nstates - 1); ++step) {
    // pick a vertex to remove based on the weight
    // heuristic of Delgado and Morais
    int min_weight_state = -1;
    int min_weight = 0;
    for (int i = 1; i < nstates; ++i) {
      if (!vertex_exists[i]) continue;
      int in = 0;
      int out = 0;
      for (int j = 0; j < (nstates + 1); ++j) {
        auto& ij_ref = *L[i][j];
        auto& ji_ref = *L[j][i];
        if (typeid(ij_ref) != typeid(regex_null)) ++out;
        if (typeid(ji_ref) != typeid(regex_null)) ++in;
      }
      int weight = 0;
      auto& ii_ref = *L[i][i];
      if (typeid(ii_ref) != typeid(regex_null)) {
        weight += ii_ref.print().length() * (in * out - 1);
      }
      for (int j = 0; j < (nstates + 1); ++j) {
        auto& ij_ref = *L[i][j];
        auto& ji_ref = *L[j][i];
        if (typeid(ij_ref) != typeid(regex_null)) {
          weight += ij_ref.print().length() * (in - 1);
        }
        if (typeid(ji_ref) != typeid(regex_null)) {
          weight += ji_ref.print().length() * (out - 1);
        }
      }
      if (min_weight_state == -1 || weight < min_weight) {
        min_weight_state = i;
        min_weight = weight;
      }
    }
    // remove the vertex k
    int const k = min_weight_state;
    for (int i = 0; i < (nstates + 1); ++i) {
      if (!vertex_exists[i]) continue;
      for (int j = 0; j < (nstates + 1); ++j) {
        if (!vertex_exists[j]) continue;
        update_path(i, i, k, L);
        update_path(j, j, k, L);
        update_path(i, j, k, L);
        update_path(j, i, k, L);
      }
    }
    vertex_exists[k] = false;
  }
  int const f = nstates;
  int const s = 0;
  return concat(star(L[s][s]), concat(L[s][f], star(either(concat(L[f][s], concat(star(L[s][s]), L[s][f])), L[f][f]))))->print();
}

std::string for_first_occurrence_of(std::string const& s)
{
  auto fa = parsegen::regex::build_dfa("ends-with", std::string(".*") + s, 0);
  fa = parsegen::remove_transitions_from_accepting(fa);
  return parsegen::regex::from_automaton(fa);
}

std::string for_case_insensitive(std::string const& s)
{
  std::string result;
  for (auto c : s) {
    if (std::islower(c)) {
      result += '[';
      result += c;
      result += char(std::toupper(c));
      result += ']';
    } else if (std::isupper(c)) {
      result += '[';
      result += char(std::tolower(c));
      result += c;
      result += ']';
    } else {
      result += c;
    }
  }
  return result;
}

std::string maybe_sign() {
  return "[\\-\\+]?";
}

std::string leading_digits() {
  return "(0|([1-9][0-9]*))";
}

std::string trailing_digits() {
  return "[0-9]+";
}

// B: digits before the dot
// D: the dot
// A: digits after the dot
// E: exponent portion
//
//  B D A E  valid
//  0 1 1 0  1  form1
//  0 1 1 1  1  form1
//  1 0 0 1  1  form2
//  1 1 0 0  1  form3
//  1 1 0 1  1  form3
//  1 1 1 0  1  form3
//  1 1 1 1  1  form3

std::string unsigned_floating_point_not_integer() {
  std::string const b = leading_digits();
  std::string const d = "\\.";
  std::string const a = trailing_digits();
  std::string const e = "([eE]" + maybe_sign() + trailing_digits() + ")";
  std::string const maybe_a = a + "?";
  std::string const maybe_e = e + "?";
  std::string const form1 = "(" + d + a + maybe_e + ")";
  std::string const form2 = "(" + b + e + ")";
  std::string const form3 = "(" + b + d + maybe_a + maybe_e + ")";
  return "(" + form1 + "|" + form2 + "|" + form3 + ")";
}

std::string unsigned_integer() {
  return leading_digits();
}

std::string unsigned_floating_point() {
  return "(" + unsigned_floating_point_not_integer() + "|" + unsigned_integer() + ")";
}

std::string signed_integer() {
  return maybe_sign() + unsigned_integer();
}

std::string signed_floating_point_not_integer() {
  return maybe_sign() + unsigned_floating_point_not_integer();
}

std::string signed_floating_point() {
  return maybe_sign() + unsigned_floating_point();
}

std::string whitespace() {
  return "[ \t\n\r]+";
}

std::string newline() {
  return "\r?\n";
}

std::string identifier() {
  return "[_a-zA-Z][_a-zA-Z0-9]*";
}

std::string C_style_comment()
{
  // https://stackoverflow.com/questions/13014947/regex-to-match-a-c-style-multiline-comment
  using namespace std::string_literals;
  auto const slash = "/"s;
  auto const asterisk = "\\*"s;
  auto const comment_start = slash + asterisk;
  auto const not_asterisk = "[^\\*]"s;
  auto const neither_slash_nor_asterisk = "[^/\\*]"s;
  auto const zero_or_more_not_asterisks = not_asterisk + "*"s;
  auto const one_or_more_asterisks = asterisk + "+"s;
  auto const comment_head = zero_or_more_not_asterisks + one_or_more_asterisks;
  auto const comment_repeatee =
    neither_slash_nor_asterisk +
    zero_or_more_not_asterisks +
    one_or_more_asterisks;
  auto const comment_repeater = "("s + comment_repeatee + ")*"s;
  return comment_start + comment_head + comment_repeater + slash;
}

std::string double_quoted_string()
{
  return "\"([^\"\\]|\\.)*\"";
}

}  // end namespace regex
}  // end namespace parsegen
