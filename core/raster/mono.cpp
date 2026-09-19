#include "mono.h"

#include <algorithm>
#include <cmath>

namespace nuxprint {

namespace {
// 8x8 Bayer matrix, scaled to 0..255. Ordered dithering is preferred over
// error diffusion for receipts because it is position-independent: reprinting
// the same logo always produces identical dots, so a customer comparing two
// receipts does not see "different" output.
constexpr uint8_t kBayer8[8][8] = {
    {  0, 128,  32, 160,   8, 136,  40, 168},
    {192,  64, 224,  96, 200,  72, 232, 104},
    { 48, 176,  16, 144,  56, 184,  24, 152},
    {240, 112, 208,  80, 248, 120, 216,  88},
    { 12, 140,  44, 172,   4, 132,  36, 164},
    {204,  76, 236, 108, 196,  68, 228, 100},
    { 60, 188,  28, 156,  52, 180,  20, 148},
    {252, 124, 220,  92, 244, 116, 212,  84},
};
} // namespace

uint8_t GrayBitmap::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 255;
    return pixels[static_cast<size_t>(y) * width + x];
}

MonoBitmap halftone(const GrayBitmap& source, const HalftoneOptions& options) {
    MonoBitmap out;
    if (!source.valid()) return out;

    out.width = source.width;
    out.height = source.height;
    out.bits.assign(static_cast<size_t>(out.stride()) * static_cast<size_t>(out.height), 0);

    const int threshold = std::clamp(options.threshold, 1, 254);

    if (options.mode == HalftoneMode::FloydSteinberg) {
        // Two rolling error rows instead of a full-page float buffer: a
        // 576x30000 receipt would otherwise cost 69 MB inside the spooler.
        std::vector<int> errCurr(static_cast<size_t>(source.width) + 2, 0);
        std::vector<int> errNext(static_cast<size_t>(source.width) + 2, 0);

        for (int y = 0; y < source.height; ++y) {
            std::fill(errNext.begin(), errNext.end(), 0);
            for (int x = 0; x < source.width; ++x) {
                const int value = std::clamp(static_cast<int>(source.at(x, y)) + errCurr[x + 1], 0, 255);
                const bool black = value < threshold;
                out.setPixel(x, y, black);
                const int error = value - (black ? 0 : 255);
                errCurr[x + 2] += error * 7 / 16;
                errNext[x]     += error * 3 / 16;
                errNext[x + 1] += error * 5 / 16;
                errNext[x + 2] += error * 1 / 16;
            }
            errCurr.swap(errNext);
        }
        return out;
    }

    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const int value = source.at(x, y);
            int limit = threshold;
            if (options.mode == HalftoneMode::OrderedDither) {
                // Re-centre the Bayer cell around the chosen threshold so the
                // density setting still works in dither mode.
                limit = std::clamp(threshold + (kBayer8[y % 8][x % 8] - 128) / 2, 1, 254);
            }
            out.setPixel(x, y, value < limit);
        }
    }
    return out;
}

GrayBitmap rgbaToGray(const uint8_t* rgba, int width, int height) {
    GrayBitmap gray;
    if (!rgba || width <= 0 || height <= 0) return gray;
    gray.width = width;
    gray.height = height;
    gray.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    for (size_t i = 0, n = gray.pixels.size(); i < n; ++i) {
        const uint8_t r = rgba[i * 4 + 0];
        const uint8_t g = rgba[i * 4 + 1];
        const uint8_t b = rgba[i * 4 + 2];
        const uint8_t a = rgba[i * 4 + 3];
        // Composite onto white, then ITU-R BT.601 luma.
        const int rr = (r * a + 255 * (255 - a)) / 255;
        const int gg = (g * a + 255 * (255 - a)) / 255;
        const int bb = (b * a + 255 * (255 - a)) / 255;
        gray.pixels[i] = static_cast<uint8_t>((299 * rr + 587 * gg + 114 * bb) / 1000);
    }
    return gray;
}

} // namespace nuxprint
