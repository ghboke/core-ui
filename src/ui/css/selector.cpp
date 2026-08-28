#include "selector.h"
#include <algorithm>

namespace ui::css {

uint32_t PseudoToBit(const std::string& name) {
    if (name == "hover")    return state::Hover;
    if (name == "pressed" || name == "active") return state::Pressed;
    if (name == "focus")    return state::Focus;
    if (name == "disabled") return state::Disabled;
    if (name == "checked")  return state::Checked;
    if (name == "root")     return state::Root;
    if (name == "drag-over") return state::DragOver;
    return 0;
}

static bool AttrMatches(const std::string& actual, AttrOp op, const std::string& expected) {
    switch (op) {
        case AttrOp::Exists:    return true;
        case AttrOp::Equals:    return actual == expected;
        case AttrOp::Includes: {
            // whitespace-separated words
            size_t i = 0;
            while (i < actual.size()) {
                while (i < actual.size() && std::isspace(static_cast<unsigned char>(actual[i]))) i++;
                size_t j = i;
                while (j < actual.size() && !std::isspace(static_cast<unsigned char>(actual[j]))) j++;
                if (j > i && actual.substr(i, j - i) == expected) return true;
                i = j;
            }
            return false;
        }
        case AttrOp::DashMatch:
            if (actual == expected) return true;
            return actual.size() > expected.size() &&
                   actual.compare(0, expected.size(), expected) == 0 &&
                   actual[expected.size()] == '-';
        case AttrOp::Prefix:
            return actual.size() >= expected.size() &&
                   actual.compare(0, expected.size(), expected) == 0;
        case AttrOp::Suffix:
            return actual.size() >= expected.size() &&
                   actual.compare(actual.size() - expected.size(), expected.size(), expected) == 0;
        case AttrOp::Substring:
            return actual.find(expected) != std::string::npos;
    }
    return false;
}

static bool MatchSimple(const SimpleSelector& ss, const MatchNode& node) {
    switch (ss.kind) {
        case SimpleKind::Universal:
            return true;
        case SimpleKind::Tag:
            return node.tag == ss.name;
        case SimpleKind::Class:
            return node.HasClass(ss.name);
        case SimpleKind::Id:
            return node.id == ss.name;
        case SimpleKind::Pseudo: {
            uint32_t bit = PseudoToBit(ss.name);
            if (bit == 0) return false;
            return (node.stateBits & bit) != 0;
        }
        case SimpleKind::Attr: {
            auto it = node.attrs.find(ss.name);
            if (it == node.attrs.end()) return false;
            return AttrMatches(it->second, ss.attrOp, ss.attrValue);
        }
    }
    return false;
}

static bool MatchCompound(const CompoundSelector& cmpd, const MatchNode& node) {
    for (auto& part : cmpd.parts) {
        if (!MatchSimple(part, node)) return false;
    }
    return true;
}

// Walks from the RIGHTMOST compound leftward using combinators.
bool Matches(const ComplexSelector& sel, const MatchNode& node) {
    if (sel.compounds.empty()) return false;
    // Rightmost must match this node
    if (!MatchCompound(sel.compounds.back(), node)) return false;

    const MatchNode* cur = node.parent;
    // Walk combinators right-to-left
    for (size_t i = sel.compounds.size() - 1; i > 0; --i) {
        Combinator comb = sel.combinators[i - 1];
        const CompoundSelector& target = sel.compounds[i - 1];

        if (comb == Combinator::Child) {
            if (!cur) return false;
            if (!MatchCompound(target, *cur)) return false;
            cur = cur->parent;
        } else {
            // Descendant: scan ancestors for a match
            bool found = false;
            while (cur) {
                if (MatchCompound(target, *cur)) { found = true; cur = cur->parent; break; }
                cur = cur->parent;
            }
            if (!found) return false;
        }
    }
    return true;
}

bool MatchesAny(const SelectorList& list, const MatchNode& node) {
    for (auto& sel : list) {
        if (Matches(sel, node)) return true;
    }
    return false;
}

}  // namespace ui::css
