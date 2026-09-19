# NuxPrint Universal Thermal Driver — Mimari ve Fizibilite

Bu belge, kod yazmadan önce verilmesi gereken kararları ve hangi kararın hangi
kanıta dayandığını kaydeder. **Doğrulanmamış hiçbir şey "çalışıyor" diye
yazılmamıştır**; her başlıkta neyin kanıtlanmış, neyin donanımda test edilmesi
gerektiği ayrı ayrı belirtilir.

## 1. Windows driver mimarisi kararı

**Karar: Windows v3 printer driver, Unidrv tabanlı, user-mode OEM rendering
plug-in ile.**

Gerekçe:

- **v4 driver kullanılamaz.** v4 (Print Class Driver) Windows 8 ile geldi;
  Windows 7 v4 paketini kuramaz. Win7 hedefte olduğu sürece v3 zorunludur.
- **Kernel-mode driver gerekmez.** USB Printer Class cihazları Windows'un kendi
  `usbprint.sys` sürücüsüne bağlanır; bize düşen tek şey RAW byte akışını
  spooler'a vermektir. Kendi kernel sürücümüzü yazmak hem gereksiz hem de
  imzalama açısından çok daha pahalıdır.
- **Unidrv, halftone'u bizim yerimize yapar.** Unidrv sayfayı 1bpp'ye indirger;
  bizim işimiz o scanline'ları ESC/POS bloklarına çevirmek.

### GPD yeter mi, rendering plug-in şart mı?

GPD tek başına `GS v 0` üretebilir gibi görünür (komut parametrelerine bayt
sayısı/satır sayısı geçirilebilir), ancak üç şey GPD'nin ifade gücünü aşar:

1. **Blok başlıkları band başına değişkendir** ve ESC/POS başlığı komuttan önce
   little-endian iki bayt çift ister; GPD'nin komut değişkenleri bu formatı
   güvenilir biçimde üretemeyebilir (WDK'da doğrulanacak — bkz. Risk R1).
2. **Profil sürücüsü davranış** (58/80 mm, kesici var/yok, band yüksekliği,
   eşik) kuyruk başına değişir; GPD statiktir.
3. **Boş satır kırpma** ve **streaming** mantığı kod ister.

**Karar:** GPD kağıt/çözünürlük/seçenek tanımını taşır, `IPrintOemUni`
implement eden `NuxPrintThermalRender.dll` ESC/POS akışını üretir. GPD-only
yaklaşımı Faz 2'de ölçülür; işe yararsa DLL ince kalır, yaramazsa zaten hazırdır.

## 2. Pipeline

```
Uygulama (Chrome / Word / PDF okuyucu / POS)
        │  GDI çizim çağrıları
        ▼
Spooler (EMF spool)  →  Print Processor
        ▼
Unidrv  ── sayfayı raster'a indirger, halftone uygular (1bpp scanline'lar)
        ▼
NuxPrintThermalRender.dll  (IPrintOemUni)
        │  • band topla (maxBandHeightDots)
        │  • sondaki boş satırları kırp
        │  • GS v 0 blokları üret
        │  • init / feed / cut enjekte et
        ▼
RAW byte akışı → Language/Port Monitor (usbmon) → usbprint.sys → yazıcı
```

Kritik nokta: **uygulama hiçbir şey bilmez.** Türkçe karakter, font, logo, QR —
hepsi Windows tarafından rasterize edilir, yazıcının codepage'i hiç devreye
girmez. Bu, 4. maddedeki "raster first" prensibinin doğrudan sonucudur.

## 3. Depo yapısı

Bkz. depo kökü. Özet: `core/` platformdan bağımsız ve test edilebilir,
`driver/` Windows'a özgü ve WDK ister, `tools/` sahada kullanılan yardımcılar,
`tests/unit` host'ta çalışır.

`core/` içinde `<windows.h>` **yoktur**. Aynı kod hem spooler içindeki DLL'e
hem test binary'sine linklenir; bu sayede ESC/POS üretimi Windows olmadan
doğrulanabilir (ve doğrulandı: 56 test).

## 4. Toolchain

| Bileşen | Araç |
|---|---|
| Driver (INF/GPD/renderer) | Visual Studio 2022 + WDK 10 (v3 print driver desteği) |
| Device Inspector / setup | Windows SDK, C++17, `/MT` (runtime bağımlılığı yok) |
| Installer | Inno Setup 6 (agent'ta zaten kullanılıyor) veya WiX |
| İmzalama | `inf2cat`, `signtool`, `infverif` |
| Host testleri | Herhangi bir C++17 derleyicisi |

Win7 hedefi nedeniyle .NET **kullanılmaz**: Win7 SP1'de .NET 4.8 var ama
yükleme sırasında bulunmama riski var; setup ve inspector native C++.

## 5. Windows 7 / 10 / 11 stratejisi

Tek kaynak ağacı, tek INF, tek installer. Fark yalnızca **imzalamada**:

- Win10/11: driver paketi Authenticode imzalı CAT ile kurulur. Printer driver'lar
  user-mode olduğu için kernel attestation kuralları uygulanmaz; ancak
  PrintNightmare sonrası **driver kurulumu yönetici yetkisi ister** ve imzasız
  paket uyarı üretir.
- Win7: SHA-2 imza için KB4474419 gerekir. **Cross-signing sertifikaları
  emekliye ayrıldı**; bu yüzden Win7'de "hiç uyarı çıkmayan" bir kurulum bugün
  garanti edilemez (Risk R3).

Windows 11 modern yol (IPP Class Driver / Print Support App) ayrı bir hedef
olarak `docs/` içinde izlenir; ESC/POS cihazların çoğu IPP konuşmadığı için v3
yolunun yerine geçmez, yanına eklenir.

## 6. INF / GPD / renderer görev dağılımı

| Dosya | Sorumluluk |
|---|---|
| `NuxPrintThermal.inf` | Paket manifesti, model listesi, dosya kopyalama, Driver Store kaydı |
| `NuxPrintThermal.gpd` | Kağıt boyutları (58/80/Custom Roll), 203 DPI, printable area, seçenek listesi |
| `NuxPrintThermalRender.dll` | Raster → ESC/POS, band, kırpma, init/feed/cut |
| `NuxPrintThermalUI.dll` | Printer Preferences (MVP'de opsiyonel; Unidrv standart UI ile başlanır) |
| `NuxPrintProfiles.json` | Model farklılıkları |

## 7. USB keşfi ve kuyruk oluşturma

Keşif: `SetupDiGetClassDevs(GUID_DEVINTERFACE_USBPRINT)` → arayüz yolu →
`IOCTL_USBPRINT_GET_1284_ID` ile cihazın kendi kimliği → `Device Parameters\PortName`
ile spooler portu (USB001...). Bu akış `tools/device-inspector` içinde yazıldı.

Kuyruk oluşturma (native Win32, PowerShell yok — Win7 uyumu):

```
AddPrinterDriverEx()   → driver'i Driver Store'dan kuyruk için kaydet
AddPrinter()           → PRINTER_INFO_2: pPortName = "USB001",
                         pDriverName = "NuxPrint Universal Thermal"
SetPrinterDataEx()     → secilen profil id'si kuyrugun property bag'ine
```

Aynı generic driver'ın farklı modellere atanması **kuyruk başına profil** ile
çözülür: driver tek, kuyruk çok, profil kuyruğun verisinde.

## 8. Raster → ESC/POS

`GS v 0 m xL xH yL yH d...` blokları. Bit 1 = siyah nokta, MSB = en sol piksel —
Unidrv'nin 1bpp çıktısıyla aynı düzen, yani yeniden paketleme yok.

Bant yüksekliği profille sınırlanır (varsayılan 128 satır): ucuz kontrolcülerin
girdi tamponu küçüktür ve tek dev blok hem RAM hem zaman aşımı riskidir.
576×30000'lik bir fiş ~2,1 MB raster eder; band band gönderilir, tamamı bellekte
tutulmaz.

Genişlik taşması **kırpılır, sarılmaz**: sarma çapraz bulaşma üretir ve tezgâhta
"sürücü bozuk" gibi görünür.

## 9. Installer mimarisi

```
NuxPrint-Universal-Thermal-Driver-Setup.exe
  ├── elevation + OS/arch kontrolü
  ├── driver paketini Driver Store'a ekle (pnputil / SetupCopyOEMInf)
  ├── NuxPrintDeviceSetup.exe çalıştır
  │     ├── USB Printer Class cihazlarını tara
  │     ├── profil öner (vid/pid → ieee1284 → manufacturer → fallback)
  │     ├── 58/80 mm seçtir
  │     ├── kuyruk oluştur
  │     └── test sayfası
  └── rollback / uninstall
```

Sessiz kurulum: `/S`, ileride `/S /PROFILE=generic80 /PORT=USB001`.

## 10. İmzalama stratejisi

1. Geliştirme: test sertifikası + `bcdedit /set testsigning on` (yalnızca VM'de).
2. Üretim: şirket OV kod imzalama sertifikası (agent için alınan sertifikanın
   aynısı) ile `inf2cat` + `signtool`.
3. Win10/11'de tamamen uyarısız kurulum isteniyorsa Partner Center üzerinden
   WHQL/attestation değerlendirilir.

Test imzalama **asla** üretim çözümü değildir.

## 11. MVP ve sonrası

**MVP:** Win7/10/11 x64, USB Printer Class + TCP 9100, 58/80 mm, 203 DPI, 1-bit
raster, kuyruk oluşturma, Windows print dialog, HTML/PDF/görsel, Türkçe, uzun
fiş, feed, opsiyonel cut, keşif, test sayfası, installer/uninstaller.

**Sonraki:** Bluetooth, vendor-specific USB (WinUSB), native QR/barcode,
bidirectional status, çekmece, ARM64, x86, IPP yolu.

## 12. En riskli teknik konular

| # | Risk | Neden kritik | Nasıl kapatılır |
|---|---|---|---|
| R1 | Unidrv'nin 1bpp scanline'larını OEM plug-in'de doğru sırada/formatta yakalamak | Tüm çıktı buna bağlı | Faz 2 PoC: tek cihazda tek sayfa |
| R2 | Rulo kağıt / değişken sayfa boyunun v3'te modellenmesi | Yanlış modelleme = her fişte sayfa sonu, boş kağıt, yanlış kesme | GPD custom page + sondaki boş satır kırpma |
| R3 | Win7'de imza | Cross-signing emekli; uyarısız kurulum garanti değil | Erken araştır, gerekirse Win7'de "imzasız uyarısı" kabul edilir |
| R4 | Spooler kararlılığı | Sahada zaten bir POS sürücüsünün spooler'ı çökerttiğini gördük | Defensive kod, fuzz'lanmış girdi, uzun fiş testi |
| R5 | Cihazın USB Class 07 yerine vendor-specific arayüz sunması | Driver hiç bağlanamaz | MVP kapsam dışı, inspector raporlar |
| R6 | `GS v 0` desteklemeyen eski kontrolcüler | Boş çıktı | Profil ile `ESC *` fallback (MVP sonrası) |

## 13. İlk PoC

**Hedef:** Tek fiziksel yazıcı (önce Device Inspector ile kimliği çıkarılmış
olan), Windows 10 x64, USB.

Adımlar: Inspector çıktısı → profil kaydı → `printer-test` aracıyla **kuyruk
olmadan** doğrudan porta ESC/POS baskı → çalışıyorsa INF/GPD ile kuyruk →
Notepad'den baskı.

Bu sıra bilinçlidir: doğrudan port baskısı ESC/POS akışımızı doğrular; driver
katmanı ancak akış kanıtlandıktan sonra devreye girer. Böylece bir hata
olduğunda "ESC/POS mu yanlış, driver mı" sorusu hiç sorulmaz.

## 14. PoC kabul testleri

1. Inspector cihazı buluyor, IEEE-1284 ID okunuyor.
2. `printer-test` doğrudan porta 576 dot genişliğinde raster basıyor.
3. Uzun fiş (>3000 satır) kesintisiz basıyor, spooler ayakta.
4. Yazıcı kapalıyken iş başarısız oluyor ama spooler çökmüyor.
5. Kuyruk oluşuyor, Printers ekranında görünüyor.
6. Notepad'den Türkçe metin doğru basıyor.
7. Chrome'dan HTML basıyor, sağ/sol kenar kırpılmıyor.
8. Kesici destekliyorsa iş sonunda bir kez kesiyor.
9. Uninstall sonrası kuyruk ve driver temiz kalkıyor.
