#include "include/GPath.h"
#include "include/GPathBuilder.h"
#include <algorithm>
#include <cmath>

static void include_point(GRect& r, GPoint p) {
    r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y);
    r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y);
}

static GPoint eval_curve(const GPoint p[], int n, float t) {
    float u = 1-t;
    if (n == 3) return p[0]*(u*u) + p[1]*(2*u*t) + p[2]*(t*t);
    return p[0]*(u*u*u) + p[1]*(3*u*u*t) + p[2]*(3*u*t*t) + p[3]*(t*t*t);
}

static void include_roots(GRect& r, const GPoint p[], float a, float b, float c) {
    if (std::abs(a) < 1e-7f) {
        if (std::abs(b) > 1e-7f) {
            float t = -c/b;
            if (t > 0 && t < 1) include_point(r, eval_curve(p, 4, t));
        }
        return;
    }
    float disc = b*b - 4*a*c;
    if (disc < 0) return;
    float root = std::sqrt(disc);
    float t0 = (-b-root)/(2*a), t1 = (-b+root)/(2*a);
    if (t0 > 0 && t0 < 1) include_point(r, eval_curve(p, 4, t0));
    if (t1 > 0 && t1 < 1) include_point(r, eval_curve(p, 4, t1));
}

GRect GPath::bounds() const {
    if (fPts.empty()) return GRect::WH(0, 0);
    GRect r = {fPts[0].x, fPts[0].y, fPts[0].x, fPts[0].y};
    GPath::Iter iter(*this);
    GPoint p[4];
    while (auto verb = iter.next(p)) {
        if (*verb == kMove) include_point(r, p[0]);
        if (*verb == kLine) include_point(r, p[1]);
        if (*verb == kQuad) {
            include_point(r, p[2]);
            for (int axis = 0; axis < 2; ++axis) {
                float a = axis ? p[0].y : p[0].x;
                float b = axis ? p[1].y : p[1].x;
                float c = axis ? p[2].y : p[2].x;
                float den = a - 2*b + c;
                if (den != 0) {
                    float t = (a-b)/den;
                    if (t > 0 && t < 1) include_point(r, eval_curve(p, 3, t));
                }
            }
        }
        if (*verb == kCubic) {
            include_point(r, p[3]);
            for (int axis = 0; axis < 2; ++axis) {
                float a = axis ? p[0].y : p[0].x;
                float b = axis ? p[1].y : p[1].x;
                float c = axis ? p[2].y : p[2].x;
                float d = axis ? p[3].y : p[3].x;
                include_roots(r, p, -a+3*b-3*c+d, 2*(a-2*b+c), b-a);
            }
        }
    }
    return r;
}

void GPathBuilder::addRect(const GRect& r, GPathDirection dir) {
    moveTo(r.left, r.top);
    if (dir == GPathDirection::kCW) {
        lineTo(r.right, r.top);
        lineTo(r.right, r.bottom);
        lineTo(r.left, r.bottom);
    } else {
        lineTo(r.left, r.bottom);
        lineTo(r.right, r.bottom);
        lineTo(r.right, r.top);
    }
}

void GPathBuilder::addPolygon(const GPoint pts[], int count) {
    if (count <= 0) return;
    moveTo(pts[0]);
    for (int i = 1; i < count; ++i) {
        lineTo(pts[i]);
    }
}

void GPathBuilder::addOval(const GRect& r, GPathDirection dir) {
    const float k = 0.55228475f;
    float cx = r.cx(), cy = r.cy(), rx = r.width()*0.5f, ry = r.height()*0.5f;
    moveTo(cx+rx, cy);
    if (dir == GPathDirection::kCW) {
        cubicTo(cx+rx, cy+k*ry, cx+k*rx, cy+ry, cx, cy+ry);
        cubicTo(cx-k*rx, cy+ry, cx-rx, cy+k*ry, cx-rx, cy);
        cubicTo(cx-rx, cy-k*ry, cx-k*rx, cy-ry, cx, cy-ry);
        cubicTo(cx+k*rx, cy-ry, cx+rx, cy-k*ry, cx+rx, cy);
    } else {
        cubicTo(cx+rx, cy-k*ry, cx+k*rx, cy-ry, cx, cy-ry);
        cubicTo(cx-k*rx, cy-ry, cx-rx, cy-k*ry, cx-rx, cy);
        cubicTo(cx-rx, cy+k*ry, cx-k*rx, cy+ry, cx, cy+ry);
        cubicTo(cx+k*rx, cy+ry, cx+rx, cy+k*ry, cx+rx, cy);
    }
}
