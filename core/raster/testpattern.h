// Hardware validation pattern. Portable so the exact same bitmap can be
// rendered on the host (for inspection) and sent to a real printer.
#pragma once

#include "../escpos/escpos.h"

namespace nuxprint {

// Draws a pattern that answers the questions a new printer model raises, in the
// order they matter:
//   1. full-width rule        -> is printableWidthDots right? clipped edges?
//   2. dot ruler (every 64)   -> where does the head actually end?
//   3. solid / 50% / grey bars-> density and halftone behaviour
//   4. fine 1px lines         -> head resolution and dither artefacts
//   5. tail block             -> feed/cut distance from the last printed line
MonoBitmap buildTestPattern(int widthDots);

// Writes a binary PBM (P4) so the pattern can be eyeballed without a printer.
bool writePbm(const MonoBitmap& bitmap, const char* path);

} // namespace nuxprint
