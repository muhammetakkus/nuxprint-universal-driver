# PoC — Windows'ta yapılacak ilk tur

Sıra bilinçli: **önce ESC/POS akışını driver olmadan doğrula.** Böylece bir
hata çıktığında "ESC/POS mu yanlış, driver mı" sorusu hiç sorulmaz.

## Adım 1 — Cihaz kimliği

```bat
cd tools\device-inspector
build.bat
NuxPrintDeviceInspector.exe
NuxPrintDeviceInspector.exe --json > ..\..\tests\hardware\<marka>-<model>.json
```

Bana gönder: tam çıktı. Özellikle `IEEE-1284 ID`, `VID/PID`, `Port`,
`Existing queue`. Cihaz listede **hiç çıkmıyorsa** USB Printer Class değildir ve
MVP kapsamı dışındadır (bunu da bilmemiz gerekiyor).

## Adım 2 — Driver'sız raster baskı

```bat
cd tools\printer-test
build.bat

REM Once kagida hicbir sey gitmeden akisi dosyaya al (guvenli):
NuxPrintPrinterTest.exe --width 576 --dry test.bin

REM 80mm cihaz, dogrudan cihaz yoluna (Inspector'in yazdigi path):
NuxPrintPrinterTest.exe --device "\\?\usb#vid_xxxx&pid_xxxx#..." --width 576

REM 58mm cihaz:
NuxPrintPrinterTest.exe --device "..." --width 384 --cut none

REM Mevcut bir kuyruk uzerinden (vendor driver kuruluysa):
NuxPrintPrinterTest.exe --queue "80mm Series Printer" --width 576

REM Uzun fis davranisi:
NuxPrintPrinterTest.exe --device "..." --width 576 --long
```

## Adım 2'de kâğıda bakarken not edilecekler

Test deseni bu soruları cevaplamak için çizildi:

| Desen | Ne söyler |
|---|---|
| En üstteki tam genişlik çizgisi | `printableWidthDots` doğru mu, kenar kırpılıyor mu |
| 64 dot'ta bir çentikler | Kafanın gerçek genişliği (cetvelle say) |
| Dolu / %50 dikey / %50 yatay / dama | Yoğunluk ve halftone davranışı |
| 1-2-3-4 dot kalınlığında çizgiler | Çözünürlük, ince çizgi kaybı |
| En alttaki çerçeveli blok | Kesme, son basılan satırdan ne kadar uzakta |

## Adım 3 — Kabul kriterleri

`docs/architecture.md` §14. Adım 1-2 için özetle:

1. Inspector cihazı buluyor ve IEEE-1284 ID okunuyor.
2. `--dry` dosyası üretiliyor (bu makinede zaten doğrulandı).
3. Desen kâğıda tam genişlikte, kırpılmadan basıyor.
4. `--long` ile 10 tekrar kesintisiz basıyor.
5. Yazıcı kapalıyken komut hata veriyor ama Windows kilitlenmiyor.
6. Kesici destekliyorsa iş sonunda bir kez kesiyor.

Bu altısı geçmeden driver katmanına (Faz 2) geçmiyoruz.
