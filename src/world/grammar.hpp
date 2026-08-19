#ifndef EIDOLON_GRAMMAR_HPP
#define EIDOLON_GRAMMAR_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <cstdint>

#include "core/vec2.hpp"

namespace eidolon {

// Formal grammars for structured goal/event templates, recipe production rules,
// grounded utterance templates (Phase 5 branch, DESIGN §22).
// Deterministic, seeded, bit-exact replay.

// Grammar symbol: terminal (string) or non-terminal (symbol name)
struct GrammarSymbol {
  bool isTerminal = false;
  std::string value; // terminal string or non-terminal name

  static GrammarSymbol terminal(const std::string& s) { return {true, s}; }
  static GrammarSymbol nonTerminal(const std::string& s) { return {false, s}; }
};

// Production rule: LHS non-terminal -> RHS sequence of symbols
struct Production {
  std::string lhs; // non-terminal name
  std::vector<GrammarSymbol> rhs;
  float weight = 1.0f; // for probabilistic grammars
};

// Context-free grammar
struct Grammar {
  std::string startSymbol = "ROOT";
  std::vector<Production> productions;
  std::unordered_map<std::string, std::vector<size_t>> lhsIndex; // LHS -> production indices
};

struct GrammarDerivation {
  std::string result; // fully expanded string
  std::vector<std::string> steps; // intermediate forms
  std::vector<std::pair<std::string, std::string>> appliedRules; // (LHS, RHS)
};

class GrammarEngine {
public:
  GrammarEngine() = default;
  explicit GrammarEngine(const Grammar& g) : grammar_(g) { buildIndex(); }

  void setGrammar(const Grammar& g) { grammar_ = g; buildIndex(); }

  // Derive from start symbol (deterministic: first matching production)
  GrammarDerivation derive(int maxSteps = 100) const;

  // Stochastic derivation using RNG for probabilistic choices
  GrammarDerivation deriveStochastic(class Rng& rng, int maxSteps = 100) const;

  // Parse a string using the grammar (CYK algorithm for CFG)
  // Returns true if string is in the language
  bool parse(const std::string& str) const;

  // Get all possible expansions for a non-terminal (for inspection)
  std::vector<std::vector<GrammarSymbol>> getExpansions(const std::string& nonTerminal) const;

  const Grammar& grammar() const { return grammar_; }

private:
  Grammar grammar_;

  void buildIndex() {
    grammar_.lhsIndex.clear();
    for (size_t i = 0; i < grammar_.productions.size(); ++i) {
      grammar_.lhsIndex[grammar_.productions[i].lhs].push_back(i);
    }
  }
};

// Predefined grammars for Eidolon
Grammar makeGoalGrammar();       // structured goal templates
Grammar makeEventGrammar();      // event templates for episodic memory
Grammar makeRecipeGrammar();     // recipe production rules
Grammar makeUtteranceGrammar();  // grounded utterance templates

} // namespace eidolon

#endif // EIDOLON_GRAMMAR_HPP