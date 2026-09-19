// NuxPrint Device Inspector — Phase 1 tool.
//
// Enumerates every USB Printer Class device Windows has bound to usbprint.sys
// and prints the identity we need to build a compatibility profile: hardware
// ID, VID/PID, the IEEE-1284 device ID string the printer reports about itself,
// the spooler port it owns (USB001...), and any printer queue already using it.
//
// This tool installs nothing and changes nothing. It is the first thing to run
// on a new printer model, and its output is what goes into compatibility.md.
//
// Build (Developer Command Prompt, Windows SDK):
//   cl /EHsc /W4 /DUNICODE /D_UNICODE main.cpp /link setupapi.lib winspool.lib
//
// Usage:
//   NuxPrintDeviceInspector.exe            list devices
//   NuxPrintDeviceInspector.exe --json     machine-readable output

#include <windows.h>
#include <setupapi.h>
#include <winspool.h>
#include <devguid.h>
#include <usbioctl.h>

#include <cstdio>
#include <string>
#include <vector>

// {28d78fad-5a12-11D1-ae5b-0000f803a8c2} — the USBPRINT device interface class.
// Not in every SDK header version, so it is declared locally rather than
// depending on which WDK the build machine happens to have.
static const GUID kGuidDevInterfaceUsbPrint = {
    0x28d78fad, 0x5a12, 0x11d1, {0xae, 0x5b, 0x00, 0x00, 0xf8, 0x03, 0xa8, 0xc2}};

#ifndef IOCTL_USBPRINT_GET_1284_ID
#define IOCTL_USBPRINT_GET_1284_ID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x11, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

namespace {

struct DeviceInfo {
    std::wstring interfacePath;
    std::wstring friendlyName;
    std::wstring manufacturer;
    std::wstring hardwareId;
    std::wstring compatibleIds;
    std::wstring portName;      // USB001, USB002...
    std::wstring ieee1284;
    std::wstring vid;
    std::wstring pid;
    std::wstring queueName;     // printer queue bound to this port, if any
    std::wstring queueDriver;
};

std::wstring registryProperty(HDEVINFO devInfo, SP_DEVINFO_DATA& devData, DWORD property) {
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, property, nullptr, nullptr, 0, &required);
    if (required == 0) return L"";

    std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, property, nullptr,
                                           buffer.data(), required, nullptr)) {
        return L"";
    }

    // REG_MULTI_SZ properties (hardware/compatible IDs) arrive as a double-null
    // terminated list; join them with "; " so one line stays one device.
    std::wstring result;
    const wchar_t* cursor = reinterpret_cast<const wchar_t*>(buffer.data());
    while (*cursor) {
        if (!result.empty()) result += L"; ";
        result += cursor;
        cursor += wcslen(cursor) + 1;
    }
    return result;
}

// The spooler port name lives under the device instance, not in a device
// property: HKLM\SYSTEM\CCS\Enum\<instance>\Device Parameters\PortName.
std::wstring devicePortName(HDEVINFO devInfo, SP_DEVINFO_DATA& devData) {
    HKEY key = SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) return L"";

    wchar_t value[64] = {0};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, L"PortName", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? std::wstring(value) : L"";
}

// Asks the printer who it is. The IEEE-1284 ID is the single most useful field
// for profiling: it carries MFG/MDL/CMD, and CMD tells us whether the device
// admits to speaking ESC/POS at all.
std::wstring read1284Id(const std::wstring& interfacePath) {
    HANDLE handle = CreateFileW(interfacePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        // Busy is the normal case when a queue is mid-job; not an error worth
        // failing the whole enumeration over.
        return L"<unavailable: " + std::to_wstring(GetLastError()) + L">";
    }

    BYTE buffer[1024] = {0};
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, IOCTL_USBPRINT_GET_1284_ID, nullptr, 0,
                                    buffer, sizeof(buffer), &returned, nullptr);
    CloseHandle(handle);
    if (!ok || returned < 3) return L"<not reported>";

    // First two bytes are a big-endian length including themselves.
    const int length = (buffer[0] << 8) | buffer[1];
    const int start = 2;
    const int count = (length > 2 && length <= static_cast<int>(returned)) ? length - 2
                                                                          : static_cast<int>(returned) - 2;
    std::string ascii(reinterpret_cast<char*>(buffer + start), count > 0 ? count : 0);
    // The string is ASCII; widen without a codepage conversion so odd bytes
    // cannot throw.
    return std::wstring(ascii.begin(), ascii.end());
}

void extractVidPid(const std::wstring& hardwareId, std::wstring& vid, std::wstring& pid) {
    const size_t v = hardwareId.find(L"VID_");
    const size_t p = hardwareId.find(L"PID_");
    if (v != std::wstring::npos && v + 8 <= hardwareId.size()) vid = hardwareId.substr(v + 4, 4);
    if (p != std::wstring::npos && p + 8 <= hardwareId.size()) pid = hardwareId.substr(p + 4, 4);
}

// Walks the spooler's queues so the report can say "this device already has a
// queue called X using driver Y" — the usual reason a vendor driver is in the way.
void attachQueueInfo(std::vector<DeviceInfo>& devices) {
    DWORD needed = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, nullptr, 0, &needed, &count);
    if (needed == 0) return;

    std::vector<BYTE> buffer(needed, 0);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, buffer.data(), needed, &needed, &count)) {
        return;
    }

    auto* printers = reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());
    for (DWORD i = 0; i < count; ++i) {
        if (!printers[i].pPortName) continue;
        for (auto& device : devices) {
            if (!device.portName.empty() && device.portName == printers[i].pPortName) {
                device.queueName = printers[i].pPrinterName ? printers[i].pPrinterName : L"";
                device.queueDriver = printers[i].pDriverName ? printers[i].pDriverName : L"";
            }
        }
    }
}

std::vector<DeviceInfo> enumerateUsbPrinters() {
    std::vector<DeviceInfo> devices;

    HDEVINFO devInfo = SetupDiGetClassDevsW(&kGuidDevInterfaceUsbPrint, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return devices;

    SP_DEVICE_INTERFACE_DATA interfaceData = {};
    interfaceData.cbSize = sizeof(interfaceData);

    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(devInfo, nullptr, &kGuidDevInterfaceUsbPrint, index, &interfaceData);
         ++index) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, nullptr, 0, &required, nullptr);
        if (required == 0) continue;

        std::vector<BYTE> detailBuffer(required, 0);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devData = {};
        devData.cbSize = sizeof(devData);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, detail, required,
                                              nullptr, &devData)) {
            continue;
        }

        DeviceInfo info;
        info.interfacePath = detail->DevicePath;
        info.friendlyName = registryProperty(devInfo, devData, SPDRP_FRIENDLYNAME);
        if (info.friendlyName.empty()) {
            info.friendlyName = registryProperty(devInfo, devData, SPDRP_DEVICEDESC);
        }
        info.manufacturer = registryProperty(devInfo, devData, SPDRP_MFG);
        info.hardwareId = registryProperty(devInfo, devData, SPDRP_HARDWAREID);
        info.compatibleIds = registryProperty(devInfo, devData, SPDRP_COMPATIBLEIDS);
        info.portName = devicePortName(devInfo, devData);
        info.ieee1284 = read1284Id(info.interfacePath);
        extractVidPid(info.hardwareId, info.vid, info.pid);

        devices.push_back(std::move(info));
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    attachQueueInfo(devices);
    return devices;
}

void printHuman(const std::vector<DeviceInfo>& devices) {
    if (devices.empty()) {
        std::wprintf(L"No USB Printer Class devices found.\n\n"
                     L"If a thermal printer is connected, it is probably bound to a\n"
                     L"vendor-specific USB interface instead of usbprint.sys. Check\n"
                     L"Device Manager: a device under 'Universal Serial Bus devices'\n"
                     L"rather than 'Printers' means the vendor driver claimed it.\n");
        return;
    }

    std::wprintf(L"Found %zu USB Printer Class device(s)\n\n", devices.size());
    int n = 1;
    for (const auto& d : devices) {
        std::wprintf(L"[%d] %s\n", n++, d.friendlyName.c_str());
        std::wprintf(L"    Manufacturer : %s\n", d.manufacturer.c_str());
        std::wprintf(L"    VID / PID    : %s / %s\n", d.vid.c_str(), d.pid.c_str());
        std::wprintf(L"    Hardware ID  : %s\n", d.hardwareId.c_str());
        std::wprintf(L"    Compatible   : %s\n", d.compatibleIds.c_str());
        std::wprintf(L"    Port         : %s\n", d.portName.c_str());
        std::wprintf(L"    IEEE-1284 ID : %s\n", d.ieee1284.c_str());
        std::wprintf(L"    Device path  : %s\n", d.interfacePath.c_str());
        if (!d.queueName.empty()) {
            std::wprintf(L"    Existing queue: %s  (driver: %s)\n",
                         d.queueName.c_str(), d.queueDriver.c_str());
        } else {
            std::wprintf(L"    Existing queue: <none>\n");
        }
        std::wprintf(L"\n");
    }
}

std::wstring jsonEscape(const std::wstring& value) {
    std::wstring out;
    for (wchar_t c : value) {
        if (c == L'"' || c == L'\\') { out += L'\\'; out += c; }
        else if (c == L'\n' || c == L'\r' || c == L'\t') out += L' ';
        else out += c;
    }
    return out;
}

void printJson(const std::vector<DeviceInfo>& devices) {
    std::wprintf(L"[\n");
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        std::wprintf(L"  {\n");
        std::wprintf(L"    \"friendlyName\": \"%s\",\n", jsonEscape(d.friendlyName).c_str());
        std::wprintf(L"    \"manufacturer\": \"%s\",\n", jsonEscape(d.manufacturer).c_str());
        std::wprintf(L"    \"vid\": \"%s\",\n", jsonEscape(d.vid).c_str());
        std::wprintf(L"    \"pid\": \"%s\",\n", jsonEscape(d.pid).c_str());
        std::wprintf(L"    \"hardwareId\": \"%s\",\n", jsonEscape(d.hardwareId).c_str());
        std::wprintf(L"    \"compatibleIds\": \"%s\",\n", jsonEscape(d.compatibleIds).c_str());
        std::wprintf(L"    \"port\": \"%s\",\n", jsonEscape(d.portName).c_str());
        std::wprintf(L"    \"ieee1284\": \"%s\",\n", jsonEscape(d.ieee1284).c_str());
        std::wprintf(L"    \"existingQueue\": \"%s\",\n", jsonEscape(d.queueName).c_str());
        std::wprintf(L"    \"existingDriver\": \"%s\"\n", jsonEscape(d.queueDriver).c_str());
        std::wprintf(L"  }%s\n", (i + 1 < devices.size()) ? L"," : L"");
    }
    std::wprintf(L"]\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool json = (argc > 1 && wcscmp(argv[1], L"--json") == 0);
    const std::vector<DeviceInfo> devices = enumerateUsbPrinters();
    if (json) printJson(devices);
    else      printHuman(devices);
    return 0;
}
