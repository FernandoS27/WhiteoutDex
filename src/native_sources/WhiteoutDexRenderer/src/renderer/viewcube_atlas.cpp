#include "viewcube_atlas.h"
#include <cstring>

namespace WhiteoutDex {
namespace {

constexpr int kCellW = 64;
constexpr int kCellH = 64;
constexpr int kFaces = 6;
constexpr int kAtlasW = kCellW * kFaces;
constexpr int kAtlasH = kCellH;

struct RGBA { uint8_t r, g, b, a; };

// 5x7 bitmap font. One byte per row; bit 0 = leftmost pixel, bit 4 = rightmost.
// Only the glyphs used by the six face labels are defined.
struct Glyph { uint8_t rows[7]; };

constexpr Glyph G_F = {{0x0F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01}};
constexpr Glyph G_R = {{0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11}};
constexpr Glyph G_O = {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
constexpr Glyph G_N = {{0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x11}};
constexpr Glyph G_T = {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
constexpr Glyph G_B = {{0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F}};
constexpr Glyph G_A = {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
constexpr Glyph G_C = {{0x1E, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1E}};
constexpr Glyph G_K = {{0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11}};
constexpr Glyph G_L = {{0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1F}};
constexpr Glyph G_E = {{0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F}};
constexpr Glyph G_I = {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
constexpr Glyph G_G = {{0x1E, 0x01, 0x01, 0x19, 0x11, 0x11, 0x1E}};
constexpr Glyph G_H = {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
constexpr Glyph G_P = {{0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01}};

struct Label { const Glyph* glyphs[5]; int count; };

constexpr Label kLabels[kFaces] = {
    {{&G_F, &G_R, &G_O, &G_N, &G_T},      5}, // FRONT
    {{&G_B, &G_A, &G_C, &G_K, nullptr},   4}, // BACK
    {{&G_L, &G_E, &G_F, &G_T, nullptr},   4}, // LEFT
    {{&G_R, &G_I, &G_G, &G_H, &G_T},      5}, // RIGHT
    {{&G_T, &G_O, &G_P, nullptr, nullptr}, 3}, // TOP
    {{&G_B, &G_O, &G_T, nullptr, nullptr}, 3}, // BOT
};

// Face background colors (match the original GDI output).
constexpr RGBA kFaceBg[kFaces] = {
    {100, 140, 190, 230}, // Front  — blue
    {190, 120, 100, 230}, // Back   — red
    {100, 180, 120, 230}, // Left   — green
    {190, 170, 100, 230}, // Right  — yellow
    {160, 160, 180, 230}, // Top    — gray-blue
    {140, 130, 120, 230}, // Bottom — brown
};

constexpr RGBA kBorder = {60,  60,  60,  230};
constexpr RGBA kText   = {240, 240, 240, 230};

inline void SetPixel(uint8_t* pixels, int x, int y, RGBA c) {
    if (x < 0 || y < 0 || x >= kAtlasW || y >= kAtlasH) return;
    uint8_t* p = pixels + (y * kAtlasW + x) * 4;
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
}

// Draw a glyph at 2x scale (10x14 px) with top-left corner at (ox, oy).
void DrawGlyph2x(uint8_t* pixels, const Glyph& g, int ox, int oy, RGBA c) {
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = g.rows[row];
        for (int col = 0; col < 5; ++col) {
            if (!(bits & (1u << col))) continue;
            int x = ox + col * 2;
            int y = oy + row * 2;
            SetPixel(pixels, x,     y,     c);
            SetPixel(pixels, x + 1, y,     c);
            SetPixel(pixels, x,     y + 1, c);
            SetPixel(pixels, x + 1, y + 1, c);
        }
    }
}

void FillCell(uint8_t* pixels, int cellX, RGBA bg) {
    for (int y = 0; y < kCellH; ++y) {
        for (int x = 0; x < kCellW; ++x) {
            SetPixel(pixels, cellX + x, y, bg);
        }
    }
    // 1px border
    for (int x = 0; x < kCellW; ++x) {
        SetPixel(pixels, cellX + x, 0,           kBorder);
        SetPixel(pixels, cellX + x, kCellH - 1,  kBorder);
    }
    for (int y = 0; y < kCellH; ++y) {
        SetPixel(pixels, cellX,              y, kBorder);
        SetPixel(pixels, cellX + kCellW - 1, y, kBorder);
    }
}

} // namespace

std::vector<uint8_t> GenerateViewCubeAtlas(int& outW, int& outH) {
    outW = kAtlasW;
    outH = kAtlasH;
    std::vector<uint8_t> pixels(kAtlasW * kAtlasH * 4);

    // Glyph cell size at 2x = 10x14 px, plus 2px gap between chars.
    constexpr int kGlyphW = 10;
    constexpr int kGlyphH = 14;
    constexpr int kGap    = 2;

    for (int face = 0; face < kFaces; ++face) {
        int cellX = face * kCellW;
        FillCell(pixels.data(), cellX, kFaceBg[face]);

        const Label& label = kLabels[face];
        int textW = label.count * kGlyphW + (label.count - 1) * kGap;
        int ox = cellX + (kCellW - textW) / 2;
        int oy = (kCellH - kGlyphH) / 2;

        for (int i = 0; i < label.count; ++i) {
            DrawGlyph2x(pixels.data(), *label.glyphs[i],
                        ox + i * (kGlyphW + kGap), oy, kText);
        }
    }

    return pixels;
}

} // namespace WhiteoutDex
