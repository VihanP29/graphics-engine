#include "include/GShader.h"
#include "include/GBitmap.h"
#include "include/GMatrix.h"

#include <algorithm>
#include <vector>

namespace {

float tile(float t, GTileMode mode) {
    if (t >= 0 && t < 1) return t;
    if (mode == GTileMode::kClamp) return std::clamp(t, 0.0f, 1.0f);
    if (mode == GTileMode::kRepeat) return t - std::floor(t);
    t = std::fmod(t, 2.0f);
    if (t < 0) t += 2;
    return t <= 1 ? t : 2-t;
}

int tile_pixel(int x, int size, GTileMode mode) {
    if (mode == GTileMode::kClamp) return std::clamp(x, 0, size-1);
    if (x >= 0 && x < size) return x;
    int period = mode == GTileMode::kRepeat ? size : 2*size;
    x %= period;
    if (x < 0) x += period;
    if (mode == GTileMode::kMirror && x >= size) x = period-1-x;
    return x;
}

class BitmapShader : public GShader {
public:
    BitmapShader(const GBitmap& bitmap, const GMatrix& localMatrix, GTileMode mode)
        : fBitmap(bitmap), fLocalMatrix(localMatrix), fMode(mode) {}

    bool isOpaque() override {
        return fBitmap.isOpaque();
    }

    class BitmapContext : public Context {
    public:
        BitmapContext(const GBitmap& bitmap, const GMatrix& inverse, GTileMode mode)
            : fBitmap(bitmap), fInverse(inverse), fMode(mode) {}

        void shadeRow(int x, int y, int count, GPixel row[]) override {
            GPoint location = fInverse * GPoint{x + 0.5f, y + 0.5f};
            const GVector step = fInverse.e0();

            for (int i = 0; i < count; ++i) {
                const int bitmapX = tile_pixel(GFloorToInt(location.x), fBitmap.width(), fMode);
                const int bitmapY = tile_pixel(GFloorToInt(location.y), fBitmap.height(), fMode);
                row[i] = *fBitmap.getAddr(bitmapX, bitmapY);
                location += step;
            }
        }

    private:
        GBitmap fBitmap;
        GMatrix fInverse;
        GTileMode fMode;
    };

    std::unique_ptr<Context> makeContext(const GMatrix& ctm) override {
        auto inverse = (ctm * fLocalMatrix).invert();
        if (!inverse.has_value()) {
            return nullptr;
        }
        return std::make_unique<BitmapContext>(fBitmap, *inverse, fMode);
    }

private:
    GBitmap fBitmap;
    GMatrix fLocalMatrix;
    GTileMode fMode;
};


GPixel gradient_pixel(const GColor& c) {
    return GPixel_PackARGB(GRoundToInt(c.a*255), GRoundToInt(c.r*c.a*255), GRoundToInt(c.g*c.a*255), GRoundToInt(c.b*c.a*255));
}

class GradientShader : public GShader {
public:
    GradientShader(GPoint p0, GPoint p1, const GColor colors[], int count, GTileMode mode)
        : fP0(p0), fP1(p1), fColors(colors, colors + count), fMode(mode) {}

    bool isOpaque() override {
        for (const GColor& c : fColors) {
            if (c.a != 1) return false;
        }
        return true;
    }

    class GradientContext : public Context {
    public:
        GradientContext(const std::vector<GColor>& colors, const GMatrix& inverse, GTileMode mode)
            : fColors(colors), fInverse(inverse), fMode(mode) {}

        void shadeRow(int x, int y, int count, GPixel row[]) override {
            if (fColors.size() == 1) {
                std::fill_n(row, count, gradient_pixel(fColors[0]));
                return;
            }
            float t = (fInverse * GPoint{x + 0.5f, y + 0.5f}).x;
            const int last = static_cast<int>(fColors.size()) - 1;
            for (int i = 0; i < count; ++i) {
                float pos = tile(t, fMode) * last;
                int idx = std::min(GFloorToInt(pos), last - 1);
                float w = pos - idx;
                GColor c = fColors[idx]*(1-w) + fColors[idx+1]*w;
                row[i] = gradient_pixel(c);
                t += fInverse[0];
            }
        }

    private:
        std::vector<GColor> fColors;
        GMatrix fInverse;
        GTileMode fMode;
    };

    std::unique_ptr<Context> makeContext(const GMatrix& ctm) override {
        if (fColors.size() == 1) {
            return std::make_unique<GradientContext>(fColors, GMatrix(), fMode);
        }
        float dx = fP1.x - fP0.x;
        float dy = fP1.y - fP0.y;
        GMatrix local(dx, -dy, fP0.x, dy, dx, fP0.y);
        auto inverse = (ctm * local).invert();
        if (!inverse.has_value()) return nullptr;
        return std::make_unique<GradientContext>(fColors, *inverse, fMode);
    }

private:
    GPoint fP0, fP1;
    std::vector<GColor> fColors;
    GTileMode fMode;
};

class AlphaShader : public GShader {
public:
    AlphaShader(std::shared_ptr<GShader> base, float scale) : fBase(std::move(base)), fScale(scale) {}
    bool isOpaque() override { return fScale == 1 && fBase->isOpaque(); }
    class AlphaContext : public Context {
    public:
        AlphaContext(std::unique_ptr<Context> base, float scale) : fBase(std::move(base)), fScale(scale) {}
        void shadeRow(int x, int y, int count, GPixel row[]) override {
            if (fScale == 0) {
                std::fill_n(row, count, 0);
                return;
            }
            fBase->shadeRow(x, y, count, row);
            if (fScale == 1) return;
            for (int i = 0; i < count; ++i) {
                GPixel p = row[i];
                row[i] = GPixel_PackARGB(GRoundToInt(GPixel_GetA(p)*fScale),
                    GRoundToInt(GPixel_GetR(p)*fScale), GRoundToInt(GPixel_GetG(p)*fScale),
                    GRoundToInt(GPixel_GetB(p)*fScale));
            }
        }
    private:
        std::unique_ptr<Context> fBase;
        float fScale;
    };
    std::unique_ptr<Context> makeContext(const GMatrix& ctm) override {
        auto ctx = fBase->makeContext(ctm);
        if (!ctx) return nullptr;
        return std::make_unique<AlphaContext>(std::move(ctx), fScale);
    }
private:
    std::shared_ptr<GShader> fBase;
    float fScale;
};

}

std::shared_ptr<GShader> GShader::Bitmap(const GBitmap& bitmap, const GMatrix& localMatrix, GTileMode mode) {
    if (bitmap.width() <= 0 || bitmap.height() <= 0 || bitmap.pixels() == nullptr) {
        return nullptr;
    }
    return std::make_shared<BitmapShader>(bitmap, localMatrix, mode);
}

std::shared_ptr<GShader> GShader::LinearGradient(GPoint p0, GPoint p1, const GColor colors[], int count, GTileMode mode) {
    if (count < 1 || colors == nullptr) return nullptr;
    return std::make_shared<GradientShader>(p0, p1, colors, count, mode);
}

std::shared_ptr<GShader> GShader::ModAlpha(std::shared_ptr<GShader> base, float alphaScale) {
    if (!base) return nullptr;
    return std::make_shared<AlphaShader>(std::move(base), std::clamp(alphaScale, 0.0f, 1.0f));
}
