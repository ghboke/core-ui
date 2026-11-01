#include "expr_rewriter.h"

#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ui::uix {

namespace {

// ---- Token kinds ----
enum class TK {
    Ident, Number, String, TemplateLit,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Dot, OptDot,        // .  ?.
    QMark,              // ?  (ternary; ?. is OptDot above)
    Colon, Comma, Semi,
    Arrow,              // =>
    Punct,              // any other operator
};

struct Tok {
    TK     kind;
    size_t start;       // byte offset in source (inclusive)
    size_t end;         // byte offset (exclusive)
    std::string text;   // verbatim slice (for Ident, used for prefix decision)
};

const std::set<std::string>& Keywords() {
    static const std::set<std::string> k = {
        "true", "false", "null", "undefined",
        "typeof", "instanceof", "void", "new", "delete", "in", "of",
        "return", "let", "const", "var", "function",
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "throw", "try", "catch", "finally",
        "this", "super", "yield", "async", "await",
    };
    return k;
}

const std::set<std::string>& Globals() {
    static const std::set<std::string> g = {
        "Math", "JSON", "Array", "Object", "String", "Number", "Boolean",
        "Date", "RegExp", "Promise", "Map", "Set", "Symbol",
        "parseInt", "parseFloat", "isNaN", "isFinite", "console",
        "Infinity", "NaN", "globalThis", "Error", "TypeError", "RangeError",
        "SyntaxError",
    };
    return g;
}

bool IsIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
bool IsIdentCont(char c) {
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

// ---- Tokenizer ----
//
// Robust over the full template-expression language (JS expression grammar).
// String / template literals are kept whole; comments collapsed to whitespace.
std::vector<Tok> Tokenize(const std::string& src) {
    std::vector<Tok> out;
    size_t i = 0, n = src.size();

    auto push = [&](TK k, size_t s, size_t e) {
        out.push_back(Tok{k, s, e, src.substr(s, e - s)});
    };

    while (i < n) {
        char c = src[i];

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }

        // Line comment
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        // Block comment
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            if (i + 1 < n) i += 2;
            continue;
        }

        // String
        if (c == '\'' || c == '"') {
            size_t s = i;
            char q = c; ++i;
            while (i < n && src[i] != q) {
                if (src[i] == '\\' && i + 1 < n) i += 2;
                else ++i;
            }
            if (i < n) ++i;
            push(TK::String, s, i);
            continue;
        }

        // Template literal — scan including ${...} embeds; we keep it as one
        // token, then recurse into ${...} contents at emit time.
        if (c == '`') {
            size_t s = i; ++i;
            int braceDepth = 0;
            while (i < n) {
                if (src[i] == '\\' && i + 1 < n) { i += 2; continue; }
                if (src[i] == '`' && braceDepth == 0) { ++i; break; }
                if (src[i] == '$' && i + 1 < n && src[i + 1] == '{' && braceDepth == 0) {
                    i += 2; braceDepth = 1;
                    continue;
                }
                if (braceDepth > 0) {
                    if (src[i] == '{') braceDepth++;
                    else if (src[i] == '}') {
                        braceDepth--;
                        if (braceDepth == 0) { ++i; continue; }
                    }
                }
                ++i;
            }
            push(TK::TemplateLit, s, i);
            continue;
        }

        // Number
        if ((c >= '0' && c <= '9') ||
            (c == '.' && i + 1 < n && src[i + 1] >= '0' && src[i + 1] <= '9')) {
            size_t s = i;
            while (i < n && src[i] >= '0' && src[i] <= '9') ++i;
            if (i < n && src[i] == '.') {
                ++i;
                while (i < n && src[i] >= '0' && src[i] <= '9') ++i;
            }
            if (i < n && (src[i] == 'e' || src[i] == 'E')) {
                ++i;
                if (i < n && (src[i] == '+' || src[i] == '-')) ++i;
                while (i < n && src[i] >= '0' && src[i] <= '9') ++i;
            }
            push(TK::Number, s, i);
            continue;
        }

        // Identifier / keyword
        if (IsIdentStart(c)) {
            size_t s = i; ++i;
            while (i < n && IsIdentCont(src[i])) ++i;
            push(TK::Ident, s, i);
            continue;
        }

        // Multi-char operators we care about
        if (i + 1 < n) {
            char d = src[i + 1];
            if (c == '?' && d == '.') { push(TK::OptDot, i, i + 2); i += 2; continue; }
            if (c == '=' && d == '>') { push(TK::Arrow,  i, i + 2); i += 2; continue; }
            // Other 2-char operators → Punct (==/!=/<=/>=/&&/||/??/+=/-=/*=//=/%=/++/--/**)
            std::string two; two += c; two += d;
            static const std::set<std::string> twoCharOps = {
                "==", "!=", "<=", ">=", "&&", "||", "??",
                "+=", "-=", "*=", "/=", "%=", "++", "--", "**",
                "<<", ">>", "&=", "|=", "^=",
            };
            if (twoCharOps.count(two)) {
                push(TK::Punct, i, i + 2); i += 2; continue;
            }
        }

        // Single-char tokens
        size_t s = i; ++i;
        switch (c) {
            case '(': push(TK::LParen,   s, i); break;
            case ')': push(TK::RParen,   s, i); break;
            case '{': push(TK::LBrace,   s, i); break;
            case '}': push(TK::RBrace,   s, i); break;
            case '[': push(TK::LBracket, s, i); break;
            case ']': push(TK::RBracket, s, i); break;
            case '.': push(TK::Dot,      s, i); break;
            case '?': push(TK::QMark,    s, i); break;
            case ':': push(TK::Colon,    s, i); break;
            case ',': push(TK::Comma,    s, i); break;
            case ';': push(TK::Semi,     s, i); break;
            default:  push(TK::Punct,    s, i); break;
        }
    }
    return out;
}

// Rewrite a single template literal's interior. The input is the slice
// including backticks. ${expr} embeds get their expr part recursively
// rewritten via the public entry point.
std::string RewriteTemplateLit(const std::string& src,
                                const std::set<std::string>& locals);

// Forward declaration of the main rewrite — used by template-literal recursion.
std::string RewriteImpl(const std::string& src,
                         const std::set<std::string>& locals);

std::string Rewrite(const std::string& src, const std::vector<Tok>& toks,
                     const std::set<std::string>& locals) {
    std::string out;
    out.reserve(src.size() + 32);

    // ---- Pre-scan: find arrow params + their token indices ----
    std::set<size_t> paramTokenIdx;   // token indices that are arrow params
    std::map<size_t, std::set<std::string>> arrowParams;  // arrow-tok-idx → names

    for (size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].kind != TK::Arrow) continue;
        std::set<std::string> ps;
        if (i > 0 && toks[i - 1].kind == TK::Ident) {
            // Single-param: `x => …`
            ps.insert(toks[i - 1].text);
            paramTokenIdx.insert(i - 1);
        } else if (i > 0 && toks[i - 1].kind == TK::RParen) {
            // `(...) => …` — find matching LParen
            int depth = 1;
            size_t j = i - 1;
            while (j > 0) {
                --j;
                if (toks[j].kind == TK::RParen) depth++;
                else if (toks[j].kind == TK::LParen) {
                    if (--depth == 0) break;
                }
            }
            // Collect Idents between j+1 and i-2 (inclusive)
            for (size_t k = j + 1; k < i - 1; ++k) {
                if (toks[k].kind != TK::Ident) continue;
                if (k > 0 && (toks[k - 1].kind == TK::Dot || toks[k - 1].kind == TK::OptDot))
                    continue;
                ps.insert(toks[k].text);
                paramTokenIdx.insert(k);
            }
        }
        arrowParams[i] = std::move(ps);
    }

    // ---- Walk + emit ----
    struct Scope {
        std::set<std::string> params;
        int depthAtStart;   // total depth (paren + brace + bracket) when scope began
    };
    std::vector<Scope> scopeStack;
    int parenD = 0, braceD = 0, bracketD = 0;
    auto totalDepth = [&] { return parenD + braceD + bracketD; };

    // Per-brace ternary `?` count, to disambiguate `:` (object-key vs ternary).
    // Each `{` push 0, each `}` pop. `?` increments top, `:` decrements (if > 0).
    std::vector<int> ternaryQ;
    ternaryQ.push_back(0);

    auto inArrowParamScope = [&](const std::string& name) {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->params.count(name)) return true;
        }
        return false;
    };

    auto isObjectKey = [&](size_t i) {
        // Ident followed by `:` while inside `{…}` and no open ternary at this level.
        if (toks[i].kind != TK::Ident) return false;
        if (braceD == 0) return false;
        if (i + 1 >= toks.size()) return false;
        if (toks[i + 1].kind != TK::Colon) return false;
        return ternaryQ.back() == 0;
    };

    size_t lastEnd = 0;

    for (size_t i = 0; i < toks.size(); ++i) {
        const Tok& t = toks[i];

        // Emit interstitial whitespace / comments verbatim.
        if (t.start > lastEnd) {
            out.append(src, lastEnd, t.start - lastEnd);
        }
        lastEnd = t.end;

        // Pop arrow scope if we've exited via depth-decrease OR via boundary
        // token (`,` `;` `)` `]` `}`) at the scope's start depth.
        while (!scopeStack.empty() && totalDepth() < scopeStack.back().depthAtStart) {
            scopeStack.pop_back();
        }
        if (!scopeStack.empty() && totalDepth() == scopeStack.back().depthAtStart) {
            if (t.kind == TK::Comma || t.kind == TK::Semi ||
                t.kind == TK::RParen || t.kind == TK::RBracket || t.kind == TK::RBrace) {
                scopeStack.pop_back();
            }
        }

        // Update ternary disambiguator BEFORE we use it for object-key check.
        // Note: order matters — we want `?` and `:` of the current token to
        // affect future tokens, not this one.

        // Update brace level and per-level ternary stack
        if (t.kind == TK::LBrace)   { braceD++; ternaryQ.push_back(0); }
        if (t.kind == TK::LParen)   parenD++;
        if (t.kind == TK::LBracket) bracketD++;

        // Emit / decide
        if (t.kind == TK::Ident) {
            bool prefix = true;
            // 1. Member-access target?
            if (i > 0 && (toks[i - 1].kind == TK::Dot || toks[i - 1].kind == TK::OptDot))
                prefix = false;
            // 2. Keyword / global?
            else if (Keywords().count(t.text) || Globals().count(t.text))
                prefix = false;
            // 3. Arrow param (token-marked, OR currently in scope)
            else if (paramTokenIdx.count(i) || inArrowParamScope(t.text))
                prefix = false;
            // 4. Object literal key
            else if (isObjectKey(i))
                prefix = false;
            // 5. Caller-supplied locals (e.g. v-for loop var / index var)
            else if (locals.count(t.text))
                prefix = false;
            // 6. `$event` — implicit param of every handler closure (Vue
            //    semantics), never a state member.
            else if (t.text == "$event")
                prefix = false;

            if (prefix) out += "this.";
            out += t.text;
        } else if (t.kind == TK::TemplateLit) {
            out += RewriteTemplateLit(t.text, locals);
        } else {
            out.append(src, t.start, t.end - t.start);
        }

        // Post-token state updates that affect FUTURE tokens
        if (t.kind == TK::Arrow) {
            auto it = arrowParams.find(i);
            if (it != arrowParams.end()) {
                Scope s;
                s.params = it->second;
                s.depthAtStart = totalDepth();
                scopeStack.push_back(std::move(s));
            }
        }
        if (t.kind == TK::QMark) {
            // We tokenized `?.` as OptDot, so any QMark here is a ternary.
            ternaryQ.back()++;
        }
        if (t.kind == TK::Colon) {
            if (ternaryQ.back() > 0) ternaryQ.back()--;
        }
        if (t.kind == TK::RParen)   parenD--;
        if (t.kind == TK::RBracket) bracketD--;
        if (t.kind == TK::RBrace)   {
            braceD--;
            if (ternaryQ.size() > 1) ternaryQ.pop_back();
        }
    }

    // Trailing whitespace.
    if (lastEnd < src.size()) {
        out.append(src, lastEnd, src.size() - lastEnd);
    }
    return out;
}

std::string RewriteImpl(const std::string& src,
                         const std::set<std::string>& locals) {
    auto toks = Tokenize(src);
    return Rewrite(src, toks, locals);
}

// Walk a template literal slice (from opening ` to closing `) and rewrite
// each ${expr} embed via RewriteImpl. Static text is preserved verbatim.
std::string RewriteTemplateLit(const std::string& src,
                                const std::set<std::string>& locals) {
    if (src.size() < 2 || src.front() != '`' || src.back() != '`') {
        return src;  // malformed — leave alone
    }
    std::string out;
    out.reserve(src.size() + 16);
    out += '`';

    size_t i = 1;
    size_t n = src.size() - 1;  // exclude trailing backtick

    while (i < n) {
        char c = src[i];
        if (c == '\\' && i + 1 < n) {
            out += c; out += src[i + 1]; i += 2; continue;
        }
        if (c == '$' && i + 1 < n && src[i + 1] == '{') {
            // Find matching `}`
            size_t j = i + 2;
            int depth = 1;
            while (j < n && depth > 0) {
                if (src[j] == '\\' && j + 1 < n) { j += 2; continue; }
                if (src[j] == '{') depth++;
                else if (src[j] == '}') {
                    depth--;
                    if (depth == 0) break;
                }
                ++j;
            }
            if (depth != 0) {
                // Unbalanced — leave the rest verbatim
                out.append(src, i, n - i);
                i = n;
                break;
            }
            std::string inner = src.substr(i + 2, j - (i + 2));
            out += "${";
            out += RewriteImpl(inner, locals);
            out += "}";
            i = j + 1;
            continue;
        }
        out += c;
        ++i;
    }
    out += '`';
    return out;
}

}  // namespace

std::string RewriteTemplateExpr(const std::string& expr,
                                 const std::set<std::string>& locals) {
    return RewriteImpl(expr, locals);
}

}  // namespace ui::uix
