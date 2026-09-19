# NuxPrintThermalRender.dll — henüz yazılmadı

Bu DLL, Unidrv'nin `IPrintOemUni` arayüzünü implement eder ve 1bpp scanline'ları
`core/escpos` üzerinden ESC/POS akışına çevirir.

**Bilerek Faz 2'ye bırakıldı.** Hangi callback'in (`ImageProcessing`,
`FilterGraphics`, `SendPage`) hangi sırada ve hangi tamponla çağrıldığı WDK
dokümantasyonu + tek cihazlık PoC ile doğrulanmadan buraya kod yazmak, sonradan
tamamı atılacak 1500 satır üretmek olur.

Doğrulanacak sorular `docs/architecture.md` §12 R1 ve R2'de.

Çekirdek hazır: bu DLL'in yapacağı tek şey Unidrv tamponunu `MonoBitmap`'e
sarıp `EscPosWriter::writeRaster` çağırmak ve sonucu spooler'a yazmak olacak.
