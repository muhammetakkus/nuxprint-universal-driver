#include "escpos.h"

#include <algorithm>

namespace nuxprint {

namespace {
constexpr uint8_t ESC = 0x1B;
constexpr uint8_t GS  = 0x1D;

void appendBytes(std::vector<uint8_t>& out, std::initializer_list<uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
} // namespace

bool MonoBitmap::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    const size_t index = static_cast<size_t>(y) * stride() + static_cast<size_t>(x / 8);
    if (index >= bits.size()) return false;
    return (bits[index] >> (7 - (x % 8))) & 1;
}

void MonoBitmap::setPixel(int x, int y, bool black) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const size_t index = static_cast<size_t>(y) * stride() + static_cast<size_t>(x / 8);
    if (index >= bits.size()) return;
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - (x % 8)));
    if (black) bits[index] |= mask;
    else       bits[index] = static_cast<uint8_t>(bits[index] & ~mask);
}

bool MonoBitmap::rowIsBlank(int y) const {
    if (y < 0 || y >= height) return true;
    const int rowStride = stride();
    const size_t begin = static_cast<size_t>(y) * rowStride;
    if (begin + rowStride > bits.size()) return true;
    for (int i = 0; i < rowStride; ++i) {
        if (bits[begin + i] != 0) return false;
    }
    return true;
}

void EscPosWriter::initialize(std::vector<uint8_t>& out) const {
    appendBytes(out, {ESC, '@'});                 // ESC @  — reset to power-on defaults
    appendBytes(out, {ESC, 'a', 0x00});           // ESC a 0 — left align; raster is pre-positioned
    appendBytes(out, {GS, 'L', 0x00, 0x00});      // GS L 0 — no left margin
}

bool EscPosWriter::writeRaster(const MonoBitmap& bitmap, std::vector<uint8_t>& out) const {
    if (!bitmap.valid()) return false;
    if (profile_.rasterMode != RasterMode::GS_V_0) return false;  // ESC * not implemented yet

    // Clip rather than wrap. A page wider than the head produces a narrower
    // receipt, which is obviously wrong to the eye; wrapping produces a
    // diagonal smear that looks like a driver bug on the customer's counter.
    const int widthDots = std::min(bitmap.width, profile_.printableWidthDots);
    const int outStride = MonoBitmap::strideForWidth(widthDots);
    const int srcStride = bitmap.stride();
    if (outStride <= 0 || outStride > 0xFFFF) return false;

    const int bandHeight = std::max(1, profile_.maxBandHeightDots);

    for (int top = 0; top < bitmap.height; top += bandHeight) {
        const int rows = std::min(bandHeight, bitmap.height - top);
        if (rows > 0xFFFF) return false;

        // GS v 0 m xL xH yL yH d1...dk  (k = xL+xH*256 times yL+yH*256)
        appendBytes(out, {GS, 'v', '0', 0x00});
        out.push_back(static_cast<uint8_t>(outStride & 0xFF));
        out.push_back(static_cast<uint8_t>((outStride >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(rows & 0xFF));
        out.push_back(static_cast<uint8_t>((rows >> 8) & 0xFF));

        const size_t payload = static_cast<size_t>(outStride) * static_cast<size_t>(rows);
        out.reserve(out.size() + payload);

        // Masking depends on whether columns were dropped, NOT on the strides
        // differing: clipping 24 dots to 20 leaves both at 3 bytes per row, and
        // the 4 clipped columns would still print without this.
        const int usedBitsInLastByte = widthDots % 8;
        const bool maskLastByte = (widthDots < bitmap.width) && usedBitsInLastByte != 0;
        const uint8_t lastByteMask = static_cast<uint8_t>(0xFF << (8 - (usedBitsInLastByte ? usedBitsInLastByte : 8)));

        for (int y = top; y < top + rows; ++y) {
            const size_t srcRow = static_cast<size_t>(y) * srcStride;
            out.insert(out.end(), bitmap.bits.begin() + srcRow,
                       bitmap.bits.begin() + srcRow + outStride);
            if (maskLastByte) {
                out.back() = static_cast<uint8_t>(out.back() & lastByteMask);
            }
        }
    }
    return true;
}

void EscPosWriter::feedDots(int dots, std::vector<uint8_t>& out) const {
    if (dots <= 0) return;
    // ESC J n feeds n dots, n <= 255; repeat for longer feeds.
    while (dots > 0) {
        const int chunk = std::min(dots, 255);
        appendBytes(out, {ESC, 'J', static_cast<uint8_t>(chunk)});
        dots -= chunk;
    }
}

void EscPosWriter::feedLines(int lines, std::vector<uint8_t>& out) const {
    if (lines <= 0) return;
    while (lines > 0) {
        const int chunk = std::min(lines, 255);
        appendBytes(out, {ESC, 'd', static_cast<uint8_t>(chunk)});
        lines -= chunk;
    }
}

void EscPosWriter::cut(CutMode mode, std::vector<uint8_t>& out) const {
    if (mode == CutMode::None || !profile_.supportsCut) return;
    const bool partial = (mode == CutMode::Partial) && profile_.supportsPartialCut;
    // GS V m: 65 = partial with feed, 66 = full with feed. The "A/B with feed"
    // variants are used because a plain GS V 0/1 cuts at the current position,
    // which on most mechanisms means cutting through the last printed line.
    appendBytes(out, {GS, 'V', static_cast<uint8_t>(partial ? 66 : 65), 0x00});
}

void EscPosWriter::pulseDrawer(int pin, std::vector<uint8_t>& out) const {
    if (!profile_.supportsDrawer) return;
    const uint8_t m = (pin == 2) ? 0x01 : 0x00;   // pin 2 or pin 5
    appendBytes(out, {ESC, 'p', m, 0x19, 0xFA});  // ~25ms on, ~250ms off
}

bool EscPosWriter::writeJob(const MonoBitmap& bitmap, CutMode cutMode,
                            std::vector<uint8_t>& out) const {
    initialize(out);
    if (!writeRaster(bitmap, out)) return false;
    feedDots(profile_.feedDotsAfterPrint, out);
    cut(cutMode, out);
    return true;
}

void trimTrailingBlankRows(MonoBitmap& bitmap, int keepRows) {
    if (!bitmap.valid()) return;
    int lastContentRow = -1;
    for (int y = bitmap.height - 1; y >= 0; --y) {
        if (!bitmap.rowIsBlank(y)) { lastContentRow = y; break; }
    }
    // An entirely blank page still keeps one row: callers treat height 0 as an
    // error, and a blank receipt should feed and cut, not fail the job.
    const int newHeight = std::max(1, std::min(bitmap.height, lastContentRow + 1 + std::max(0, keepRows)));
    if (newHeight >= bitmap.height) return;
    bitmap.bits.resize(static_cast<size_t>(bitmap.stride()) * static_cast<size_t>(newHeight));
    bitmap.height = newHeight;
}

} // namespace nuxprint
