#pragma once
#include "css_ast.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::css {

// State flags for pseudo-class matching.
namespace state {
constexpr uint32_t Hover    = 1u << 0;
constexpr uint32_t Pressed  = 1u << 1;
constexpr uint32_t Focus    = 1u << 2;
constexpr uint32_t Disabled = 1u << 3;
constexpr uint32_t Checked  = 1u << 4;
constexpr uint32_t Root     = 1u << 5;
constexpr uint32_t DragOver = 1u << 6;  // drop target under an active drag
}

// Lightweight descriptor passed to the matcher.
// Runtime fills this from a Widget's current state.
struct MatchNode {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    std::unordered_map<std::string, std::string> attrs;
    uint32_t    stateBits = 0;
    const MatchNode* parent = nullptr;

    bool HasClass(const std::string& c) const {
        for (auto& x : classes) if (x == c) return true;
        return false;
    }
};

// Returns true if `node` satisfies the complex selector.
// Walks parent chain for descendant/child combinators.
bool Matches(const ComplexSelector& sel, const MatchNode& node);

// Returns true if any selector in the list matches.
bool MatchesAny(const SelectorList& list, const MatchNode& node);

// Map pseudo-class name to state bit. Returns 0 if unknown.
uint32_t PseudoToBit(const std::string& name);

}  // namespace ui::css
