# Build

## Host testleri (Windows gerekmez)

`core/` altındaki ESC/POS ve raster kodu platformdan bağımsızdır ve tek başına
test edilir. Bu, depoda **şu an gerçekten doğrulanmış** olan tek katmandır.

```bash
c++ -std=c++17 -O1 -Wall -Wextra -o nuxtest \
    tests/unit/test_escpos.cpp core/escpos/escpos.cpp core/raster/mono.cpp
./nuxtest
```

Beklenen: `56 checks, 0 failures`.

## Device Inspector (Windows)

Windows SDK yeterli, WDK gerekmez.

```bat
cd tools\device-inspector
build.bat
NuxPrintDeviceInspector.exe
NuxPrintDeviceInspector.exe --json > device.json
```

Yönetici olarak çalıştırmak gerekmez; ancak yazıcı başka bir işlem tarafından
açıksa IEEE-1284 okuması `<unavailable: 32>` dönebilir — bu normaldir, kuyruğu
duraklatıp tekrar deneyin.

## Driver paketi (Windows + WDK)

Henüz build edilmedi; INF/GPD taslakları `driver/` altındadır ve Faz 2'de
`infverif` + `inf2cat` ile doğrulanacaktır. Komutlar Faz 2 sonunda buraya yazılır.
