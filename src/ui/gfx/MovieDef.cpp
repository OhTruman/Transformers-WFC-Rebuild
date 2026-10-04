#include "ui/gfx/MovieDef.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gfx {

namespace {

// Little-endian byte + MSB-first bit reader over one tag body.
class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    size_t pos() const { return pos_; }
    void seek(size_t p) { pos_ = p; bit_ = 0; }
    bool eof() const { return pos_ >= n_; }
    size_t size() const { return n_; }
    const uint8_t* data() const { return d_; }
    void align() { if (bit_) { bit_ = 0; ++pos_; } }
    uint8_t u8() { align(); return pos_ < n_ ? d_[pos_++] : 0; }
    uint16_t u16() { align(); uint16_t v = 0; if (pos_ + 2 <= n_) v = (uint16_t)(d_[pos_] | (d_[pos_ + 1] << 8)); pos_ += 2; return v; }
    int16_t s16() { return (int16_t)u16(); }
    uint32_t u32() { align(); uint32_t v = 0; if (pos_ + 4 <= n_) std::memcpy(&v, d_ + pos_, 4); pos_ += 4; return v; }
    float f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    float fixed16() { return (float)(int32_t)u32() / 65536.0f; }
    float fixed8() { return (float)s16() / 256.0f; }
    std::string str() {
        align();
        size_t s = pos_;
        while (pos_ < n_ && d_[pos_]) ++pos_;
        std::string r((const char*)d_ + s, pos_ - s);
        if (pos_ < n_) ++pos_;
        return r;
    }
    std::string pstr() {   // u8 length-prefixed (GFx tags)
        uint8_t len = u8();
        std::string r;
        if (pos_ + len <= n_) r.assign((const char*)d_ + pos_, len);
        pos_ += len;
        return r;
    }
    uint32_t ub(int nbits) {
        uint32_t v = 0;
        for (int i = 0; i < nbits; ++i) {
            if (pos_ >= n_) return v;
            v = (v << 1) | ((d_[pos_] >> (7 - bit_)) & 1);
            if (++bit_ == 8) { bit_ = 0; ++pos_; }
        }
        return v;
    }
    int32_t sb(int nbits) {
        if (nbits == 0) return 0;
        uint32_t v = ub(nbits);
        if (v & (1u << (nbits - 1))) v |= ~0u << nbits;
        return (int32_t)v;
    }
    float fb(int nbits) { return (float)sb(nbits) / 65536.0f; }

    Rect rect() {
        align();
        int n = (int)ub(5);
        Rect r;
        r.xmin = (float)sb(n); r.xmax = (float)sb(n); r.ymin = (float)sb(n); r.ymax = (float)sb(n);
        align();
        return r;
    }
    Matrix matrix() {
        align();
        Matrix m;
        if (ub(1)) { int n = (int)ub(5); m.a = fb(n); m.d = fb(n); }
        if (ub(1)) { int n = (int)ub(5); m.b = fb(n); m.c = fb(n); }
        int n = (int)ub(5);
        m.tx = (float)sb(n); m.ty = (float)sb(n);
        align();
        return m;
    }
    CXForm cxform(bool alpha) {
        align();
        CXForm c;
        bool hasAdd = ub(1), hasMult = ub(1);
        int n = (int)ub(4);
        if (hasMult) {
            c.mr = sb(n) / 256.0f; c.mg = sb(n) / 256.0f; c.mb = sb(n) / 256.0f;
            if (alpha) c.ma = sb(n) / 256.0f;
        }
        if (hasAdd) {
            c.ar = (float)sb(n); c.ag = (float)sb(n); c.ab = (float)sb(n);
            if (alpha) c.aa = (float)sb(n);
        }
        align();
        return c;
    }
    RGBA rgb() { RGBA c; c.r = u8(); c.g = u8(); c.b = u8(); c.a = 255; return c; }
    RGBA rgba() { RGBA c; c.r = u8(); c.g = u8(); c.b = u8(); c.a = u8(); return c; }

private:
    const uint8_t* d_;
    size_t n_;
    size_t pos_ = 0;
    int bit_ = 0;
};

// Quadratic curve flattening: segments by control-polygon length / tolerance.
void flattenQuad(std::vector<Point>& out, Point p0, Point c, Point p1, float tol) {
    float len = std::hypot(c.x - p0.x, c.y - p0.y) + std::hypot(p1.x - c.x, p1.y - c.y);
    int n = (int)std::ceil(len / tol);
    n = std::max(2, std::min(32, n));
    for (int i = 1; i <= n; ++i) {
        float t = (float)i / n, u = 1 - t;
        out.push_back({u * u * p0.x + 2 * u * t * c.x + t * t * p1.x, u * u * p0.y + 2 * u * t * c.y + t * t * p1.y});
    }
}

bool readFillStyle(Reader& r, int shapeVer, FillStyle& fs) {
    fs.type = r.u8();
    if (fs.type == FillStyle::Solid) {
        fs.color = shapeVer >= 3 ? r.rgba() : r.rgb();
    } else if (fs.type == FillStyle::Linear || fs.type == FillStyle::Radial || fs.type == FillStyle::Focal) {
        fs.m = r.matrix();
        r.align();
        fs.spread = (uint8_t)r.ub(2);
        fs.interp = (uint8_t)r.ub(2);
        int n = (int)r.ub(4);
        for (int i = 0; i < n; ++i) {
            GradStop g;
            g.ratio = r.u8();
            g.color = shapeVer >= 3 ? r.rgba() : r.rgb();
            fs.grad.push_back(g);
        }
        if (fs.type == FillStyle::Focal) fs.focal = r.fixed8();
    } else if (fs.isBitmap()) {
        fs.bitmapId = r.u16();
        fs.m = r.matrix();
    } else {
        return false;
    }
    return true;
}

bool readStyles(Reader& r, int shapeVer, std::vector<FillStyle>& fills, std::vector<LineStyle>& lines) {
    int n = r.u8();
    if (n == 0xFF && shapeVer >= 2) n = r.u16();
    for (int i = 0; i < n; ++i) {
        FillStyle fs;
        if (!readFillStyle(r, shapeVer, fs)) return false;
        fills.push_back(fs);
    }
    n = r.u8();
    if (n == 0xFF && shapeVer >= 2) n = r.u16();
    for (int i = 0; i < n; ++i) {
        LineStyle ls;
        ls.width = r.u16();
        if (shapeVer >= 4) {
            r.align();
            ls.startCap = (uint8_t)r.ub(2);
            ls.join = (uint8_t)r.ub(2);
            ls.hasFill = r.ub(1);
            ls.noHScale = r.ub(1);
            ls.noVScale = r.ub(1);
            ls.pixelHinting = r.ub(1);
            r.ub(5);
            ls.noClose = r.ub(1);
            ls.endCap = (uint8_t)r.ub(2);
            if (ls.join == 2) r.u16();   // miter limit factor (8.8)
            if (ls.hasFill) { if (!readFillStyle(r, shapeVer, ls.fill)) return false; ls.color = ls.fill.color; }
            else ls.color = r.rgba();
        } else {
            ls.color = shapeVer >= 3 ? r.rgba() : r.rgb();
        }
        lines.push_back(ls);
    }
    return true;
}

bool parseShapeBody(Reader& r, int shapeVer, ShapeDef& out, bool withStyles, bool glyph) {
    if (withStyles) {
        out.fillSets.emplace_back();
        out.lineSets.emplace_back();
        if (!readStyles(r, shapeVer, out.fillSets.back(), out.lineSets.back())) return false;
    }
    r.align();
    int fillBits = (int)r.ub(4), lineBits = (int)r.ub(4);
    float x = 0, y = 0;
    int set = 0, f0 = 0, f1 = 0, ln = 0;
    const float tol = glyph ? 400.0f : 40.0f;   // glyph units (EM 20480) / stage twips
    ShapePath* cur = nullptr;
    auto newPath = [&]() {
        out.paths.emplace_back();
        cur = &out.paths.back();
        cur->styleSet = set; cur->fill0 = f0; cur->fill1 = f1; cur->line = ln;
        cur->pts.push_back({x, y});
    };
    for (;;) {
        if (r.eof()) break;
        bool edge = r.ub(1);
        if (!edge) {
            uint32_t flags = r.ub(5);
            if (flags == 0) break;   // EndShapeRecord
            bool newStyles = flags & 0x10, lineFlag = flags & 0x08, fill1Flag = flags & 0x04, fill0Flag = flags & 0x02,
                 moveTo = flags & 0x01;
            if (moveTo) { int n = (int)r.ub(5); x = (float)r.sb(n); y = (float)r.sb(n); }
            if (fill0Flag) f0 = (int)r.ub(fillBits);
            if (fill1Flag) f1 = (int)r.ub(fillBits);
            if (lineFlag) ln = (int)r.ub(lineBits);
            if (newStyles && shapeVer >= 2) {
                out.fillSets.emplace_back();
                out.lineSets.emplace_back();
                if (!readStyles(r, shapeVer, out.fillSets.back(), out.lineSets.back())) return false;
                set = (int)out.fillSets.size() - 1;
                r.align();
                fillBits = (int)r.ub(4); lineBits = (int)r.ub(4);
            }
            if (glyph) { if (!f0 && !f1) f0 = 1; }
            newPath();
        } else {
            if (!cur) newPath();
            bool straight = r.ub(1);
            int n = (int)r.ub(4) + 2;
            if (straight) {
                float dx = 0, dy = 0;
                if (r.ub(1)) { dx = (float)r.sb(n); dy = (float)r.sb(n); }
                else if (r.ub(1)) dy = (float)r.sb(n);
                else dx = (float)r.sb(n);
                x += dx; y += dy;
                cur->pts.push_back({x, y});
            } else {
                float cx = x + (float)r.sb(n), cy = y + (float)r.sb(n);
                float ax = cx + (float)r.sb(n), ay = cy + (float)r.sb(n);
                flattenQuad(cur->pts, {x, y}, {cx, cy}, {ax, ay}, tol);
                x = ax; y = ay;
            }
        }
    }
    r.align();
    // Drop degenerate paths (style-only records).
    out.paths.erase(std::remove_if(out.paths.begin(), out.paths.end(), [](const ShapePath& p) { return p.pts.size() < 2; }),
                    out.paths.end());
    return true;
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// Shape records without flattening (morph shapes interpolate them first). Straight edges become curves with the
// control point at the midpoint so start and end records pair up.
void parseRawRecords(Reader& r, std::vector<MorphRec>& out) {
    r.align();
    int fillBits = (int)r.ub(4), lineBits = (int)r.ub(4);
    float x = 0, y = 0;
    for (;;) {
        if (r.eof()) break;
        if (!r.ub(1)) {
            uint32_t flags = r.ub(5);
            if (!flags) break;
            if (flags & 0x01) { int n = (int)r.ub(5); x = (float)r.sb(n); y = (float)r.sb(n); MorphRec m; m.kind = MorphRec::Move; m.x = x; m.y = y; out.push_back(m); }
            MorphRec st; st.kind = MorphRec::Style; st.fill0 = st.fill1 = st.line = -1;
            bool any = false;
            if (flags & 0x02) { st.fill0 = (int)r.ub(fillBits); any = true; }
            if (flags & 0x04) { st.fill1 = (int)r.ub(fillBits); any = true; }
            if (flags & 0x08) { st.line = (int)r.ub(lineBits); any = true; }
            if (any) out.push_back(st);
        } else {
            bool straight = r.ub(1);
            int n = (int)r.ub(4) + 2;
            MorphRec e;
            if (straight) {
                float dx = 0, dy = 0;
                if (r.ub(1)) { dx = (float)r.sb(n); dy = (float)r.sb(n); }
                else if (r.ub(1)) dy = (float)r.sb(n);
                else dx = (float)r.sb(n);
                e.kind = MorphRec::Line;
                e.cx = x + dx * 0.5f; e.cy = y + dy * 0.5f;
                x += dx; y += dy;
            } else {
                e.kind = MorphRec::Curve;
                e.cx = x + (float)r.sb(n); e.cy = y + (float)r.sb(n);
                x = e.cx + (float)r.sb(n); y = e.cy + (float)r.sb(n);
            }
            e.x = x; e.y = y;
            out.push_back(e);
        }
    }
    r.align();
}

RGBA lerpC(const RGBA& a, const RGBA& b, float t) {
    RGBA c;
    c.r = (uint8_t)(a.r + (b.r - a.r) * t); c.g = (uint8_t)(a.g + (b.g - a.g) * t);
    c.b = (uint8_t)(a.b + (b.b - a.b) * t); c.a = (uint8_t)(a.a + (b.a - a.a) * t);
    return c;
}
Matrix lerpM(const Matrix& a, const Matrix& b, float t) {
    return {a.a + (b.a - a.a) * t, a.b + (b.b - a.b) * t, a.c + (b.c - a.c) * t, a.d + (b.d - a.d) * t, a.tx + (b.tx - a.tx) * t, a.ty + (b.ty - a.ty) * t};
}

} // namespace

const ShapeDef* MorphDef::at(uint16_t ratio) const {
    auto it = cache.find(ratio);
    if (it != cache.end()) return it->second.get();
    float t = ratio / 65535.0f;
    auto sh = std::make_unique<ShapeDef>();
    sh->bounds = {startBounds.xmin + (endBounds.xmin - startBounds.xmin) * t, startBounds.ymin + (endBounds.ymin - startBounds.ymin) * t,
                  startBounds.xmax + (endBounds.xmax - startBounds.xmax) * t, startBounds.ymax + (endBounds.ymax - startBounds.ymax) * t};
    sh->fillSets.emplace_back();
    sh->lineSets.emplace_back();
    for (size_t i = 0; i < startFills.size() && i < endFills.size(); ++i) {
        FillStyle f = startFills[i];
        const FillStyle& e = endFills[i];
        f.color = lerpC(f.color, e.color, t);
        f.m = lerpM(f.m, e.m, t);
        for (size_t g = 0; g < f.grad.size() && g < e.grad.size(); ++g) {
            f.grad[g].color = lerpC(f.grad[g].color, e.grad[g].color, t);
            f.grad[g].ratio = (uint8_t)(f.grad[g].ratio + (e.grad[g].ratio - f.grad[g].ratio) * t);
        }
        sh->fillSets[0].push_back(f);
    }
    for (size_t i = 0; i < startLines.size() && i < endLines.size(); ++i) {
        LineStyle l = startLines[i];
        l.width += (endLines[i].width - l.width) * t;
        l.color = lerpC(l.color, endLines[i].color, t);
        sh->lineSets[0].push_back(l);
    }
    size_t ei = 0;
    int f0 = 0, f1 = 0, ln = 0;
    float x = 0, y = 0;
    ShapePath* cur = nullptr;
    auto newPath = [&]() { sh->paths.emplace_back(); cur = &sh->paths.back(); cur->fill0 = f0; cur->fill1 = f1; cur->line = ln; cur->pts.push_back({x, y}); };
    for (const MorphRec& a : start) {
        if (a.kind == MorphRec::Style) {
            if (a.fill0 >= 0) f0 = a.fill0;
            if (a.fill1 >= 0) f1 = a.fill1;
            if (a.line >= 0) ln = a.line;
            newPath();
            continue;
        }
        while (ei < end.size() && end[ei].kind == MorphRec::Style) ++ei;
        const MorphRec* b = ei < end.size() ? &end[ei] : &a;
        ++ei;
        float nx = a.x + (b->x - a.x) * t, ny = a.y + (b->y - a.y) * t;
        if (a.kind == MorphRec::Move) { x = nx; y = ny; newPath(); continue; }
        if (!cur) newPath();
        float cx = a.cx + (b->cx - a.cx) * t, cy = a.cy + (b->cy - a.cy) * t;
        flattenQuad(cur->pts, {x, y}, {cx, cy}, {nx, ny}, 40.0f);
        x = nx; y = ny;
    }
    sh->paths.erase(std::remove_if(sh->paths.begin(), sh->paths.end(), [](const ShapePath& q) { return q.pts.size() < 2; }), sh->paths.end());
    const ShapeDef* raw = sh.get();
    cache[ratio] = std::move(sh);
    return raw;
}

bool parseShapeRecords(const uint8_t* data, size_t size, size_t& pos, int shapeVersion, ShapeDef& out, bool glyph) {
    Reader r(data, size);
    r.seek(pos);
    bool ok = parseShapeBody(r, shapeVersion, out, false, glyph);
    pos = r.pos();
    return ok;
}

std::string SpriteDef::label(int frame) const {
    for (const auto& [k, v] : labels) if (v == frame) return k;
    return {};
}

std::string MovieDef::dir() const {
    size_t s = path_.find_last_of("/\\");
    return s == std::string::npos ? "." : path_.substr(0, s);
}

const SpriteDef* MovieDef::sprite(uint16_t id) const {
    const CharDef* c = character(id);
    return c && c->type == CharType::Sprite ? &sprites[(size_t)c->index] : nullptr;
}

int MovieDef::findFont(const std::string& name, bool bold, bool italic) const {
    int best = -1;
    std::string ln = lower(name);
    for (size_t i = 0; i < fonts.size(); ++i) {
        if (lower(fonts[i].name) != ln) continue;
        if (fonts[i].bold == bold && fonts[i].italic == italic) return (int)i;
        if (best < 0) best = (int)i;
    }
    return best;
}

namespace {

struct Parser {
    MovieDef& m;
    const std::vector<uint8_t>& buf;

    void addChar(uint16_t id, CharType t, int index) { CharDef c; c.type = t; c.index = index; m.chars[id] = c; }

    void parseFont(Reader& r, int code) {
        uint16_t id = r.u16();
        FontDef f;
        f.font3 = code == 75;
        uint8_t flags = r.u8();
        f.hasLayout = flags & 0x80;
        bool wideOffsets = flags & 0x08;
        f.wideCodes = flags & 0x04;
        f.italic = flags & 0x02;
        f.bold = flags & 0x01;
        r.u8();   // language code
        uint8_t nameLen = r.u8();
        for (int i = 0; i < nameLen; ++i) { char c = (char)r.u8(); if (c) f.name += c; }
        int n = r.u16();
        size_t tableStart = r.pos();
        std::vector<uint32_t> offsets(n);
        for (int i = 0; i < n; ++i) offsets[i] = wideOffsets ? r.u32() : r.u16();
        uint32_t codeTableOffset = n > 0 || true ? (wideOffsets ? r.u32() : r.u16()) : 0;
        for (int i = 0; i < n; ++i) {
            ShapeDef g;
            size_t p = tableStart + offsets[i];
            Reader gr(r.data(), r.size());
            gr.seek(p);
            parseShapeBody(gr, 1, g, false, true);
            f.glyphs.push_back(std::move(g));
        }
        r.seek(tableStart + codeTableOffset);
        for (int i = 0; i < n; ++i) {
            uint16_t c = f.wideCodes ? r.u16() : r.u8();
            f.codes.push_back(c);
            f.codeToGlyph[c] = i;
        }
        if (f.hasLayout) {
            f.ascent = r.u16();
            f.descent = r.u16();
            f.leading = r.s16();
            for (int i = 0; i < n; ++i) f.advances.push_back(r.s16());
            for (int i = 0; i < n; ++i) r.rect();
            int k = r.u16();
            for (int i = 0; i < k; ++i) {
                uint16_t a = f.wideCodes ? r.u16() : r.u8();
                uint16_t b = f.wideCodes ? r.u16() : r.u8();
                float adj = r.s16();
                f.kerning[((uint32_t)a << 16) | b] = adj;
            }
        }
        m.fonts.push_back(std::move(f));
        addChar(id, CharType::Font, (int)m.fonts.size() - 1);
    }

    void parseEditText(Reader& r) {
        uint16_t id = r.u16();
        EditTextDef e;
        e.bounds = r.rect();
        uint8_t f1 = r.u8(), f2 = r.u8();
        e.hasText = f1 & 0x80; e.wordWrap = f1 & 0x40; e.multiline = f1 & 0x20; e.password = f1 & 0x10;
        e.readOnly = f1 & 0x08; e.hasColor = f1 & 0x04; e.hasMaxLength = f1 & 0x02; e.hasFont = f1 & 0x01;
        e.hasFontClass = f2 & 0x80; e.autoSize = f2 & 0x40; e.hasLayout = f2 & 0x20; e.noSelect = f2 & 0x10;
        e.border = f2 & 0x08; e.wasStatic = f2 & 0x04; e.html = f2 & 0x02; e.useOutlines = f2 & 0x01;
        if (e.hasFont) e.fontId = r.u16();
        if (e.hasFontClass) e.fontClass = r.str();
        if (e.hasFont || e.hasFontClass) e.height = r.u16();
        if (e.hasColor) e.color = r.rgba();
        if (e.hasMaxLength) e.maxLength = r.u16();
        if (e.hasLayout) {
            e.align = r.u8();
            e.leftMargin = r.u16(); e.rightMargin = r.u16(); e.indent = r.u16(); e.leading = r.s16();
        }
        e.variable = r.str();
        if (e.hasText) e.initialText = r.str();
        m.editTexts.push_back(std::move(e));
        addChar(id, CharType::EditText, (int)m.editTexts.size() - 1);
    }

    void parseText(Reader& r, int code) {
        uint16_t id = r.u16();
        StaticTextDef t;
        t.bounds = r.rect();
        t.m = r.matrix();
        int glyphBits = r.u8(), advBits = r.u8();
        TextRecord cur;
        for (;;) {
            uint8_t flags = r.u8();
            if (flags == 0 || r.eof()) break;
            TextRecord rec;
            rec.fontId = cur.fontId; rec.color = cur.color; rec.height = cur.height;
            if (flags & 0x08) { rec.fontId = r.u16(); rec.hasFont = true; }
            if (flags & 0x04) { rec.color = code == 33 ? r.rgba() : r.rgb(); rec.hasColor = true; }
            if (flags & 0x01) { rec.x = r.s16(); rec.hasX = true; }
            if (flags & 0x02) { rec.y = r.s16(); rec.hasY = true; }
            if (flags & 0x08) rec.height = r.u16();
            int n = r.u8();
            for (int i = 0; i < n; ++i) {
                TextRecordGlyph g;
                g.glyph = (int)r.ub(glyphBits);
                g.advance = (float)r.sb(advBits);
                rec.glyphs.push_back(g);
            }
            r.align();
            cur = rec;
            t.records.push_back(rec);
        }
        m.staticTexts.push_back(std::move(t));
        addChar(id, CharType::StaticText, (int)m.staticTexts.size() - 1);
    }

    void parsePlace(Reader& r, int code, Frame& fr) {
        ControlTag ct;
        ct.kind = ControlTag::Place;
        PlaceCmd& p = ct.place;
        uint8_t f1 = r.u8(), f2 = code == 70 ? r.u8() : 0;
        p.hasClipActions = f1 & 0x80; p.hasClipDepth = f1 & 0x40; p.hasName = f1 & 0x20; p.hasRatio = f1 & 0x10;
        p.hasCx = f1 & 0x08; p.hasMatrix = f1 & 0x04; p.hasChar = f1 & 0x02; p.move = f1 & 0x01;
        bool hasImage = f2 & 0x10, hasClassName = f2 & 0x08, hasCache = f2 & 0x04, hasBlend = f2 & 0x02, hasFilters = f2 & 0x01;
        p.depth = r.u16();
        if (hasClassName || (hasImage && p.hasChar)) { p.className = r.str(); p.hasClassName = true; }
        if (p.hasChar) p.charId = r.u16();
        if (p.hasMatrix) p.m = r.matrix();
        if (p.hasCx) p.cx = r.cxform(true);
        if (p.hasRatio) p.ratio = r.u16();
        if (p.hasName) p.name = r.str();
        if (p.hasClipDepth) p.clipDepth = r.u16();
        if (hasFilters) {
            int n = r.u8();
            for (int i = 0; i < n && !r.eof(); ++i) {
                int id = r.u8();
                switch (id) {
                case 0: r.seek(r.pos() + 23); break;
                case 1: r.seek(r.pos() + 9); break;
                case 2: r.seek(r.pos() + 15); break;
                case 3: r.seek(r.pos() + 27); break;
                case 4: case 7: { int c = r.u8(); r.seek(r.pos() + 5 * c + 19); break; }
                case 5: { int x = r.u8(), y = r.u8(); r.seek(r.pos() + 8 + 4 * x * y + 5); break; }
                case 6: r.seek(r.pos() + 80); break;
                default: i = n; break;
                }
            }
        }
        if (hasBlend) { p.blend = r.u8(); p.hasBlend = true; }
        if (hasCache) r.u8();
        if (p.hasClipActions) {
            r.u16();
            r.u32();   // all event flags (SWF6+)
            for (;;) {
                uint32_t ev = r.u32();
                if (ev == 0 || r.eof()) break;
                uint32_t size = r.u32();
                ClipAction ca;
                ca.events = ev;
                size_t start = r.pos();
                if (ev & 0x00020000u) { ca.keyCode = r.u8(); }   // ClipEventKeyPress (in the byte order of the flags)
                size_t codeStart = r.pos();
                ca.code.code = std::make_shared<std::vector<uint8_t>>(r.data() + codeStart, r.data() + std::min(r.size(), start + size));
                r.seek(start + size);
                p.clipActions.push_back(ca);
            }
        }
        fr.tags.push_back(std::move(ct));
    }

    void parseTimeline(const uint8_t* d, size_t n, SpriteDef& sp, bool isRoot) {
        Reader r(d, n);
        sp.frames.emplace_back();
        while (!r.eof()) {
            uint16_t h = r.u16();
            int code = h >> 6;
            uint32_t len = h & 0x3F;
            if (len == 0x3F) len = r.u32();
            size_t start = r.pos();
            if (start + len > n) break;
            Reader t(d + start, len);
            Frame& fr = sp.frames.back();
            switch (code) {
            case 0: r.seek(n); continue;
            case 1: sp.frames.emplace_back(); break;   // ShowFrame
            case 4: case 26: case 70:
                if (code == 4) { m.unsupported[4] = {"PlaceObject", ""}; break; }
                parsePlace(t, code, fr);
                break;
            case 5: { ControlTag ct; ct.kind = ControlTag::Remove; t.u16(); ct.depth = t.u16(); fr.tags.push_back(ct); break; }
            case 28: { ControlTag ct; ct.kind = ControlTag::Remove; ct.depth = t.u16(); fr.tags.push_back(ct); break; }
            case 12: { ActionBlock a; a.code = std::make_shared<std::vector<uint8_t>>(d + start, d + start + len); fr.actions.push_back(a); break; }
            case 59: {
                uint16_t sid = t.u16();
                ActionBlock a;
                a.code = std::make_shared<std::vector<uint8_t>>(d + start + 2, d + start + len);
                fr.initActions.push_back({sid, a});
                break;
            }
            case 43: sp.labels[t.str()] = (int)sp.frames.size() - 1; break;
            case 9: if (isRoot) m.background = t.rgb(); break;
            case 39: {
                if (!isRoot) break;
                uint16_t id = t.u16();
                SpriteDef s;
                s.frameCount = t.u16();
                parseTimeline(d + start + 4, len - 4, s, false);
                m.sprites.push_back(std::move(s));
                addChar(id, CharType::Sprite, (int)m.sprites.size() - 1);
                break;
            }
            case 2: case 22: case 32: case 83: {
                uint16_t id = t.u16();
                ShapeDef s;
                s.bounds = t.rect();
                int ver = code == 2 ? 1 : code == 22 ? 2 : code == 32 ? 3 : 4;
                if (ver == 4) { t.rect(); t.u8(); }
                if (!parseShapeBody(t, ver, s, true, false))
                    LOG_WARN("GFX %s: shape %u parse error", m.baseName().c_str(), id);
                m.shapes.push_back(std::move(s));
                addChar(id, CharType::Shape, (int)m.shapes.size() - 1);
                break;
            }
            case 37: parseEditText(t); break;
            case 11: case 33: parseText(t, code); break;
            case 48: case 75: parseFont(t, code); break;
            case 56: {
                int c = t.u16();
                for (int i = 0; i < c; ++i) { uint16_t id = t.u16(); std::string nm = t.str(); m.exports[nm] = id; m.exportNames[id] = nm; }
                break;
            }
            case 57: case 71: {
                std::string url = t.str();
                if (code == 71) { t.u8(); t.u8(); }
                int c = t.u16();
                for (int i = 0; i < c; ++i) {
                    ImportDef im; im.url = url; im.id = t.u16(); im.name = t.str();
                    CharDef cd; cd.type = CharType::Imported; cd.importUrl = url; cd.importName = im.name;
                    m.chars[im.id] = cd;
                    m.imports.push_back(im);
                }
                break;
            }
            case 46: {   // DefineMorphShape
                uint16_t id = t.u16();
                auto md = std::make_unique<MorphDef>();
                md->startBounds = t.rect();
                md->endBounds = t.rect();
                uint32_t off = t.u32();
                size_t endPos = t.pos() + off;
                int n = t.u8();
                if (n == 0xFF) n = t.u16();
                bool ok = true;
                for (int i = 0; i < n && ok; ++i) {
                    FillStyle a, b;
                    a.type = b.type = t.u8();
                    if (a.type == FillStyle::Solid) { a.color = t.rgba(); b.color = t.rgba(); }
                    else if (a.isGradient()) {
                        a.m = t.matrix(); b.m = t.matrix();
                        int g = t.u8() & 0x0F;
                        for (int k = 0; k < g; ++k) {
                            GradStop sa, sb;
                            sa.ratio = t.u8(); sa.color = t.rgba(); sb.ratio = t.u8(); sb.color = t.rgba();
                            a.grad.push_back(sa); b.grad.push_back(sb);
                        }
                    } else if (a.isBitmap()) { a.bitmapId = b.bitmapId = t.u16(); a.m = t.matrix(); b.m = t.matrix(); }
                    else ok = false;
                    md->startFills.push_back(a); md->endFills.push_back(b);
                }
                n = t.u8();
                if (n == 0xFF) n = t.u16();
                for (int i = 0; i < n && ok; ++i) {
                    LineStyle a, b;
                    a.width = t.u16(); b.width = t.u16(); a.color = t.rgba(); b.color = t.rgba();
                    md->startLines.push_back(a); md->endLines.push_back(b);
                }
                if (ok) {
                    parseRawRecords(t, md->start);
                    t.seek(endPos);
                    parseRawRecords(t, md->end);
                }
                m.morphs.push_back(std::move(md));
                addChar(id, CharType::Morph, (int)m.morphs.size() - 1);
                break;
            }
            case 1001: case 1009: {
                uint32_t id = code == 1009 ? t.u32() : t.u16();
                BitmapDef b;
                t.u16();   // bitmap format
                b.targetWidth = t.u16();
                b.targetHeight = t.u16();
                b.exportName = t.pstr();
                b.fileName = t.pstr();
                std::string png = b.fileName;
                size_t dot = png.rfind('.');
                if (dot != std::string::npos) png = png.substr(0, dot);
                b.resolvedPath = m.dir() + "/" + png + ".png";
                m.bitmaps.push_back(b);
                addChar((uint16_t)id, CharType::Bitmap, (int)m.bitmaps.size() - 1);
                break;
            }
            case 69: case 77: case 1000: case 1002: case 1003: case 1004: case 74: case 73: case 88: case 78: case 24: case 64: case 65:
                break;   // FileAttributes, Metadata, ExporterInfo, FontTextureInfo, ExternalGradient, GradientMap, CSMTextSettings,
                         // FontAlignZones, FontName, ScalingGrid, Protect, EnableDebugger2, ScriptLimits
            default:
                if (!m.unsupported.count((uint16_t)code)) m.unsupported[(uint16_t)code] = {"tag", std::to_string(len)};
                break;
            }
            r.seek(start + len);
        }
        if (!sp.frames.empty() && sp.frames.back().tags.empty() && sp.frames.back().actions.empty() &&
            sp.frames.back().initActions.empty() && (int)sp.frames.size() > std::max(1, sp.frameCount))
            sp.frames.pop_back();
        if (sp.frameCount <= 0) sp.frameCount = (int)sp.frames.size();
        while ((int)sp.frames.size() < sp.frameCount) sp.frames.emplace_back();
    }
};

} // namespace

bool MovieDef::load(const std::string& path) {
    path_ = path;
    size_t s = path.find_last_of("/\\");
    base_ = s == std::string::npos ? path : path.substr(s + 1);
    size_t dot = base_.rfind('.');
    if (dot != std::string::npos) base_ = base_.substr(0, dot);
    std::ifstream f(path, std::ios::binary);
    if (!f) { LOG_WARN("GFX cannot open %s", path.c_str()); return false; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 21) return false;
    char sig[4] = {(char)buf[0], (char)buf[1], (char)buf[2], 0};
    if (std::strcmp(sig, "GFX") != 0 && std::strcmp(sig, "FWS") != 0) {
        LOG_WARN("GFX %s: unsupported signature %s (compressed movies are not supported)", path.c_str(), sig);
        return false;
    }
    version = buf[3];
    Reader r(buf.data(), buf.size());
    r.seek(8);
    stage = r.rect();
    frameRate = r.u16() / 256.0f;
    root.frameCount = r.u16();
    Parser p{*this, buf};
    p.parseTimeline(buf.data() + r.pos(), buf.size() - r.pos(), root, true);
    std::string un;
    for (const auto& [code, note] : unsupported) un += " " + std::to_string(code);
    LOG_INFO("GFX loaded %s: stage %.0fx%.0f @%.0f fps, %d frames, %zu shapes, %zu sprites, %zu texts, %zu fonts, %zu bitmaps, %zu exports%s%s",
             base_.c_str(), (stage.xmax - stage.xmin) / 20, (stage.ymax - stage.ymin) / 20, frameRate, root.frameCount,
             shapes.size(), sprites.size(), editTexts.size(), fonts.size(), bitmaps.size(), exports.size(),
             un.empty() ? "" : ", unsupported tags:", un.c_str());
    return true;
}

} // namespace gfx
