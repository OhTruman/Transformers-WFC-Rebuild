// Clean-room reconstruction — Scaleform GFx (SWF 8 / AS2) runtime: shared value types.
// Coordinates inside movie definitions are SWF twips (1/20 pixel of the movie stage).
#pragma once
#include <cmath>
#include <cstdint>

namespace gfx {

struct RGBA {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Point { float x = 0, y = 0; };

struct Rect {
    float xmin = 0, ymin = 0, xmax = 0, ymax = 0;
    bool empty() const { return xmax <= xmin || ymax <= ymin; }
    void expand(float x, float y) {
        if (empty() && xmin == 0 && xmax == 0 && ymin == 0 && ymax == 0) { xmin = xmax = x; ymin = ymax = y; return; }
        xmin = std::fmin(xmin, x); ymin = std::fmin(ymin, y); xmax = std::fmax(xmax, x); ymax = std::fmax(ymax, y);
    }
};

// SWF MATRIX: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Matrix {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Point apply(Point p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    // (this * o)(p) = this(o(p)).
    Matrix operator*(const Matrix& o) const {
        Matrix r;
        r.a = a * o.a + c * o.b;  r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;  r.d = b * o.c + d * o.d;
        r.tx = a * o.tx + c * o.ty + tx;
        r.ty = b * o.tx + d * o.ty + ty;
        return r;
    }
    Matrix inverse() const {
        float det = a * d - b * c;
        if (std::fabs(det) < 1e-12f) return Matrix{0, 0, 0, 0, -tx, -ty};
        float id = 1.0f / det;
        Matrix r;
        r.a = d * id; r.b = -b * id; r.c = -c * id; r.d = a * id;
        r.tx = -(r.a * tx + r.c * ty);
        r.ty = -(r.b * tx + r.d * ty);
        return r;
    }
    // Flash _xscale/_yscale/_rotation decomposition.
    float scaleX() const { return std::sqrt(a * a + b * b); }
    float scaleY() const { return std::sqrt(c * c + d * d); }
    float rotation() const { return std::atan2(b, a); }
};

// SWF CXFORMWITHALPHA: c' = c * mult + add (add in 0..255 colour units).
struct CXForm {
    float mr = 1, mg = 1, mb = 1, ma = 1;
    float ar = 0, ag = 0, ab = 0, aa = 0;
    // parent applied after child: (this o child)(c) = this(child(c)).
    CXForm operator*(const CXForm& ch) const {
        CXForm r;
        r.mr = mr * ch.mr; r.mg = mg * ch.mg; r.mb = mb * ch.mb; r.ma = ma * ch.ma;
        r.ar = mr * ch.ar + ar; r.ag = mg * ch.ag + ag; r.ab = mb * ch.ab + ab; r.aa = ma * ch.aa + aa;
        return r;
    }
    bool identity() const { return mr == 1 && mg == 1 && mb == 1 && ma == 1 && ar == 0 && ag == 0 && ab == 0 && aa == 0; }
};

} // namespace gfx
