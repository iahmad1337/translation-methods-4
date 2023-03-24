#include <regex>

#include <absl/strings/str_split.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>

#include <absl/log/log.h>

#include <range/v3/action/transform.hpp>
#include <range/v3/all.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/any_view.hpp>

#include <cpputils/string.hh>
#include <cpputils/common.hh>

#include "common.hh"

static const std::regex TOKEN_REGEX{"[A-Z][A-Z0-9_]*"};
static const std::regex NONTERMINAL_REGEX{"[a-z][a-z0-9_]*"};
static const std::regex TS_REGEX{"\\$[a-z][a-z0-9_]*"};  // TODO: check if the
                                                         // escaping stuff is
                                                         // valid
constexpr auto IS_TOKEN = [] (std::string_view s) { return std::regex_match(s.begin(), s.end(), TOKEN_REGEX); };
constexpr auto IS_NTERM = [] (std::string_view s) { return std::regex_match(s.begin(), s.end(), NONTERMINAL_REGEX); };
constexpr auto IS_TS = [] (std::string_view s) { return std::regex_match(s.begin(), s.end(), TS_REGEX); };

std::shared_ptr<TGrammar> ParseGrammar(const std::string& grammarString) {

  TGrammar grammar;
  auto [tokensLines, productions] = ConstSplit<2>(grammarString, "\n%%");

  for (auto line : absl::StrSplit(tokensLines, '\n', absl::SkipWhitespace())) {
    assert(!line.empty());
    auto [tokId, regex] = ConstSplit<2>(line, "    ");
    EXPECT(IS_TOKEN(tokId), absl::StrFormat("Token doesn't match the format: `%s`", tokId));
    grammar.tokenToRegex[tokId] = regex;
  }

  EXPECT(!utils::OneOf("EPS", grammar.tokenToRegex | ranges::views::keys), "Don't define reserved token EPS");

  for (auto productionGroup : absl::StrSplit(productions, ';', absl::SkipWhitespace())) {
    auto [nonTermId, ps] = ConstSplit<2>(productionGroup, ":");

    nonTermId = utils::Trim(nonTermId);
    EXPECT(IS_NTERM(nonTermId), absl::StrFormat("Non-terminal doesn't match the format: `%s`", nonTermId));

    auto& ruleGroup = grammar.rules[nonTermId];

    for (auto production : absl::StrSplit(ps, '|')) {
      EXPECT(!(production | ranges::views::filter([](char c) { return !std::isspace(c); })).empty(), "Empty productions are prohibited");

      std::vector<std::string> vec{absl::StrSplit(production, ' ', absl::SkipWhitespace())};
      vec |= ranges::actions::transform([] (std::string& s) { return utils::Trim(s); });

      EXPECT(ranges::all_of(vec, [] (const std::string& s) {
            return IS_TOKEN(s) || IS_NTERM(s) || IS_TS(s);
      }), "The right hand side of the production should only contain tokens, nonterminals or translating symbols");

      ruleGroup.push_back(vec);
    }
  }

  return std::make_shared<TGrammar>(std::move(grammar));
}

std::unordered_set<std::string>& CalculateRecurFIRST(TGrammar& grammar, ranges::any_view<std::string, ranges::category::bidirectional | ranges::category::sized> alpha) {
  if (alpha.empty()) {
    return grammar.first["EPS"];  // basically { EPS }
  }

  std::string realAlpha = alpha | ranges::views::join(' ') | ranges::to<std::string>();
  auto& fst = grammar.first[std::move(realAlpha)];

  std::string head = *alpha.begin();
  if (head == "EPS" || IS_TS(head)) {
    return CalculateRecurFIRST(grammar, alpha | ranges::views::drop(1));
  } else if (IS_TOKEN(head)) {
    grammar.first[head].insert(head);
    fst.insert(head);
    return fst;
  } else if (IS_NTERM(head)) {
    auto& lhsFirst = grammar.first[head];
    auto& rhsFirst = CalculateRecurFIRST(grammar, alpha | ranges::views::drop(1));
    for (const auto& tokLhs : lhsFirst) {
      if (tokLhs == "EPS") {
        for (const auto& tokRhs : rhsFirst) {
          fst.insert(tokRhs);
        }
      } else {
        fst.insert(tokLhs);
      }
    }
    return fst;
  } else {
    EXPECT(false, absl::StrFormat("Unreachable statement. Attempt to calculate FIRST(%s)", realAlpha));
  }
}

// TODO: get rid of "useless" symbols, otherwise algorithm will not work
void TGrammar::CalculateFIRST() {
  EXPECT(first.empty(), "FIRST set should be calculated only once");
  // Initialize
  first["EPS"] = { "EPS" };

  // Calculate recursively
  bool change{true};
  while (change) {
    change = false;
    for (auto& [lhs, rhsGroup] : rules) {
      for (auto& rhs : rhsGroup) {
        auto& lhsFirst = first[lhs];
        auto oldSize = lhsFirst.size();
        EXPECT(!ranges::equal(ranges::views::single(lhs), ranges::views::all(rhs)), absl::StrFormat("Productions of form `%s : %s` are prohibited", lhs, rhs.front()));
        for (const auto& tok : CalculateRecurFIRST(*this, rhs)) {
          lhsFirst.insert(tok);
        }
        if (lhsFirst.size() != oldSize) {
          change = true;
        }
      }
    }
  }
}