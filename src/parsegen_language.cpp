#include "parsegen_language.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>

#include "parsegen_build_parser.hpp"
#include "parsegen_regex.hpp"
#include "parsegen_std_vector.hpp"
#include "parsegen_string.hpp"
#include "parsegen_error.hpp"

namespace parsegen {

grammar_ptr build_grammar(language const& language) {
  std::map<std::string, int> symbol_map;
  int nterminals = 0;
  for (auto& token : language.tokens) {
    symbol_map[token.name] = nterminals++;
  }
  int nsymbols = nterminals;
  for (auto& production : language.productions) {
    if (production.lhs.empty()) {
      std::cerr << "ERROR: production "
        << (&production - language.productions.data())
        << " has empty left hand side\n";
      abort();
    }
    if (symbol_map.count(production.lhs)) continue;
    symbol_map[production.lhs] = nsymbols++;
  }
  grammar out;
  out.nsymbols = nsymbols;
  out.nterminals = nterminals;
  for (auto& lang_prod : language.productions) {
    grammar::production gprod;
    assert(symbol_map.count(lang_prod.lhs));
    gprod.lhs = symbol_map[lang_prod.lhs];
    for (auto& lang_symb : lang_prod.rhs) {
      if (!symbol_map.count(lang_symb)) {
        std::stringstream ss;
        ss << "RHS entry \"" << lang_symb
           << "\" is neither a nonterminal (LHS of a production) nor a "
              "token!\n";
        throw std::invalid_argument(ss.str());
      }
      gprod.rhs.push_back(symbol_map[lang_symb]);
    }
    out.productions.emplace_back(std::move(gprod));
  }
  out.symbol_names = make_vector<std::string>(nsymbols);
  for (auto& pair : symbol_map) {
    at(out.symbol_names, pair.second) = pair.first;
  }
  add_end_terminal(out);
  add_accept_production(out);
  for (auto const& name : language.ignored_tokens) {
    auto const it = symbol_map.find(name);
    if (it == symbol_map.end()) {
      throw std::runtime_error("ignored token " + name + " does not exist");
    }
    out.ignored_terminals.push_back(it->second);
  }
  return std::make_shared<grammar>(std::move(out));
}

std::ostream& operator<<(std::ostream& os, language const& lang) {
  for (auto& token : lang.tokens) {
    os << "token " << token.name << " regex " << single_quote(token.regex) << "\n";
  }
  std::set<std::string> nonterminal_set;
  std::vector<std::string> nonterminal_list;
  for (auto& prod : lang.productions) {
    if (!nonterminal_set.count(prod.lhs)) {
      nonterminal_set.insert(prod.lhs);
      nonterminal_list.push_back(prod.lhs);
    }
  }
  for (auto& nonterminal : nonterminal_list) {
    std::stringstream ss;
    ss << nonterminal << " ::=";
    auto lead = ss.str();
    os << lead;
    for (auto& c : lead) c = ' ';
    bool first = true;
    for (auto& prod : lang.productions) {
      if (prod.lhs != nonterminal) continue;
      if (first)
        first = false;
      else
        os << " |\n" << lead;
      for (auto& symb : prod.rhs) {
        if (symb == "|")
          os << " '|'";
        else
          os << " " << symb;
      }
    }
    os << "\n";
  }
  os << "\n";
  return os;
}

finite_automaton build_lexer(language const& language) {
  finite_automaton lexer;
  for (int i = 0; i < isize(language.tokens); ++i) {
    auto& name = at(language.tokens, i).name;
    auto& regex = at(language.tokens, i).regex;
    if (name.empty()) {
      std::cerr << "ERROR: token "
        << i << " has empty name\n";
      abort();
    }
    if (regex.empty()) {
      std::cerr << "ERROR: token "
        << i << " has empty regex\n";
      abort();
    }
    if (i == 0) {
      lexer = regex::build_dfa(name, regex, i);
    } else {
      lexer = finite_automaton::unite(lexer, regex::build_dfa(name, regex, i));
    }
  }
  lexer = finite_automaton::simplify(finite_automaton::make_deterministic(lexer));
  return lexer;
}

static indentation build_indent_info(language const& language) {
  indentation out;
  out.is_sensitive = false;
  out.indent_token = -1;
  out.dedent_token = -1;
  out.newline_token = -1;
  for (int tok_i = 0; tok_i < isize(language.tokens); ++tok_i) {
    auto& token = at(language.tokens, tok_i);
    if (token.name == "INDENT") {
      if (out.indent_token != -1) {
        throw std::invalid_argument("The language has two or more INDENT tokens\n");
      }
      out.indent_token = tok_i;
      out.is_sensitive = true;
    } else if (token.name == "DEDENT") {
      if (out.dedent_token != -1) {
        throw std::invalid_argument("The language has two or more DEDENT tokens\n");
      }
      out.dedent_token = tok_i;
    } else if (token.name == "NEWLINE") {
      if (out.newline_token != -1) {
        throw std::invalid_argument("The language has two or more NEWLINE tokens\n");
      }
      out.newline_token = tok_i;
    }
  }
  if (out.is_sensitive && out.indent_token == -1) {
    throw std::invalid_argument(
        "This indentation-sensitive language has no INDENT token\n");
  }
  if (out.is_sensitive && out.dedent_token == -1) {
    throw std::invalid_argument(
        "This indentation-sensitive language has no DEDENT token\n");
  }
  if (out.is_sensitive && out.newline_token == -1) {
    throw std::invalid_argument(
        "This indentation-sensitive language has no NEWLINE token\n");
  }
  if (out.indent_token < out.newline_token ||
      out.dedent_token < out.newline_token) {
    throw std::invalid_argument(
        "NEWLINE needs to come before all other indent tokens\n");
  }
  return out;
}

parser_tables_ptr build_parser_tables(language const& language) {
  auto lexer = build_lexer(language);
  auto indent_info = build_indent_info(language);
  auto grammar = build_grammar(language);
  auto parser = accept_parser(build_lalr1_parser(grammar));
  return parser_tables_ptr(new parser_tables({parser, lexer, indent_info}));
}

}  // namespace parsegen
