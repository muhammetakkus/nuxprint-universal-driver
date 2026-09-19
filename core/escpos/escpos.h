// ESC/POS command writer — portable, no Windows dependencies.
//
// This file is deliberately free of <windows.h> so the same object code can be
// linked into (a) the Unidrv rendering plug-in that runs inside spoolsv.exe,
// (b) the device-setup / test utilities, and (c) the host unit tests that run
// on any platform. Nothing here allocates unbounded memory or throws: the
// spooler must never die because of us.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace nuxprint {

// How the printer expects a bitmap. GS v 0 is the modern, most widely
// implemented command; ESC * is the legacy 8/24-dot column mode kept as a
// fallback for devices that reject GS v 0. Only GS_V_0 is implemented in the
// MVP — ESC_STAR is declared so profiles can already name it.
enum class RasterMode : uint8_t {
    GS_V_0 = 0,
    ESC_STAR_24 = 1,
};

enum class CutMode : uint8_t {
    None = 0,
    Partial = 1,
    Full = 2,
};

// Everything that differs between printer models lives here, never in code.
// Values are filled from NuxPrintProfiles.json at queue-creation time and
// carried per-queue in the printer property bag.
struct Profile {
    int printableWidthDots = 576;   // 58mm: 384, 80mm: 576 typical — not assumed
    int dpi = 203;
    RasterMode rasterMode = RasterMode::GS_V_0;
    bool supportsCut = true;
    bool supportsPartialCut = true;
    bool supportsDrawer = false;

    // Rows per GS v 0 block. Many cheap controllers choke on very tall blocks
    // and some have small input buffers, so the default is conservative; the
    // profile can raise it once a model is proven on hardware.
    int maxBandHeightDots = 128;

    // Dots fed after the image, before the cut. A cut fired flush against the
    // last printed line leaves the text inside the cutter.
    int feedDotsAfterPrint = 80;
};

// A 1-bit-per-pixel bitmap, MSB = leftmost pixel, bit set = black dot.
// Row stride is always ceil(width/8) bytes, which is exactly what GS v 0 wants,
// so no repacking happens on the hot path.
struct MonoBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bits;

    static int strideForWidth(int width) { return (width + 7) / 8; }
    int stride() const { return strideForWidth(width); }
    bool valid() const {
        return width > 0 && height > 0 &&
               bits.size() == static_cast<size_t>(stride()) * static_cast<size_t>(height);
    }
    bool pixel(int x, int y) const;
    void setPixel(int x, int y, bool black);
    bool rowIsBlank(int y) const;
};

// Appends bytes to a caller-owned buffer. The plug-in hands each completed
// chunk straight to the spooler's write path, so the whole receipt is never
// resident at once (a 576x30000 job is ~2.1 MB of raster — fine — but the
// grayscale source that produced it is 17 MB, which is why banding matters).
class EscPosWriter {
public:
    explicit EscPosWriter(const Profile& profile) : profile_(profile) {}

    void initialize(std::vector<uint8_t>& out) const;

    // Emits the bitmap as one or more GS v 0 blocks, splitting on
    // profile.maxBandHeightDots. Pixels beyond printableWidthDots are dropped
    // rather than wrapped: a too-wide page must not turn into diagonal garbage.
    // Returns false if the bitmap is malformed (nothing is written).
    bool writeRaster(const MonoBitmap& bitmap, std::vector<uint8_t>& out) const;

    void feedDots(int dots, std::vector<uint8_t>& out) const;
    void feedLines(int lines, std::vector<uint8_t>& out) const;
    void cut(CutMode mode, std::vector<uint8_t>& out) const;
    void pulseDrawer(int pin, std::vector<uint8_t>& out) const;

    // Convenience for the test page / PoC: init + raster + feed + cut.
    bool writeJob(const MonoBitmap& bitmap, CutMode cut, std::vector<uint8_t>& out) const;

    const Profile& profile() const { return profile_; }

private:
    Profile profile_;
};

// Drops all-blank rows from the bottom of the bitmap. Receipt sources routinely
// hand us a page far taller than the content (a Letter-height PDF for a 3 cm
// receipt is the common case); without this the printer feeds — and on many
// models cuts — tens of centimetres of empty paper.
void trimTrailingBlankRows(MonoBitmap& bitmap, int keepRows = 0);

} // namespace nuxprint
