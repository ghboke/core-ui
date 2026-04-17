#include "markup_parser.h"
#include <sstream>
#include <cctype>
#include <cstring>

namespace ui {
namespace {

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src), pos_(0), line_(1) {}

    bool Parse(UiDocument& doc, std::string& err) {
        SkipWhitespace();

        // Expect <ui ...>
        if (!Expect('<')) return Error(err, "expected '<ui>'");
        std::string tag = ReadName();
        if (tag != "ui") return Error(err, "root element must be <ui>, got <" + tag + ">");

        // Read <ui> attributes (e.g. version="1")
        auto attrs = ReadAttributes(err);
        if (!err.empty()) return false;
        if (attrs.count("version"))   doc.version = std::stoi(attrs["version"]);
        if (attrs.count("width"))     doc.window.width  = std::stoi(attrs["width"]);
        if (attrs.count("height"))    doc.window.height = std::stoi(attrs["height"]);
        if (attrs.count("resizable"))     doc.window.resizable = (attrs["resizable"] == "true" || attrs["resizable"] == "1") ? 1 : 0;
        if (attrs.count("tabNavigation")) doc.window.tabNavigation = (attrs["tabNavigation"] == "true" || attrs["tabNavigation"] == "1") ? 1 : 0;
        if (attrs.count("title"))         doc.window.title = attrs["title"];

        if (!Expect('>')) return Error(err, "expected '>' after <ui>");

        SkipWhitespace();

        // Parse children: <style> blocks and widget elements
        bool foundRoot = false;
        while (pos_ < src_.size()) {
            SkipWhitespace();
            if (pos_ >= src_.size()) break;

            // Check for closing tag
            if (LookAhead("</")) {
                pos_ += 2;
                std::string close = ReadName();
                SkipWhitespace();
                Expect('>');
                if (close != "ui") return Error(err, "expected </ui>, got </" + close + ">");
                break;
            }

            // Check for comment
            if (LookAhead("<!--")) {
                SkipComment();
                continue;
            }

            if (!LookAhead("<")) return Error(err, "expected '<'");

            // Peek tag name
            size_t saved = pos_;
            pos_++; // skip '<'
            std::string childTag = ReadName();
            pos_ = saved; // restore

            if (childTag == "style") {
                if (!ParseStyleBlock(doc.styles, doc.mediaQueries, err)) return false;
            } else {
                if (foundRoot) return Error(err, "only one root widget element allowed inside <ui>");
                if (!ParseElement(doc.root, err)) return false;
                foundRoot = true;
            }
        }

        if (!foundRoot) return Error(err, "no root widget element found inside <ui>");
        return true;
    }

private:
    const std::string& src_;
    size_t pos_;
    int line_;

    char Peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char Advance() {
        char c = src_[pos_++];
        if (c == '\n') line_++;
        return c;
    }

    bool Expect(char c) {
        SkipWhitespace();
        if (Peek() == c) { Advance(); return true; }
        return false;
    }

    bool LookAhead(const char* s) const {
        size_t len = strlen(s);
        if (pos_ + len > src_.size()) return false;
        return src_.compare(pos_, len, s) == 0;
    }

    void SkipWhitespace() {
        while (pos_ < src_.size() && std::isspace((unsigned char)src_[pos_])) {
            if (src_[pos_] == '\n') line_++;
            pos_++;
        }
    }

    void SkipComment() {
        pos_ += 4; // skip <!--
        while (pos_ + 2 < src_.size()) {
            if (src_[pos_] == '-' && src_[pos_+1] == '-' && src_[pos_+2] == '>') {
                pos_ += 3;
                return;
            }
            if (src_[pos_] == '\n') line_++;
            pos_++;
        }
        pos_ = src_.size();
    }

    std::string ReadName() {
        std::string name;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
                name += c;
                pos_++;
            } else break;
        }
        return name;
    }

    // Read until a delimiter char (used for quoted strings)
    std::string ReadUntil(char delim) {
        std::string result;
        while (pos_ < src_.size() && src_[pos_] != delim) {
            if (src_[pos_] == '\n') line_++;
            result += src_[pos_++];
        }
        if (pos_ < src_.size()) pos_++; // skip delimiter
        return result;
    }

    std::map<std::string, std::string> ReadAttributes(std::string& err) {
        std::map<std::string, std::string> attrs;
        while (pos_ < src_.size()) {
            SkipWhitespace();
            char c = Peek();
            if (c == '>' || c == '/' || c == '\0') break;

            std::string key = ReadName();
            if (key.empty()) { Error(err, "expected attribute name"); return attrs; }

            SkipWhitespace();
            if (Peek() != '=') { Error(err, "expected '=' after attribute '" + key + "'"); return attrs; }
            Advance(); // skip '='
            SkipWhitespace();

            char quote = Peek();
            if (quote != '"' && quote != '\'') {
                Error(err, "expected '\"' after '" + key + "='");
                return attrs;
            }
            Advance(); // skip opening quote
            std::string value = ReadUntil(quote);

            attrs[key] = DecodeXmlEntities(value);
        }
        return attrs;
    }

    // Decode XML entities: &lt; &gt; &amp; &apos; &quot; &#xNNNN; &#NNNN;
    static std::string DecodeXmlEntities(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ) {
            if (s[i] == '&') {
                size_t semi = s.find(';', i);
                if (semi != std::string::npos) {
                    std::string ent = s.substr(i + 1, semi - i - 1);
                    if (ent == "lt")        { out += '<'; i = semi + 1; continue; }
                    if (ent == "gt")        { out += '>'; i = semi + 1; continue; }
                    if (ent == "amp")       { out += '&'; i = semi + 1; continue; }
                    if (ent == "apos")      { out += '\''; i = semi + 1; continue; }
                    if (ent == "quot")      { out += '"'; i = semi + 1; continue; }
                    if (ent.size() >= 2 && ent[0] == '#') {
                        // Numeric entity: &#xHHHH; or &#DDDD;
                        unsigned long cp = 0;
                        if (ent[1] == 'x' || ent[1] == 'X')
                            cp = std::strtoul(ent.c_str() + 2, nullptr, 16);
                        else
                            cp = std::strtoul(ent.c_str() + 1, nullptr, 10);
                        // Encode as UTF-8
                        if (cp < 0x80) {
                            out += (char)cp;
                        } else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        i = semi + 1;
                        continue;
                    }
                }
            }
            out += s[i++];
        }
        return out;
    }

    bool ParseElement(UiNode& node, std::string& err) {
        SkipWhitespace();
        if (!Expect('<')) return Error(err, "expected '<'");

        node.line = line_;
        node.tag = ReadName();
        if (node.tag.empty()) return Error(err, "expected element name after '<'");

        node.attrs = ReadAttributes(err);
        if (!err.empty()) return false;

        SkipWhitespace();

        // Self-closing? <Tag ... />
        if (LookAhead("/>")) {
            pos_ += 2;
            return true;
        }

        if (!Expect('>')) return Error(err, "expected '>' or '/>' for <" + node.tag + ">");

        // Parse children until </tag>
        while (pos_ < src_.size()) {
            SkipWhitespace();
            if (pos_ >= src_.size()) break;

            if (LookAhead("<!--")) {
                SkipComment();
                continue;
            }

            if (LookAhead("</")) {
                pos_ += 2;
                std::string close = ReadName();
                SkipWhitespace();
                Expect('>');
                if (close != node.tag)
                    return Error(err, "mismatched close tag: expected </" + node.tag + ">, got </" + close + ">");
                return true;
            }

            if (LookAhead("<")) {
                UiNode child;
                if (!ParseElement(child, err)) return false;
                node.children.push_back(std::move(child));
            } else {
                // Skip text content (not used for widget trees)
                while (pos_ < src_.size() && src_[pos_] != '<') {
                    if (src_[pos_] == '\n') line_++;
                    pos_++;
                }
            }
        }

        return Error(err, "unclosed element <" + node.tag + ">");
    }

    bool ParseStyleBlock(std::vector<StyleRule>& styles, std::vector<MediaQuery>& mediaQueries, std::string& err) {
        // Skip <style> open tag
        Expect('<');
        std::string tag = ReadName();
        if (tag != "style") return Error(err, "expected <style>");
        ReadAttributes(err); // ignore attributes on <style>
        if (!Expect('>')) return Error(err, "expected '>' after <style>");

        // Read raw CSS-like text until </style>
        std::string body;
        while (pos_ < src_.size()) {
            if (LookAhead("</style>")) {
                pos_ += 8;
                break;
            }
            if (src_[pos_] == '\n') line_++;
            body += src_[pos_++];
        }

        // Parse CSS-like rules + @media blocks
        ParseStyleBody(body, styles, &mediaQueries);
        return true;
    }

    void ParseStyleBody(const std::string& body, std::vector<StyleRule>& styles,
                         std::vector<MediaQuery>* mediaQueries = nullptr) {
        size_t p = 0;
        auto skip_ws = [&]() {
            while (p < body.size() && std::isspace((unsigned char)body[p])) p++;
        };
        auto skip_comment = [&]() {
            skip_ws();
            while (p + 1 < body.size() && body[p] == '/' && body[p+1] == '*') {
                p += 2;
                while (p + 1 < body.size() && !(body[p] == '*' && body[p+1] == '/')) p++;
                if (p + 1 < body.size()) p += 2;
                skip_ws();
            }
        };

        while (p < body.size()) {
            skip_ws();
            skip_comment();
            if (p >= body.size()) break;

            // Check for @media block
            if (mediaQueries && body.substr(p, 6) == "@media") {
                p += 6;
                skip_ws();
                // Parse condition: (minWidth: 800)
                MediaQuery mq;
                if (p < body.size() && body[p] == '(') {
                    p++; // skip (
                    while (p < body.size() && body[p] != ')') {
                        skip_ws();
                        std::string key, val;
                        while (p < body.size() && body[p] != ':' && body[p] != ')') key += body[p++];
                        while (!key.empty() && key.back() == ' ') key.pop_back();
                        if (p < body.size() && body[p] == ':') p++;
                        skip_ws();
                        while (p < body.size() && body[p] != ',' && body[p] != ')') val += body[p++];
                        while (!val.empty() && val.back() == ' ') val.pop_back();

                        float fv = 0; try { fv = std::stof(val); } catch (...) {}
                        if (key == "minWidth") mq.minWidth = fv;
                        else if (key == "maxWidth") mq.maxWidth = fv;
                        else if (key == "minHeight") mq.minHeight = fv;
                        else if (key == "maxHeight") mq.maxHeight = fv;

                        if (p < body.size() && body[p] == ',') p++;
                    }
                    if (p < body.size() && body[p] == ')') p++;
                }
                skip_ws();
                // Parse inner { ... } block
                if (p < body.size() && body[p] == '{') {
                    p++;
                    // Find matching }
                    int depth = 1;
                    std::string innerBody;
                    while (p < body.size() && depth > 0) {
                        if (body[p] == '{') depth++;
                        else if (body[p] == '}') { depth--; if (depth == 0) break; }
                        innerBody += body[p++];
                    }
                    if (p < body.size()) p++; // skip closing }
                    ParseStyleBody(innerBody, mq.rules);
                }
                mediaQueries->push_back(std::move(mq));
                continue;
            }

            // Read selector
            std::string selector;
            while (p < body.size() && body[p] != '{') {
                selector += body[p++];
            }
            // Trim
            while (!selector.empty() && std::isspace((unsigned char)selector.back()))
                selector.pop_back();
            if (selector.empty() || p >= body.size()) break;
            p++; // skip '{'

            // Read properties until '}'
            StyleRule rule;
            rule.selector = selector;

            while (p < body.size() && body[p] != '}') {
                skip_ws();
                if (p >= body.size() || body[p] == '}') break;

                // key: value;
                std::string key, val;
                while (p < body.size() && body[p] != ':' && body[p] != '}') key += body[p++];
                // Trim key
                while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
                if (p < body.size() && body[p] == ':') p++;

                skip_ws();
                while (p < body.size() && body[p] != ';' && body[p] != '}') val += body[p++];
                // Trim val
                while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();
                if (p < body.size() && body[p] == ';') p++;

                if (!key.empty()) rule.props[key] = val;
            }
            if (p < body.size()) p++; // skip '}'

            if (!rule.selector.empty()) styles.push_back(std::move(rule));
        }
    }

    bool Error(std::string& err, const std::string& msg) {
        err = "line " + std::to_string(line_) + ": " + msg;
        return false;
    }
};

} // anonymous namespace

bool ParseUiMarkup(const std::string& source, UiDocument& doc, std::string& errorOut) {
    errorOut.clear();
    Parser parser(source);
    return parser.Parse(doc, errorOut);
}

} // namespace ui
