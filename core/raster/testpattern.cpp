#include "testpattern.h"

#include <cstdio>

namespace nuxprint {

namespace {
void fillRect(MonoBitmap& bmp, int x0, int y0, int x1, int y1, bool black) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            bmp.setPixel(x, y, black);
}
} // namespace

MonoBitmap buildTestPattern(int widthDots) {
    MonoBitmap bmp;
    if (widthDots <= 0) return bmp;

    const int height = 420;
    bmp.width = widthDots;
    bmp.height = height;
    bmp.bits.assign(static_cast<size_t>(bmp.stride()) * height, 0);

    // 1. Full-width rule. If either end is missing, printableWidthDots is wrong.
    fillRect(bmp, 0, 0, widthDots, 8, true);

    // 2. Dot ruler: a tick every 64 dots, taller every 128. Lets you read the
    //    real head width off the paper with a ruler.
    for (int x = 0; x < widthDots; x += 64) {
        const int tickHeight = (x % 128 == 0) ? 24 : 14;
        fillRect(bmp, x, 12, (x + 2 > widthDots ? widthDots : x + 2), 12 + tickHeight, true);
    }

    // 3. Density bars: solid, vertical 50%, horizontal 50%, checkerboard.
    int y = 48;
    fillRect(bmp, 0, y, widthDots, y + 24, true);
    y += 32;
    for (int x = 0; x < widthDots; x += 2) fillRect(bmp, x, y, x + 1, y + 24, true);
    y += 32;
    for (int row = y; row < y + 24; row += 2) fillRect(bmp, 0, row, widthDots, row + 1, true);
    y += 32;
    for (int row = y; row < y + 24; ++row)
        for (int x = 0; x < widthDots; ++x)
            bmp.setPixel(x, row, ((x + row) & 1) != 0);

    // 4. Fine lines: 1,2,3,4 dot wide verticals with equal gaps.
    y += 32;
    int x = 8;
    for (int w = 1; w <= 4 && x < widthDots - 8; ++w) {
        for (int repeat = 0; repeat < 8 && x < widthDots - 8; ++repeat) {
            fillRect(bmp, x, y, x + w, y + 32, true);
            x += w * 2;
        }
        x += 12;
    }

    // 5. A framed block at the tail: shows exactly where the cut lands relative
    //    to the last printed dot.
    y += 48;
    fillRect(bmp, 0, y, widthDots, y + 4, true);
    fillRect(bmp, 0, y + 4, 4, y + 60, true);
    fillRect(bmp, widthDots - 4, y + 4, widthDots, y + 60, true);
    fillRect(bmp, 0, y + 60, widthDots, y + 64, true);

    return bmp;
}

bool writePbm(const MonoBitmap& bitmap, const char* path) {
    if (!bitmap.valid() || !path) return false;
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    std::fprintf(file, "P4\n%d %d\n", bitmap.width, bitmap.height);
    // PBM P4 packs exactly like we do: MSB first, 1 = black.
    std::fwrite(bitmap.bits.data(), 1, bitmap.bits.size(), file);
    std::fclose(file);
    return true;
}

} // namespace nuxprint
