#include "include/GCanvas.h"
#include "include/GBitmap.h"
#include "include/GColor.h"
#include "include/GRect.h"
#include "include/GPixel.h"
#include "include/GPoint.h"
#include "include/GMath.h"
#include "include/GShader.h"
#include "include/GPath.h"
#include "include/GPathBuilder.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {
int pixel_boundary(float x, int limit) {
    return static_cast<int>(std::floor(std::clamp(x, 0.0f, static_cast<float>(limit)) + 0.5f));
}

struct Edge {
    GPoint a, b;
    int top, bottom;
    float slope;
    int winding;
    float xAt(float y) const { return a.x + (y - a.y) * slope; }
};

void add_edge(std::vector<Edge>& edges, GPoint a, GPoint b, int height, int winding = 1) {
    if (a.y > b.y) std::swap(a, b);
    int top = pixel_boundary(a.y, height);
    int bottom = pixel_boundary(b.y, height);
    if (top >= bottom) return;
    edges.push_back({a, b, top, bottom, (b.x-a.x)/(b.y-a.y), winding});
}

void clip_edge(std::vector<Edge>& edges, GPoint a, GPoint b, int width, int height, int winding = 1) {
    if (a.y > b.y) std::swap(a,b);
    if (b.y <= 0 || a.y >= height || a.y == b.y) return;
    const float slope = (b.x-a.x)/(b.y-a.y);
    if (a.y < 0) a = {a.x + (0-a.y)*slope, 0};
    if (b.y > height) b = {b.x + (height-b.y)*slope, static_cast<float>(height)};

    if (a.x > b.x) std::swap(a,b);
    const float w = static_cast<float>(width);
    if (b.x <= 0) { add_edge(edges,{0,a.y},{0,b.y},height,winding); return; }
    if (a.x >= w) { add_edge(edges,{w,a.y},{w,b.y},height,winding); return; }
    if (a.x < 0) {
        GPoint q = {0, a.y + (b.y-a.y)*(0-a.x)/(b.x-a.x)};
        add_edge(edges,{0,a.y},q,height,winding);
        a = q;
    }
    if (b.x > w) {
        GPoint q = {w, a.y + (b.y-a.y)*(w-a.x)/(b.x-a.x)};
        add_edge(edges,q,{w,b.y},height,winding);
        b = q;
    }
    add_edge(edges,a,b,height,winding);
}
}

class MyCanvas : public GCanvas {

    using BlendProc = GPixel (*)(GPixel, GPixel);

    static BlendProc find_blend(GBlendMode mode) {
        static const BlendProc blendProcs[] = {
            blend_clear,
            blend_src,
            blend_dst,
            blend_src_over,
            blend_dst_over,
            blend_src_in,
            blend_dst_in,
            blend_src_out,
            blend_dst_out,
            blend_src_atop,
            blend_dst_atop,
            blend_xor
        };
        return blendProcs[static_cast<int>(mode)];
    }

public:
    MyCanvas(const GBitmap& bmap) : bmap(bmap) {}

    void save() override {
        matrixStack.push_back(ctm);
    }

    void restore() override {
        assert(!matrixStack.empty());
        ctm = matrixStack.back();
        matrixStack.pop_back();
    }

    void concat(const GMatrix& matrix) override {
        ctm = ctm * matrix;
    }

    static unsigned RoundedDivide255(unsigned product) {
        const unsigned coeff = (1 << 16) | (1 << 8) | 1;
        return (product * coeff + (1 << 23)) >> 24;
    }

    static GPixel blend_src(GPixel src, GPixel dst) {
        return src;
    }

    static GPixel blend_dst(GPixel src, GPixel dst) {
        return dst;
    }

    static GPixel blend_clear(GPixel src, GPixel dst){
        return GPixel_PackARGB(0,0,0,0);
    }

    static GPixel blend_src_in(GPixel src, GPixel dst){
        unsigned dst_alpha = GPixel_GetA(dst);
        return GPixel_PackARGB(
            RoundedDivide255(GPixel_GetA(src)*dst_alpha),
            RoundedDivide255(GPixel_GetR(src)*dst_alpha),
            RoundedDivide255(GPixel_GetG(src)*dst_alpha),
            RoundedDivide255(GPixel_GetB(src)*dst_alpha)
        );
    }

    static GPixel blend_src_over(GPixel src, GPixel dst){
        unsigned left_over = 255 - GPixel_GetA(src);

        return GPixel_PackARGB(
            GPixel_GetA(src) + RoundedDivide255(left_over * GPixel_GetA(dst)),
            GPixel_GetR(src) + RoundedDivide255(left_over * GPixel_GetR(dst)),
            GPixel_GetG(src) + RoundedDivide255(left_over * GPixel_GetG(dst)),
            GPixel_GetB(src) + RoundedDivide255(left_over * GPixel_GetB(dst))
        );
    };

    static GPixel blend_dst_over(GPixel src, GPixel dst){
        unsigned left_over = 255 - GPixel_GetA(dst);

        return GPixel_PackARGB(
            GPixel_GetA(dst) + RoundedDivide255(left_over*GPixel_GetA(src)),
            GPixel_GetR(dst) + RoundedDivide255(left_over*GPixel_GetR(src)),
            GPixel_GetG(dst) + RoundedDivide255(left_over*GPixel_GetG(src)),
            GPixel_GetB(dst) + RoundedDivide255(left_over*GPixel_GetB(src))
        );
    }

    static GPixel blend_dst_in(GPixel src, GPixel dst){
        unsigned src_alpha = GPixel_GetA(src);

        return GPixel_PackARGB(
            RoundedDivide255(GPixel_GetA(dst)*src_alpha),
            RoundedDivide255(GPixel_GetR(dst)*src_alpha),
            RoundedDivide255(GPixel_GetG(dst)*src_alpha),
            RoundedDivide255(GPixel_GetB(dst)*src_alpha)
        );
    }

    static GPixel blend_src_out(GPixel src, GPixel dst){
        unsigned dst_alpha = 255 - GPixel_GetA(dst);
        return GPixel_PackARGB(
            RoundedDivide255(GPixel_GetA(src)*dst_alpha),
            RoundedDivide255(GPixel_GetR(src)*dst_alpha),
            RoundedDivide255(GPixel_GetG(src)*dst_alpha),
            RoundedDivide255(GPixel_GetB(src)*dst_alpha)
        );
    }

    static GPixel blend_dst_out(GPixel src, GPixel dst){
        unsigned src_alpha = 255 - GPixel_GetA(src);
        return GPixel_PackARGB(
            RoundedDivide255(GPixel_GetA(dst)*src_alpha),
            RoundedDivide255(GPixel_GetR(dst)*src_alpha),
            RoundedDivide255(GPixel_GetG(dst)*src_alpha),
            RoundedDivide255(GPixel_GetB(dst)*src_alpha)
        );
    }

    static GPixel blend_src_atop(GPixel src, GPixel dst) {
        unsigned sa = GPixel_GetA(src);
        unsigned da = GPixel_GetA(dst);

        return GPixel_PackARGB(
            RoundedDivide255(da * sa + (255 - sa) * da),
            RoundedDivide255(da * GPixel_GetR(src) + (255 - sa) * GPixel_GetR(dst)),
            RoundedDivide255(da * GPixel_GetG(src) + (255 - sa) * GPixel_GetG(dst)),
            RoundedDivide255(da * GPixel_GetB(src) + (255 - sa) * GPixel_GetB(dst))
        );
    }

    static GPixel blend_dst_atop(GPixel src, GPixel dst) {
        unsigned sa = GPixel_GetA(src);
        unsigned da = GPixel_GetA(dst);

        return GPixel_PackARGB(
            RoundedDivide255(sa * da + (255 - da) * sa),
            RoundedDivide255(sa * GPixel_GetR(dst) + (255 - da) * GPixel_GetR(src)),
            RoundedDivide255(sa * GPixel_GetG(dst) + (255 - da) * GPixel_GetG(src)),
            RoundedDivide255(sa * GPixel_GetB(dst) + (255 - da) * GPixel_GetB(src))
        );
    }

    static GPixel blend_xor(GPixel src, GPixel dst) {
        unsigned sa = GPixel_GetA(src);
        unsigned da = GPixel_GetA(dst);

        return GPixel_PackARGB(
            RoundedDivide255((255 - sa) * da + (255 - da) * sa),
            RoundedDivide255((255 - sa) * GPixel_GetR(dst) + (255 - da) * GPixel_GetR(src)),
            RoundedDivide255((255 - sa) * GPixel_GetG(dst) + (255 - da) * GPixel_GetG(src)),
            RoundedDivide255((255 - sa) * GPixel_GetB(dst) + (255 - da) * GPixel_GetB(src))
        );
    }
    static GBlendMode simplify_mode(GBlendMode mode, GPixel src) {
        const unsigned a = GPixel_GetA(src);
        if (a == 0) {
            switch (mode) {
                case GBlendMode::kClear: case GBlendMode::kSrc:
                case GBlendMode::kSrcIn: case GBlendMode::kDstIn:
                case GBlendMode::kSrcOut: case GBlendMode::kDstATop:
                    return GBlendMode::kClear;
                default: return GBlendMode::kDst;
            }
        }
        if (a == 255) {
            switch (mode) {
                case GBlendMode::kSrcOver: return GBlendMode::kSrc;
                case GBlendMode::kDstIn: return GBlendMode::kDst;
                case GBlendMode::kDstOut: return GBlendMode::kClear;
                case GBlendMode::kSrcATop: return GBlendMode::kSrcIn;
                case GBlendMode::kDstATop: return GBlendMode::kDstOver;
                case GBlendMode::kXor: return GBlendMode::kSrcOut;
                default: break;
            }
        }
        return mode;
    }

    static void blend_span(GPixel* dst, int count, GPixel src, size_t stride, BlendProc proc) {
        if (proc == blend_dst) return;
        if (proc == blend_src || proc == blend_clear) {
            GPixel value = proc == blend_clear ? 0 : src;
            if (stride == 1) {
                std::fill_n(dst, count, value);
            } else {
                for (int i = 0; i < count; ++i)
                    dst[static_cast<size_t>(i) * stride] = value;
            }
            return;
        }
        if (proc == blend_src_over) {
            for (int i = 0; i < count; ++i) {
                GPixel* pixel = dst + static_cast<size_t>(i) * stride;
                *pixel = blend_src_over(src, *pixel);
            }
            return;
        }
        for (int i = 0; i < count; ++i) {
            GPixel* pixel = dst + static_cast<size_t>(i) * stride;
            *pixel = proc(src, *pixel);
        }
    }

    static GPixel color_to_pixel(const GColor& color) {
        return GPixel_PackARGB(
            GRoundToInt(color.a * 255),
            GRoundToInt(color.r * color.a * 255),
            GRoundToInt(color.g * color.a * 255),
            GRoundToInt(color.b * color.a * 255));
    }

    void blend_paint_span(int x, int y, int count, GPixel solid, GShader::Context* context, BlendProc proc) {
        GPixel* dst = bmap.getAddr(x, y);
        if (context == nullptr) {
            blend_span(dst, count, solid, 1, proc);
            return;
        }

        shaderRow.resize(static_cast<size_t>(count));
        context->shadeRow(x, y, count, shaderRow.data());
        if (proc == blend_src) {
            std::copy_n(shaderRow.data(), count, dst);
        } else if (proc == blend_src_over) {
            for (int i = 0; i < count; ++i) {
                dst[i] = blend_src_over(shaderRow[i], dst[i]);
            }
        } else {
            for (int i = 0; i < count; ++i) {
                dst[i] = proc(shaderRow[i], dst[i]);
            }
        }
    }

    void blend_paint_pixel(int x, int y, GPixel solid, GShader::Context* context, BlendProc proc) {
        GPixel* dst = bmap.getAddr(x, y);
        GPixel src = solid;
        if (context != nullptr) {
            context->shadeRow(x, y, 1, &src);
        }
        *dst = proc(src, *dst);
    }

    void drawRect(const GRect& rect, const GPaint& paint) override {
        if (ctm[1] == 0 && ctm[2] == 0) {
            GPoint corners[] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
            ctm.mapPoints(corners, 2);

            const int startX = pixel_boundary(std::min(corners[0].x, corners[1].x), bmap.width());
            const int endX = pixel_boundary(std::max(corners[0].x, corners[1].x), bmap.width());
            const int startY = pixel_boundary(std::min(corners[0].y, corners[1].y), bmap.height());
            const int endY = pixel_boundary(std::max(corners[0].y, corners[1].y), bmap.height());
            if (startX >= endX || startY >= endY) return;

            const GPixel src = color_to_pixel(paint.color());
            GShader* shader = paint.peekShader();
            GBlendMode mode = paint.blendMode();
            if (shader == nullptr) {
                mode = simplify_mode(mode, src);
            } else if (shader->isOpaque()) {
                mode = simplify_mode(mode, GPixel_PackARGB(255, 0, 0, 0));
            }
            if (mode == GBlendMode::kDst) return;
            BlendProc proc = find_blend(mode);

            std::unique_ptr<GShader::Context> context;
            if (shader != nullptr && mode != GBlendMode::kClear) {
                context = shader->makeContext(ctm);
                if (context == nullptr) return;
            }

            for (int y = startY; y < endY; ++y)
                blend_paint_span(startX, y, endX - startX, src, context.get(), proc);
            return;
        }

        const GPoint points[] = {{rect.left, rect.top}, {rect.right, rect.top},
                                 {rect.right, rect.bottom}, {rect.left, rect.bottom}};
        drawConvexPolygon(points, 4, paint);
    }

    void drawConvexPolygon(const GPoint points[], int count, const GPaint& paint) override {
        if (count < 3 || bmap.width() == 0 || bmap.height() == 0) return;
        const GPixel src = color_to_pixel(paint.color());
        GShader* shader = paint.peekShader();
        GBlendMode mode = paint.blendMode();
        if (shader == nullptr) {
            mode = simplify_mode(mode, src);
        } else if (shader->isOpaque()) {
            mode = simplify_mode(mode, GPixel_PackARGB(255, 0, 0, 0));
        }
        if (mode == GBlendMode::kDst) return;
        BlendProc proc = find_blend(mode);

        std::unique_ptr<GShader::Context> context;
        if (shader != nullptr && mode != GBlendMode::kClear) {
            context = shader->makeContext(ctm);
            if (context == nullptr) return;
        }

        std::vector<GPoint> devicePoints(static_cast<size_t>(count));
        ctm.mapPoints(devicePoints.data(), points, count);
        std::vector<Edge> edges;
        edges.reserve(static_cast<size_t>(count)*3);
        for (int i = 0; i < count; ++i)
            clip_edge(edges, devicePoints[i], devicePoints[(i+1)%count], bmap.width(), bmap.height());
        if (edges.empty()) return;
        int top = bmap.height(), bottom = 0;
        for (const Edge& edge : edges) {
            top = std::min(top,edge.top);
            bottom = std::max(bottom,edge.bottom);
        }

        std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            return a.top < b.top;
        });
        std::vector<const Edge*> active;
        active.reserve(edges.size());
        size_t next = 0;
        for (int y = top; y < bottom; ++y) {
            active.erase(std::remove_if(active.begin(), active.end(), [y](const Edge* e) {
                return e->bottom <= y;
            }), active.end());
            while (next < edges.size() && edges[next].top <= y)
                active.push_back(&edges[next++]);
            float left = std::numeric_limits<float>::infinity();
            float right = -std::numeric_limits<float>::infinity();
            for (const Edge* edge : active) {
                float x = edge->xAt(static_cast<float>(y)+0.5f);
                left = std::min(left,x);
                right = std::max(right,x);
            }
            if (left < right) {
                int startX = pixel_boundary(left, bmap.width());
                int endX = pixel_boundary(right, bmap.width());
                if (startX < endX)
                    blend_paint_span(startX, y, endX-startX, src, context.get(), proc);
            }
        }
    }

    void drawPath(const GPath& path, const GPaint& paint) override {
        if (path.countPoints() < 3 || bmap.width() == 0 || bmap.height() == 0) return;
        const GPixel src = color_to_pixel(paint.color());
        GShader* shader = paint.peekShader();
        GBlendMode mode = paint.blendMode();
        if (shader == nullptr) {
            mode = simplify_mode(mode, src);
        } else if (shader->isOpaque()) {
            mode = simplify_mode(mode, GPixel_PackARGB(255, 0, 0, 0));
        }
        if (mode == GBlendMode::kDst) return;
        BlendProc proc = find_blend(mode);
        std::unique_ptr<GShader::Context> context;
        if (shader != nullptr && mode != GBlendMode::kClear) {
            context = shader->makeContext(ctm);
            if (context == nullptr) return;
        }

        std::vector<Edge> edges;
        edges.reserve(path.countPoints()*3);
        GPath::Edger edger(path);
        GPoint p[4];
        while (auto verb = edger.next(p)) {
            int count = *verb == kQuad ? 3 : (*verb == kCubic ? 4 : 2);
            ctm.mapPoints(p, count);
            int segments = 1;
            float tol = paint.curveTolerance();
            if (*verb == kQuad) {
                GPoint e = (p[0] - p[1]*2 + p[2])/4;
                segments = std::max(1, (int)std::ceil(std::sqrt(e.length()/tol)));
            } else if (*verb == kCubic) {
                GPoint e0 = p[0] - p[1]*2 + p[2];
                GPoint e1 = p[1] - p[2]*2 + p[3];
                GPoint e = {std::max(std::abs(e0.x), std::abs(e1.x)),
                            std::max(std::abs(e0.y), std::abs(e1.y))};
                segments = std::max(1, (int)std::ceil(std::sqrt(3*e.length()/(4*tol))));
            }
            GPoint a = p[0];
            for (int i = 1; i <= segments; ++i) {
                float t = float(i)/segments, u = 1-t;
                GPoint b = p[1];
                if (*verb == kQuad) b = p[0]*(u*u) + p[1]*(2*u*t) + p[2]*(t*t);
                if (*verb == kCubic) b = p[0]*(u*u*u) + p[1]*(3*u*u*t) + p[2]*(3*u*t*t) + p[3]*(t*t*t);
                int winding = a.y < b.y ? 1 : -1;
                clip_edge(edges, a, b, bmap.width(), bmap.height(), winding);
                a = b;
            }
        }
        if (edges.empty()) return;
        std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            if (a.top != b.top) return a.top < b.top;
            return a.xAt(a.top + 0.5f) < b.xAt(b.top + 0.5f);
        });
        int bottom = 0;
        for (const Edge& edge : edges) {
            bottom = std::max(bottom, edge.bottom);
        }
        std::vector<const Edge*> active;
        active.reserve(edges.size());
        size_t next = 0;
        for (int y = edges[0].top; y < bottom; ++y) {
            active.erase(std::remove_if(active.begin(), active.end(), [y](const Edge* e) {
                return e->bottom <= y;
            }), active.end());
            while (next < edges.size() && edges[next].top <= y) {
                active.push_back(&edges[next++]);
            }
            const float yc = y + 0.5f;
            std::sort(active.begin(), active.end(), [yc](const Edge* a, const Edge* b) {
                return a->xAt(yc) < b->xAt(yc);
            });
            int winding = 0;
            int startX = 0;
            for (const Edge* edge : active) {
                int x = pixel_boundary(edge->xAt(yc), bmap.width());
                if (winding == 0) startX = x;
                winding += edge->winding;
                if (winding == 0 && startX < x) {
                    blend_paint_span(startX, y, x-startX, src, context.get(), proc);
                }
            }
            assert(winding == 0);
        }
    }

    void drawLine(GPoint p1, GPoint p2, const GPaint& paint) override {

        const float localDx = p2.x - p1.x;
        const float localDy = p2.y - p1.y;
        if (!paint.isHairline()) {
            const float length = std::hypot(localDx, localDy);
            if (length == 0 || paint.lineWidth() == 0) return;
            const float scale = paint.lineWidth() / (2 * length);
            const GPoint offset = {-localDy * scale, localDx * scale};
            GPoint start = p1, end = p2;
            if (paint.capType() == GCapType::kSquare) {
                GPoint along = {localDx*scale, localDy*scale};
                start -= along;
                end += along;
            }
            const GPoint corners[] = {start + offset, end + offset, end - offset, start - offset};
            drawConvexPolygon(corners, 4, paint);
            if (paint.capType() == GCapType::kRound) {
                GPathBuilder builder;
                float radius = paint.lineWidth()*0.5f;
                builder.addCircle(p1, radius);
                builder.addCircle(p2, radius);
                drawPath(*builder.detach(), paint);
            }
            return;
        }

        GPoint devicePoints[] = {p1, p2};
        ctm.mapPoints(devicePoints, 2);
        p1 = devicePoints[0];
        p2 = devicePoints[1];

        const float dx = p2.x - p1.x;
        const float dy = p2.y - p1.y;

        const float adx = std::abs(dx);
        const float ady = std::abs(dy);

        const GPixel src = color_to_pixel(paint.color());
        GShader* shader = paint.peekShader();
        GBlendMode mode = paint.blendMode();
        if (shader == nullptr) {
            mode = simplify_mode(mode, src);
        } else if (shader->isOpaque()) {
            mode = simplify_mode(mode, GPixel_PackARGB(255, 0, 0, 0));
        }
        if (mode == GBlendMode::kDst) return;
        BlendProc proc = find_blend(mode);
        std::unique_ptr<GShader::Context> context;
        if (shader != nullptr && mode != GBlendMode::kClear) {
            context = shader->makeContext(ctm);
            if (context == nullptr) return;
        }

        if (dy == 0 && dx != 0) {
            if (p1.y < 0 || p1.y >= bmap.height()) return;
            int left = pixel_boundary(std::min(p1.x, p2.x), bmap.width());
            int right = pixel_boundary(std::max(p1.x, p2.x), bmap.width());
            if (left < right)
                blend_paint_span(left, GFloorToInt(p1.y), right-left, src, context.get(), proc);
            return;
        }
        if (dx == 0 && dy != 0) {
            if (p1.x < 0 || p1.x >= bmap.width()) return;
            int top = pixel_boundary(std::min(p1.y, p2.y), bmap.height());
            int bottom = pixel_boundary(std::max(p1.y, p2.y), bmap.height());
            if (top < bottom) {
                const int x = GFloorToInt(p1.x);
                for (int y = top; y < bottom; ++y)
                    blend_paint_pixel(x, y, src, context.get(), proc);
            }
            return;
        }

        if (adx == 0 && ady == 0) {
            const int x = GFloorToInt(p1.x);
            const int y = GFloorToInt(p1.y);
            if (x >= 0 && x < bmap.width() && y >= 0 && y < bmap.height()) {
                blend_paint_pixel(x, y, src, context.get(), proc);
            }
            return;
        }

        if (ady > adx) {
            const float slope = dx / dy;
            const float yMin = std::min(p1.y, p2.y);
            const float yMax = std::max(p1.y, p2.y);
            int start = std::max(0, GFloorToInt(yMin + 0.5f));
            int end = std::min(bmap.height() - 1, GFloorToInt(yMax + 0.5f));
            for (int y = start; y <= end; ++y) {
                const float yCenter = y + 0.5f;
                if (yCenter <= yMin || yCenter > yMax){
                    continue;
                }
                const float centerX = p1.x + (yCenter - p1.y) * slope;
                const int x = GFloorToInt(centerX);
                if (x >= 0 && x < bmap.width()) {
                    blend_paint_pixel(x, y, src, context.get(), proc);
                }
            }
        } else {
            const float slope = dy / dx;
            const float xMin = std::min(p1.x, p2.x);
            const float xMax = std::max(p1.x, p2.x);
            int start = std::max(0, GFloorToInt(xMin + 0.5f));
            int end = std::min(bmap.width() - 1, GFloorToInt(xMax + 0.5f));
            for (int x = start; x <= end; ++x) {
                const float xCenter = x + 0.5f;
                if (xCenter <= xMin || xCenter > xMax) continue;
                const float centerY = p1.y + (xCenter - p1.x) * slope;
                const int y = GFloorToInt(centerY);
                if (y >= 0 && y < bmap.height()) {
                    blend_paint_pixel(x, y, src, context.get(), proc);
                }
            }
        }
    }
    void clear(const GColor& color) override {
        const GPixel packed_pix = GPixel_PackARGB(
            GRoundToInt(color.a*255),
            GRoundToInt(color.r*color.a*255),
            GRoundToInt(color.g*color.a*255),
            GRoundToInt(color.b*color.a*255)
        );

        if (bmap.width() == 0) return;
        for (int y = 0; y < bmap.height(); ++y)
            std::fill_n(bmap.getAddr(0, y), bmap.width(), packed_pix);
    }

private:
    GBitmap bmap;
    GMatrix ctm;
    std::vector<GMatrix> matrixStack;
    std::vector<GPixel> shaderRow;
};

std::unique_ptr<GCanvas> GCreateCanvas(const GBitmap& bmap) {
    return std::make_unique<MyCanvas>(bmap);
}

std::string GDrawSomething(GCanvas* canvas, GISize dim) {
    const float w = static_cast<float>(dim.width);
    const float h = static_cast<float>(dim.height);

    canvas->clear({0.62f, 0.82f, 0.96f, 1});
    canvas->drawRect(GRect::LTRB(0, 0.66f*h, w, h), GPaint({0.35f, 0.65f, 0.28f, 1}));

    static const GPixel wallPixels[] = {
        GPixel_PackARGB(255, 236, 196, 145), GPixel_PackARGB(255, 220, 169, 120),
        GPixel_PackARGB(255, 226, 181, 130), GPixel_PackARGB(255, 242, 205, 157)
    };
    GBitmap wallBitmap({2, 2}, 2 * sizeof(GPixel),
                       GData::Unmanaged(wallPixels, sizeof(wallPixels)));
    wallBitmap.computeIsOpaque();

    GMatrix wallMatrix = GMatrix::Translate(0, 30) * GMatrix::Scale(35, 35);
    GPaint wallPaint(GShader::Bitmap(wallBitmap, wallMatrix));

    const GPoint roof[] = {{-5, 30}, {35, 0}, {75, 30}};
    const float houseX[] = {22, 94, 180};
    const float houseY[] = {82, 67, 91};
    const float houseScale[] = {0.85f, 1.05f, 0.75f};
    const GColor roofColor[] = {
        {0.62f, 0.19f, 0.16f, 1},
        {0.25f, 0.35f, 0.58f, 1},
        {0.52f, 0.25f, 0.13f, 1}
    };

    canvas->save();
    canvas->scale(w / 256, h / 256);
    for (int i = 0; i < 3; ++i) {
        canvas->save();
        canvas->translate(houseX[i], houseY[i]);
        canvas->scale(houseScale[i], houseScale[i]);

        canvas->drawRect(GRect::LTRB(0, 30, 70, 100), wallPaint);
        canvas->drawConvexPolygon(roof, 3, GPaint(roofColor[i]));
        canvas->drawRect(GRect::LTRB(28, 68, 43, 100), GPaint({0.36f, 0.20f, 0.10f, 1}));
        canvas->drawRect(GRect::LTRB(10, 48, 24, 64), GPaint({0.60f, 0.84f, 0.92f, 1}));
        canvas->drawRect(GRect::LTRB(47, 48, 61, 64), GPaint({0.60f, 0.84f, 0.92f, 1}));

        canvas->restore();
    }
    canvas->restore();

    return "Three Little Houses";
}
