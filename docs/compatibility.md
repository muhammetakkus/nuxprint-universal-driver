# Uyumluluk Veritabanı

Bu tabloya **yalnızca gerçek donanımda test edilen** cihazlar eklenir. Datasheet
veya "ESC/POS uyumlu" ibaresi yeterli değildir.

Her cihaz için `NuxPrintDeviceInspector.exe --json` çıktısı `tests/hardware/`
altına ham haliyle kaydedilir.

| Marka | Model | VID | PID | Bağlantı | Kağıt | Dots | DPI | USB Class 07 | ESC @ | GS v 0 | Cut | Drawer | Status | Win7 | Win10 | Win11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| _(ilk cihaz buraya)_ | | | | | | | | | | | | | | | | |

Kısaltmalar: ✔ çalışıyor, ✘ çalışmıyor, — test edilmedi.

## Kabul kriteri

Bir cihaz "supported" sayılmadan önce `docs/architecture.md` §14'teki dokuz
testin tamamından geçmelidir.
