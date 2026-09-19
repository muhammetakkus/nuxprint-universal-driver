// Grayscale -> 1-bit conversion for thermal output. Portable; see escpos.h for
// why these files avoid <windows.h>.
#pragma once

#include <cstdint>
#include <vector>

#include "../escpos/escpos.h"

namespace nuxprint {

// 8-bit grayscale, 0 = black, 255 = white — the orientation Windows halftoning
// and every image library already use.
struct GrayBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
    uint8_t at(int x, int y) const;
};

enum class HalftoneMode : uint8_t {
    Threshold = 0,       // text/barcode: sharp edges, no dot noise
    OrderedDither = 1,   // photos, cheap and deterministic
    FloydSteinberg = 2,  // photos, best quality, slowest
};

// Density shifts the decision point: darker prints push more pixels to black,
// which on worn thermal heads is what "Dark" in the preferences UI means.
struct HalftoneOptions {
    HalftoneMode mode = HalftoneMode::Threshold;
    int threshold = 128;   // 0..255
};

// Converts to the packed 1bpp layout GS v 0 expects (bit set = black).
MonoBitmap halftone(const GrayBitmap& source, const HalftoneOptions& options);

// Convenience for RGB sources (logos, PNG): alpha is composited onto white
// first, because an unpainted alpha channel otherwise prints as solid black.
GrayBitmap rgbaToGray(const uint8_t* rgba, int width, int height);

} // namespace nuxprint
