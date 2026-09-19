// Host-side unit tests for the portable core. These run on any platform with a
// C++17 compiler (they are the only part of this repository that can be
// verified without Windows and a physical printer).
#include "../../core/escpos/escpos.h"
#include "../../core/raster/mono.h"
#include "../../core/raster/testpattern.h"

#include <cstdio>
#include <string>

using namespace nuxprint;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

static MonoBitmap makeBitmap(int width, int height, bool fillBlack) {
    MonoBitmap bmp;
    bmp.width = width;
    bmp.height = height;
    bmp.bits.assign(static_cast<size_t>(bmp.stride()) * height, fillBlack ? 0xFF : 0x00);
    return bmp;
}

static void testBitPacking() {
    std::printf("bit packing\n");
    MonoBitmap bmp = makeBitmap(16, 1, false);
    bmp.setPixel(0, 0, true);    // MSB of byte 0
    bmp.setPixel(7, 0, true);    // LSB of byte 0
    bmp.setPixel(8, 0, true);    // MSB of byte 1
    check(bmp.bits[0] == 0x81, "leftmost and 8th pixel -> 0x81");
    check(bmp.bits[1] == 0x80, "9th pixel -> 0x80 in second byte");
    check(bmp.pixel(0, 0) && bmp.pixel(7, 0) && bmp.pixel(8, 0), "pixels read back");
    check(!bmp.pixel(1, 0), "untouched pixel stays white");
    check(!bmp.pixel(-1, 0) && !bmp.pixel(99, 0), "out-of-range reads are safe");
}

static void testGsV0Header() {
    std::printf("GS v 0 header\n");
    Profile p;
    p.printableWidthDots = 576;
    p.maxBandHeightDots = 1000;
    EscPosWriter w(p);

    MonoBitmap bmp = makeBitmap(576, 3, true);
    std::vector<uint8_t> out;
    check(w.writeRaster(bmp, out), "writeRaster succeeds");

    // 1D 76 30 00 xL xH yL yH + payload
    check(out.size() == 8 + 72 * 3, "total length = header + stride*rows");
    check(out[0] == 0x1D && out[1] == 'v' && out[2] == '0' && out[3] == 0x00, "GS v 0 m=0");
    check(out[4] == 72 && out[5] == 0, "xL/xH = 72 bytes per row (576 dots)");
    check(out[6] == 3 && out[7] == 0, "yL/yH = 3 rows");
    check(out[8] == 0xFF, "payload starts immediately after the header");
}

static void testBanding() {
    std::printf("banding / chunking\n");
    Profile p;
    p.printableWidthDots = 384;      // 58mm
    p.maxBandHeightDots = 128;
    EscPosWriter w(p);

    MonoBitmap bmp = makeBitmap(384, 300, false);
    std::vector<uint8_t> out;
    check(w.writeRaster(bmp, out), "tall bitmap accepted");

    int blocks = 0;
    size_t i = 0;
    int totalRows = 0;
    while (i + 8 <= out.size()) {
        check(out[i] == 0x1D && out[i + 1] == 'v' && out[i + 2] == '0', "block starts with GS v 0");
        const int stride = out[i + 4] | (out[i + 5] << 8);
        const int rows = out[i + 6] | (out[i + 7] << 8);
        check(stride == 48, "58mm stride = 48 bytes");
        check(rows <= 128, "no band exceeds maxBandHeightDots");
        totalRows += rows;
        ++blocks;
        i += 8 + static_cast<size_t>(stride) * rows;
    }
    check(i == out.size(), "blocks tile the buffer exactly, no trailing bytes");
    check(blocks == 3, "300 rows / 128 -> 3 blocks");
    check(totalRows == 300, "all rows emitted exactly once");
}

static void testClipping() {
    std::printf("over-wide page clipping\n");
    Profile p;
    p.printableWidthDots = 384;
    p.maxBandHeightDots = 64;
    EscPosWriter w(p);

    MonoBitmap bmp = makeBitmap(576, 2, true);   // 80mm content on a 58mm head
    std::vector<uint8_t> out;
    check(w.writeRaster(bmp, out), "wider-than-head bitmap still prints");
    const int stride = out[4] | (out[5] << 8);
    check(stride == 48, "emitted stride clamped to the printable width");
    check(out.size() == 8 + 48 * 2, "payload matches the clamped stride");
}

static void testPartialByteMasking() {
    std::printf("partial trailing byte masking\n");
    Profile p;
    p.printableWidthDots = 20;       // 2.5 bytes -> stride 3, 4 unused bits
    p.maxBandHeightDots = 64;
    EscPosWriter w(p);

    MonoBitmap bmp = makeBitmap(24, 1, true);
    std::vector<uint8_t> out;
    check(w.writeRaster(bmp, out), "narrow clamp succeeds");
    check(out[4] == 3, "stride = 3 bytes for 20 dots");
    check(out[8] == 0xFF && out[9] == 0xFF, "full bytes untouched");
    check(out[10] == 0xF0, "clipped columns masked off in the last byte");
}

static void testTrimTrailingBlank() {
    std::printf("trailing blank trim\n");
    MonoBitmap bmp = makeBitmap(64, 1000, false);
    for (int y = 0; y < 40; ++y) bmp.setPixel(3, y, true);
    trimTrailingBlankRows(bmp, 0);
    check(bmp.height == 40, "1000-row page with 40 rows of content trims to 40");
    check(bmp.valid(), "bitmap stays self-consistent after trim");

    MonoBitmap keep = makeBitmap(64, 500, false);
    for (int y = 0; y < 10; ++y) keep.setPixel(1, y, true);
    trimTrailingBlankRows(keep, 24);
    check(keep.height == 34, "keepRows adds bottom margin");

    MonoBitmap blank = makeBitmap(64, 200, false);
    trimTrailingBlankRows(blank, 0);
    check(blank.height == 1 && blank.valid(), "fully blank page collapses to one row, not zero");
}

static void testCommands() {
    std::printf("init / feed / cut / drawer\n");
    Profile p;
    p.supportsCut = true;
    p.supportsPartialCut = true;
    p.supportsDrawer = false;
    EscPosWriter w(p);

    std::vector<uint8_t> out;
    w.initialize(out);
    check(out.size() >= 2 && out[0] == 0x1B && out[1] == '@', "init starts with ESC @");

    out.clear();
    w.feedDots(600, out);
    check(out.size() == 9, "600 dots -> three ESC J chunks");
    check(out[0] == 0x1B && out[1] == 'J' && out[2] == 255, "first chunk is the 255 maximum");
    check(out[6] == 0x1B && out[7] == 'J' && out[8] == 90, "remainder is 90 dots");

    out.clear();
    w.cut(CutMode::Partial, out);
    check(out.size() == 4 && out[0] == 0x1D && out[1] == 'V' && out[2] == 66, "partial cut = GS V 66");

    out.clear();
    w.cut(CutMode::Full, out);
    check(out[2] == 65, "full cut = GS V 65");

    out.clear();
    w.pulseDrawer(1, out);
    check(out.empty(), "drawer pulse suppressed when the profile says unsupported");

    Profile noCut = p;
    noCut.supportsCut = false;
    out.clear();
    EscPosWriter(noCut).cut(CutMode::Full, out);
    check(out.empty(), "cut suppressed on cutter-less models");
}

static void testInvalidInput() {
    std::printf("defensive input handling\n");
    Profile p;
    EscPosWriter w(p);
    std::vector<uint8_t> out;

    MonoBitmap empty;
    check(!w.writeRaster(empty, out), "empty bitmap rejected");
    check(out.empty(), "nothing written for rejected input");

    MonoBitmap lying;
    lying.width = 64;
    lying.height = 10;
    lying.bits.assign(4, 0);     // far too small for the declared size
    check(!w.writeRaster(lying, out), "size/buffer mismatch rejected");
    check(out.empty(), "still nothing written");
}

static void testHalftone() {
    std::printf("halftone\n");
    GrayBitmap gray;
    gray.width = 64;
    gray.height = 64;
    gray.pixels.assign(64 * 64, 200);   // light grey

    HalftoneOptions th;
    th.mode = HalftoneMode::Threshold;
    th.threshold = 128;
    MonoBitmap mono = halftone(gray, th);
    check(mono.valid(), "threshold output is a valid bitmap");
    int black = 0;
    for (int y = 0; y < mono.height; ++y)
        for (int x = 0; x < mono.width; ++x)
            if (mono.pixel(x, y)) ++black;
    check(black == 0, "light grey above the threshold prints white");

    HalftoneOptions dither;
    dither.mode = HalftoneMode::OrderedDither;
    MonoBitmap dithered = halftone(gray, dither);
    int ditherBlack = 0;
    for (int y = 0; y < dithered.height; ++y)
        for (int x = 0; x < dithered.width; ++x)
            if (dithered.pixel(x, y)) ++ditherBlack;
    check(ditherBlack == 0, "ordered dither keeps light grey white at default density");

    gray.pixels.assign(64 * 64, 120);   // just darker than the threshold
    MonoBitmap fs = halftone(gray, {HalftoneMode::FloydSteinberg, 128});
    int fsBlack = 0;
    for (int y = 0; y < fs.height; ++y)
        for (int x = 0; x < fs.width; ++x)
            if (fs.pixel(x, y)) ++fsBlack;
    check(fsBlack > 0 && fsBlack < 64 * 64, "error diffusion produces a mix, not a solid block");

    const uint8_t rgba[8] = {0, 0, 0, 0,      // transparent black -> must read white
                             0, 0, 0, 255};   // opaque black
    GrayBitmap fromRgba = rgbaToGray(rgba, 2, 1);
    check(fromRgba.at(0, 0) == 255, "transparent pixels composite onto white");
    check(fromRgba.at(1, 0) == 0, "opaque black stays black");
}

static void testFullJob() {
    std::printf("full job assembly\n");
    Profile p;
    p.printableWidthDots = 576;
    p.maxBandHeightDots = 255;
    p.feedDotsAfterPrint = 80;
    EscPosWriter w(p);

    MonoBitmap bmp = makeBitmap(576, 100, true);
    std::vector<uint8_t> out;
    check(w.writeJob(bmp, CutMode::Partial, out), "job assembled");
    check(out[0] == 0x1B && out[1] == '@', "job starts with a reset");
    check(out[out.size() - 4] == 0x1D && out[out.size() - 3] == 'V', "job ends with the cut");
}

static void testPattern() {
    std::printf("hardware test pattern\n");
    for (int width : {384, 576, 640}) {
        MonoBitmap pattern = buildTestPattern(width);
        check(pattern.valid(), "pattern is a valid bitmap");
        check(pattern.width == width, "pattern honours the requested width");
        check(!pattern.rowIsBlank(0), "top rule is printed edge to edge");
        check(pattern.pixel(0, 0) && pattern.pixel(width - 1, 0), "both ends of the rule are set");

        Profile p;
        p.printableWidthDots = width;
        std::vector<uint8_t> out;
        check(EscPosWriter(p).writeJob(pattern, CutMode::Partial, out), "pattern encodes to ESC/POS");
        check(out.size() > static_cast<size_t>(MonoBitmap::strideForWidth(width)) * pattern.height,
              "encoded stream is larger than the raw payload (headers present)");
    }
    check(buildTestPattern(0).width == 0, "zero width rejected");
}

int main() {
    testBitPacking();
    testGsV0Header();
    testBanding();
    testClipping();
    testPartialByteMasking();
    testTrimTrailingBlank();
    testCommands();
    testInvalidInput();
    testHalftone();
    testFullJob();
    testPattern();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
