// GFx TextField content (plain / HTML subset) and layout with the movies' embedded DefineFont3 outlines.
#include "ui/gfx/Display.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace gfx {

namespace {

std::u16string toU16(const std::string& s) {
    std::u16string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char ch = (unsigned char)s[i];
        uint32_t cp; int n;
        if (ch < 0x80) { cp = ch; n = 1; }
        else if ((ch & 0xE0) == 0xC0) { cp = ch & 0x1F; n = 2; }
        else if ((ch & 0xF0) == 0xE0) { cp = ch & 0x0F; n = 3; }
        else { cp = ch & 0x07; n = 4; }
        for (int k = 1; k < n && i + (size_t)k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3F);
        i += (size_t)n;
        o.push_back((char16_t)(cp > 0xFFFF ? 0xFFFD : cp));
    }
    return o;
}

std::string toU8(const std::u16string& s) {
    std::string o;
    for (char16_t ch : s) {
        uint32_t cp = ch;
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    return o;
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

RGBA parseColor(const std::string& v) {
    std::string s = v;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    uint32_t c = (uint32_t)std::strtoul(s.c_str(), nullptr, 16);
    RGBA r; r.r = (uint8_t)(c >> 16); r.g = (uint8_t)(c >> 8); r.b = (uint8_t)c; r.a = 255;
    return r;
}

// Attribute map of one tag.
std::map<std::string, std::string> attrs(const std::string& tag) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < tag.size() && !std::isspace((unsigned char)tag[i])) ++i;
    while (i < tag.size()) {
        while (i < tag.size() && std::isspace((unsigned char)tag[i])) ++i;
        size_t k = i;
        while (i < tag.size() && tag[i] != '=' && !std::isspace((unsigned char)tag[i])) ++i;
        std::string key = lower(tag.substr(k, i - k));
        while (i < tag.size() && std::isspace((unsigned char)tag[i])) ++i;
        std::string val;
        if (i < tag.size() && tag[i] == '=') {
            ++i;
            while (i < tag.size() && std::isspace((unsigned char)tag[i])) ++i;
            if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
                char q = tag[i++];
                size_t e = tag.find(q, i);
                val = tag.substr(i, e == std::string::npos ? std::string::npos : e - i);
                i = e == std::string::npos ? tag.size() : e + 1;
            } else {
                size_t e = i;
                while (e < tag.size() && !std::isspace((unsigned char)tag[e])) ++e;
                val = tag.substr(i, e - i);
                i = e;
            }
        }
        if (!key.empty()) out[key] = val;
    }
    return out;
}

std::string decodeEntities(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        size_t e = s.find(';', i);
        if (e == std::string::npos || e - i > 8) { o += s[i]; continue; }
        std::string ent = s.substr(i + 1, e - i - 1);
        std::u16string u;
        if (ent == "amp") o += '&';
        else if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (ent == "nbsp") o += "\xC2\xA0";
        else if (!ent.empty() && ent[0] == '#') {
            long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::strtol(ent.c_str() + 2, nullptr, 16) : std::strtol(ent.c_str() + 1, nullptr, 10);
            u.push_back((char16_t)cp);
            o += toU8(u);
        } else { o += s.substr(i, e - i + 1); }
        i = e;
    }
    return o;
}

} // namespace

void TextField::setPlainText(const std::string& utf8) {
    chars = toU16(utf8);
    for (auto& c : chars) if (c == '\n') c = '\r';
    formats = {newFormat};
    charFormat.assign(chars.size(), 0);
    htmlSource.clear();
    layoutDirty = true;
}

void TextField::setHtmlText(const std::string& html) {
    htmlSource = html;
    chars.clear();
    charFormat.clear();
    formats.clear();
    std::vector<TextFormatSpan> stack{newFormat};
    auto pushChar = [&](char16_t c) {
        const TextFormatSpan& f = stack.back();
        if (formats.empty() || !(formats.back().font == f.font && formats.back().size == f.size && formats.back().color.r == f.color.r &&
                                 formats.back().color.g == f.color.g && formats.back().color.b == f.color.b && formats.back().align == f.align &&
                                 formats.back().letterSpacing == f.letterSpacing && formats.back().bold == f.bold &&
                                 formats.back().italic == f.italic && formats.back().leading == f.leading &&
                                 formats.back().kerning == f.kerning))
            formats.push_back(f);
        chars.push_back(c);
        charFormat.push_back((int)formats.size() - 1);
    };
    bool pendingParagraph = false;
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] == '<') {
            size_t e = html.find('>', i);
            if (e == std::string::npos) break;
            std::string tag = html.substr(i + 1, e - i - 1);
            i = e + 1;
            bool close = !tag.empty() && tag[0] == '/';
            std::string name = lower(close ? tag.substr(1) : tag);
            size_t sp = name.find_first_of(" \t\r\n/");
            if (sp != std::string::npos) name = name.substr(0, sp);
            if (name == "p" || name == "li") {
                if (close) { pendingParagraph = true; if (stack.size() > 1) stack.pop_back(); }
                else {
                    if (pendingParagraph || (!chars.empty() && chars.back() != '\r')) pushChar('\r');
                    pendingParagraph = false;
                    TextFormatSpan f = stack.back();
                    auto a = attrs(tag);
                    if (a.count("align")) { std::string al = lower(a["align"]); f.align = al == "right" ? 1 : al == "center" ? 2 : al == "justify" ? 3 : 0; }
                    stack.push_back(f);
                }
            } else if (name == "br") {
                pushChar('\r');
            } else if (name == "font" || name == "textformat" || name == "b" || name == "i" || name == "u" || name == "a" || name == "span") {
                if (close) { if (stack.size() > 1) stack.pop_back(); continue; }
                if (pendingParagraph) { pushChar('\r'); pendingParagraph = false; }
                TextFormatSpan f = stack.back();
                auto a = attrs(tag);
                if (name == "b") f.bold = true;
                if (name == "i") f.italic = true;
                if (name == "u") f.underline = true;
                if (a.count("face")) f.font = a["face"];
                if (a.count("size")) {
                    std::string s = a["size"];
                    if (!s.empty() && (s[0] == '+' || s[0] == '-')) f.size += (float)std::atof(s.c_str());
                    else f.size = (float)std::atof(s.c_str());
                }
                if (a.count("color")) f.color = parseColor(a["color"]);
                if (a.count("letterspacing")) f.letterSpacing = (float)std::atof(a["letterspacing"].c_str());
                if (a.count("kerning")) f.kerning = a["kerning"] == "1" || lower(a["kerning"]) == "true";
                if (a.count("leading")) f.leading = (float)std::atof(a["leading"].c_str());
                if (a.count("leftmargin")) f.leftMargin = (float)std::atof(a["leftmargin"].c_str());
                if (a.count("rightmargin")) f.rightMargin = (float)std::atof(a["rightmargin"].c_str());
                if (a.count("indent")) f.indent = (float)std::atof(a["indent"].c_str());
                stack.push_back(f);
            }
            continue;
        }
        size_t e = html.find('<', i);
        std::string text = decodeEntities(html.substr(i, e == std::string::npos ? std::string::npos : e - i));
        if (text.size() > 1 && text[0] == '$') text = player->translate(text);   // GFx translates text runs
        i = e == std::string::npos ? html.size() : e;
        if (text.empty()) continue;
        if (pendingParagraph) { pushChar('\r'); pendingParagraph = false; }
        std::u16string u = toU16(text);
        for (char16_t c : u) {
            if (condenseWhite && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                if (!chars.empty() && chars.back() == ' ') continue;
                c = ' ';
            } else if (c == '\n') c = '\r';
            pushChar(c);
        }
    }
    if (formats.empty()) formats.push_back(newFormat);
    layoutDirty = true;
}

std::string TextField::plainText() const {
    std::u16string s = chars;
    return toU8(s);
}

std::string TextField::htmlText() const {
    // Flash-style generated HTML (one <P> per paragraph, one <FONT> run per format change).
    std::string out;
    const char* al[] = {"LEFT", "RIGHT", "CENTER", "JUSTIFY"};
    size_t i = 0;
    do {
        int fi = chars.empty() ? 0 : charFormat[std::min(i, chars.size() - 1)];
        const TextFormatSpan& pf = formats.empty() ? newFormat : formats[(size_t)fi];
        out += std::string("<P ALIGN=\"") + al[std::max(0, std::min(3, pf.align))] + "\">";
        int cur = -1;
        std::u16string run;
        auto flush = [&]() {
            if (cur < 0) return;
            const TextFormatSpan& f = formats[(size_t)cur];
            char col[16];
            std::snprintf(col, sizeof col, "#%02X%02X%02X", f.color.r, f.color.g, f.color.b);
            std::string t = toU8(run), esc;
            for (char c : t) { if (c == '<') esc += "&lt;"; else if (c == '>') esc += "&gt;"; else if (c == '&') esc += "&amp;"; else esc += c; }
            out += "<FONT FACE=\"" + f.font + "\" SIZE=\"" + std::to_string((int)f.size) + "\" COLOR=\"" + col +
                   "\" LETTERSPACING=\"" + std::to_string((int)f.letterSpacing) + "\" KERNING=\"" + (f.kerning ? "1" : "0") + "\">" + esc + "</FONT>";
            run.clear();
        };
        while (i < chars.size() && chars[i] != '\r') {
            if (charFormat[i] != cur) { flush(); cur = charFormat[i]; }
            run.push_back(chars[i]);
            ++i;
        }
        flush();
        out += "</P>";
        if (i < chars.size()) ++i; else break;
    } while (i <= chars.size());
    return out;
}

void TextField::layout() {
    if (!layoutDirty) return;
    layoutDirty = false;
    glyphs.clear();
    const float gutter = 2.0f * 20.0f;
    struct Item { char16_t c; int fmt; const FontDef* font; int glyph; float adv; float size; int sub = -1; };
    std::vector<Item> items;
    items.reserve(chars.size());
    float shrink = 1.0f;
    for (int pass = 0; pass < 12; ++pass) {
        items.clear();
        for (size_t i = 0; i < chars.size(); ++i) {
            const TextFormatSpan& f = formats[(size_t)std::max(0, std::min((int)formats.size() - 1, charFormat[i]))];
            const FontDef* font = player->resolveFont(f.font, f.bold, f.italic, def.get());
            float size = f.size * 20.0f * shrink;
            // GFx image substitution: the substring becomes one inline image item.
            int subHit = -1;
            for (size_t k = 0; k < imageSubs.size(); ++k) {
                const std::u16string& key = imageSubs[k].key;
                if (!key.empty() && chars.compare(i, key.size(), key) == 0) { subHit = (int)k; break; }
            }
            if (subHit >= 0) {
                Item im{0xFFFC, charFormat[i], font, -1, imageSubs[(size_t)subHit].w * 20.0f * shrink, size, subHit};
                items.push_back(im);
                i += imageSubs[(size_t)subHit].key.size() - 1;
                continue;
            }
            Item it{chars[i], charFormat[i], font, -1, 0, size};
            char16_t c = chars[i] == 0xA0 ? ' ' : chars[i];
            if (font) {
                auto g = font->codeToGlyph.find(c);
                if (g != font->codeToGlyph.end()) {
                    it.glyph = g->second;
                    float adv = (size_t)it.glyph < font->advances.size() ? font->advances[(size_t)it.glyph] : font->emSize() * 0.5f;
                    it.adv = adv * size / font->emSize();
                } else if (c == ' ') {
                    it.adv = size * 0.25f;
                }
            }
            it.adv += f.letterSpacing * 20.0f * shrink;
            if (f.kerning && font && i + 1 < chars.size()) {
                auto k = font->kerning.find(((uint32_t)c << 16) | chars[i + 1]);
                if (k != font->kerning.end()) it.adv += k->second * size / font->emSize();
            }
            items.push_back(it);
        }
        // Line breaking.
        struct Line { size_t b, e; float width; float ascent, descent, leading; int align; float indent, lm, rm; };
        std::vector<Line> lines;
        float maxW = bounds.xmax - bounds.xmin - 2 * gutter;
        size_t start = 0;
        auto metrics = [&](size_t b, size_t e, Line& L) {
            L.ascent = L.descent = L.leading = 0;
            for (size_t k = b; k < e; ++k) {
                const Item& it = items[k];
                if (!it.font) continue;
                float s = it.size / it.font->emSize();
                L.ascent = std::max(L.ascent, it.font->ascent * s);
                L.descent = std::max(L.descent, it.font->descent * s);
                L.leading = std::max(L.leading, formats[(size_t)it.fmt].leading * 20.0f);
            }
            if (L.ascent == 0 && !formats.empty()) {
                const TextFormatSpan& f = formats[(size_t)(b < items.size() ? items[b].fmt : 0)];
                const FontDef* font = player->resolveFont(f.font, f.bold, f.italic, def.get());
                float size = f.size * 20.0f * shrink;
                if (font) { L.ascent = font->ascent * size / font->emSize(); L.descent = font->descent * size / font->emSize(); }
                else { L.ascent = size * 0.8f; L.descent = size * 0.2f; }
                L.leading = f.leading * 20.0f;
            }
        };
        while (start <= items.size()) {
            size_t e = start;
            float w = 0;
            size_t lastBreak = std::string::npos;
            const TextFormatSpan& pf = formats[(size_t)(start < items.size() ? items[start].fmt : (formats.empty() ? 0 : charFormat.empty() ? 0 : charFormat.back()))];
            float avail = maxW - (pf.leftMargin + pf.rightMargin + pf.indent) * 20.0f;
            while (e < items.size() && items[e].c != '\r') {
                if (items[e].c == ' ') lastBreak = e;
                if (wordWrap && w + items[e].adv > avail && e > start) {
                    if (lastBreak != std::string::npos && lastBreak > start) { e = lastBreak; break; }
                    break;
                }
                w += items[e].adv;
                ++e;
            }
            Line L{start, e, 0, 0, 0, 0, pf.align, pf.indent * 20.0f, pf.leftMargin * 20.0f, pf.rightMargin * 20.0f};
            for (size_t k = start; k < e; ++k) L.width += items[k].adv;
            // trailing spaces do not count for alignment
            for (size_t k = e; k > start && items[k - 1].c == ' '; --k) L.width -= items[k - 1].adv;
            metrics(start, e, L);
            lines.push_back(L);
            if (e >= items.size()) break;
            start = (items[e].c == '\r' || items[e].c == ' ') ? e + 1 : e;
            if (start == items.size() && items[e].c == '\r') { Line E = L; E.b = E.e = start; E.width = 0; lines.push_back(E); break; }
        }
        // autoSize (no word wrap): the box follows the text width; left keeps x, right the right edge, center the middle.
        textWidth = 0;
        for (const Line& L : lines) textWidth = std::max(textWidth, L.width + L.lm + L.rm + L.indent);
        float boxMin = bounds.xmin, boxMax = bounds.xmax;
        if (autoSize != "none" && autoSize != "false" && !wordWrap) {
            float w = textWidth + 2 * gutter, oldW = bounds.xmax - bounds.xmin;
            float dx = autoSize == "right" ? oldW - w : autoSize == "center" ? (oldW - w) * 0.5f : 0.0f;
            boxMin = bounds.xmin + dx;
            boxMax = boxMin + w;
        }
        // Positions.
        float y = gutter;
        std::vector<GlyphRun> out;
        for (size_t li = 0; li < lines.size(); ++li) {
            Line& L = lines[li];
            y += L.ascent;
            float boxW = boxMax - boxMin;
            float x = gutter + L.lm + L.indent;
            float inner = boxW - 2 * gutter - L.lm - L.rm - L.indent;
            if (L.align == 1) x += inner - L.width;
            else if (L.align == 2) x += (inner - L.width) * 0.5f;
            for (size_t k = L.b; k < L.e; ++k) {
                const Item& it = items[k];
                if (it.sub >= 0) {
                    // Image baseline (baseLineY in the image's own pixels) sits on the text baseline.
                    const ImageSub& is = imageSubs[(size_t)it.sub];
                    GlyphRun g;
                    float scaleY = is.natH > 0 ? is.h / is.natH : 1.0f;
                    g.image = is.path; g.imgW = is.w * shrink; g.imgH = is.h * shrink;
                    g.x = boxMin + x; g.y = bounds.ymin + y - is.baseLineY * scaleY * 20.0f * shrink;
                    out.push_back(g);
                } else if (it.glyph >= 0 && it.font) {
                    GlyphRun g;
                    g.font = it.font; g.glyph = it.glyph; g.x = boxMin + x; g.y = bounds.ymin + y; g.size = it.size;
                    g.color = formats[(size_t)it.fmt].color;
                    out.push_back(g);
                }
                x += it.adv;
            }
            y += L.descent + (li + 1 < lines.size() ? L.leading : 0);
        }
        textHeight = y - gutter;
        float fieldH = bounds.ymax - bounds.ymin;
        if (textAutoSize == "shrink" && (textHeight + 2 * gutter > fieldH || textWidth + 2 * gutter > bounds.xmax - bounds.xmin) && shrink > 0.3f) {
            shrink *= 0.92f;
            continue;
        }
        glyphs.swap(out);
        if (autoSize != "none" && autoSize != "false") {
            bounds.xmin = boxMin;
            bounds.xmax = boxMax;
            bounds.ymax = bounds.ymin + textHeight + 2 * gutter;
        }
        break;
    }
    if (verticalAlign == "center" || verticalAlign == "bottom") {
        float fieldH = bounds.ymax - bounds.ymin;
        float off = fieldH - (textHeight + 2 * gutter);
        if (verticalAlign == "center") off *= 0.5f;
        if (off > 0) for (auto& g : glyphs) g.y += off;
    }
    if (border || background) {
        boxShape = std::make_shared<ShapeDef>();
        boxShape->bounds = bounds;
        boxShape->fillSets.emplace_back();
        boxShape->lineSets.emplace_back();
        ShapePath p;
        p.pts = {{bounds.xmin, bounds.ymin}, {bounds.xmax, bounds.ymin}, {bounds.xmax, bounds.ymax}, {bounds.xmin, bounds.ymax}, {bounds.xmin, bounds.ymin}};
        if (background) { FillStyle f; f.color = backgroundColor; boxShape->fillSets[0].push_back(f); p.fill1 = 1; }
        if (border) { LineStyle l; l.width = 20; l.color = borderColor; boxShape->lineSets[0].push_back(l); p.line = 1; }
        boxShape->paths.push_back(p);
    } else boxShape.reset();
}

} // namespace gfx
