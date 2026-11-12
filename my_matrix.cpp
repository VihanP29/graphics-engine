#include "include/GMatrix.h"

#include <cmath>

GMatrix::GMatrix() : GMatrix(1, 0, 0, 0, 1, 0) {}

GMatrix GMatrix::Translate(float tx, float ty) {
    return GMatrix(1, 0, tx, 0, 1, ty);
}

GMatrix GMatrix::Scale(float sx, float sy) {
    return GMatrix(sx, 0, 0, 0, sy, 0);
}

GMatrix GMatrix::Rotate(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return GMatrix(c, -s, 0, s, c, 0);
}

GMatrix GMatrix::Concat(const GMatrix& a, const GMatrix& b) {
    return GMatrix(a[0] * b[0] + a[2] * b[1], a[0] * b[2] + a[2] * b[3],
                   a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[0] + a[3] * b[1],
                   a[1] * b[2] + a[3] * b[3], a[1] * b[4] + a[3] * b[5] + a[5]);
}

std::optional<GMatrix> GMatrix::invert() const {
    const float determinant = fMat[0] * fMat[3] - fMat[2] * fMat[1];
    if (determinant == 0) {
        return {};
    }

    const float invDet = 1.0f / determinant;
    return GMatrix(fMat[3] * invDet, -fMat[2] * invDet,
                   (fMat[2] * fMat[5] - fMat[3] * fMat[4]) * invDet, -fMat[1] * invDet,
                   fMat[0] * invDet,
                   (fMat[1] * fMat[4] - fMat[0] * fMat[5]) * invDet);
}

void GMatrix::mapPoints(GPoint dst[], const GPoint src[], int count) const {
    for (int i = 0; i < count; ++i) {
        const float x = src[i].x;
        const float y = src[i].y;
        dst[i] = {fMat[0] * x + fMat[2] * y + fMat[4],
                  fMat[1] * x + fMat[3] * y + fMat[5]};
    }
}
