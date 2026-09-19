// NuxPrint Printer Test — PoC step 2.
//
// Sends an ESC/POS raster test pattern straight to a printer, deliberately
// WITHOUT any driver in the path. This proves the byte stream before the driver
// exists, so that when the driver is later wired up and something misprints, we
// already know which half is at fault.
//
// Two transports:
//   --device \\?\usb#vid_...   write directly to the usbprint device interface
//                              (the path Device Inspector prints)
//   --queue  "Printer Name"    spool a RAW job through an existing queue
//
// Build (Developer Command Prompt):
//   cl /EHsc /W4 /DUNICODE /D_UNICODE /I..\.. main.cpp ..\..\core\escpos\escpos.cpp ^
//      ..\..\core\raster\mono.cpp ..\..\core\raster\testpattern.cpp /link winspool.lib

#include <windows.h>
#include <winspool.h>

#include <cstdio>
#include <cstdlib>      // _wtoi
#include <string>
#include <utility>      // std::move
#include <vector>

#include "core/escpos/escpos.h"
#include "core/raster/testpattern.h"

using namespace nuxprint;

namespace {

bool writeToDevice(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        std::wprintf(L"CreateFile failed: %lu\n", GetLastError());
        std::wprintf(L"  (error 32 = another process holds the printer; pause its queue)\n");
        return false;
    }

    // Chunked writes: some controllers stall on a single multi-megabyte write,
    // and a stalled write inside the spooler is exactly how a print subsystem
    // hangs. 8 KB is small enough for the cheapest buffers.
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        const DWORD chunk = static_cast<DWORD>((data.size() - offset > 8192) ? 8192 : data.size() - offset);
        DWORD written = 0;
        if (!WriteFile(handle, data.data() + offset, chunk, &written, nullptr) || written == 0) {
            std::wprintf(L"WriteFile failed at offset %zu: %lu\n", offset, GetLastError());
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(handle);
    return ok;
}

bool writeToQueue(const std::wstring& queueName, const std::vector<uint8_t>& data) {
    HANDLE printer = nullptr;
    if (!OpenPrinterW(const_cast<wchar_t*>(queueName.c_str()), &printer, nullptr)) {
        std::wprintf(L"OpenPrinter failed: %lu\n", GetLastError());
        return false;
    }

    wchar_t docName[] = L"NuxPrint Raster Test";
    wchar_t dataType[] = L"RAW";
    DOC_INFO_1W docInfo = {docName, nullptr, dataType};

    bool ok = false;
    if (StartDocPrinterW(printer, 1, reinterpret_cast<LPBYTE>(&docInfo)) != 0) {
        if (StartPagePrinter(printer)) {
            DWORD written = 0;
            ok = WritePrinter(printer, const_cast<uint8_t*>(data.data()),
                              static_cast<DWORD>(data.size()), &written) != FALSE &&
                 written == data.size();
            if (!ok) std::wprintf(L"WritePrinter wrote %lu/%zu bytes\n", written, data.size());
            EndPagePrinter(printer);
        } else {
            std::wprintf(L"StartPagePrinter failed: %lu\n", GetLastError());
        }
        EndDocPrinter(printer);
    } else {
        std::wprintf(L"StartDocPrinter failed: %lu\n", GetLastError());
    }

    ClosePrinter(printer);
    return ok;
}

void usage() {
    std::wprintf(
        L"NuxPrint Printer Test\n\n"
        L"  NuxPrintPrinterTest.exe --device <devicePath> [options]\n"
        L"  NuxPrintPrinterTest.exe --queue  <printerName> [options]\n\n"
        L"Options:\n"
        L"  --width <dots>   printable width, default 576 (58mm is usually 384)\n"
        L"  --band <rows>    rows per GS v 0 block, default 128\n"
        L"  --cut none|partial|full   default partial\n"
        L"  --long           repeat the pattern 10x to test long receipts\n"
        L"  --dry <file>     write the ESC/POS stream to a file instead of printing\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { usage(); return 1; }

    std::wstring devicePath, queueName, dryPath;
    int width = 576;
    int band = 128;
    CutMode cutMode = CutMode::Partial;
    int repeat = 1;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : L""; };
        if (arg == L"--device") devicePath = next();
        else if (arg == L"--queue") queueName = next();
        else if (arg == L"--width") width = _wtoi(next().c_str());
        else if (arg == L"--band") band = _wtoi(next().c_str());
        else if (arg == L"--dry") dryPath = next();
        else if (arg == L"--long") repeat = 10;
        else if (arg == L"--cut") {
            const std::wstring mode = next();
            cutMode = (mode == L"none") ? CutMode::None
                    : (mode == L"full") ? CutMode::Full : CutMode::Partial;
        }
    }

    if (width <= 0 || band <= 0) { usage(); return 1; }

    Profile profile;
    profile.printableWidthDots = width;
    profile.maxBandHeightDots = band;

    MonoBitmap pattern = buildTestPattern(width);
    if (repeat > 1) {
        MonoBitmap tall;
        tall.width = width;
        tall.height = pattern.height * repeat;
        tall.bits.reserve(pattern.bits.size() * repeat);
        for (int i = 0; i < repeat; ++i)
            tall.bits.insert(tall.bits.end(), pattern.bits.begin(), pattern.bits.end());
        pattern = std::move(tall);
    }

    std::vector<uint8_t> stream;
    if (!EscPosWriter(profile).writeJob(pattern, cutMode, stream)) {
        std::wprintf(L"Failed to build the ESC/POS stream\n");
        return 2;
    }

    std::wprintf(L"Pattern %dx%d dots -> %zu bytes of ESC/POS\n",
                 pattern.width, pattern.height, stream.size());

    if (!dryPath.empty()) {
        FILE* file = _wfopen(dryPath.c_str(), L"wb");
        if (!file) { std::wprintf(L"Cannot open %s\n", dryPath.c_str()); return 3; }
        fwrite(stream.data(), 1, stream.size(), file);
        fclose(file);
        std::wprintf(L"Written to %s (nothing printed)\n", dryPath.c_str());
        return 0;
    }

    bool ok = false;
    if (!devicePath.empty())      ok = writeToDevice(devicePath, stream);
    else if (!queueName.empty())  ok = writeToQueue(queueName, stream);
    else { usage(); return 1; }

    std::wprintf(ok ? L"Sent.\n" : L"FAILED.\n");
    return ok ? 0 : 4;
}
