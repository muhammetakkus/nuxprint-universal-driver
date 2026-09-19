# NuxPrint Universal Thermal Driver

58 mm ve 80 mm ESC/POS uyumlu termal fiş yazıcılarını tek bir Windows driver'ı
ve tek kurulumla çalıştırmayı hedefleyen proje.

**Durum: Faz 1.** Mimari kararlar alındı, platformdan bağımsız ESC/POS çekirdeği
yazıldı ve test edildi, Device Inspector yazıldı. Driver paketi (INF/GPD/renderer)
henüz build edilmedi.

| Katman | Durum |
|---|---|
| `core/escpos`, `core/raster` | Yazıldı, **host'ta test edildi** (56 test) |
| `tools/device-inspector` | Yazıldı, **derlenmedi** (Windows gerekiyor) |
| `driver/inf`, `driver/gpd` | Taslak, **doğrulanmadı** |
| `driver/renderer` | Yazılmadı (Faz 2, PoC sonrası) |
| `installer`, `device-setup` | Yazılmadı (Faz 6) |

Mimari ve fizibilite: [docs/architecture.md](docs/architecture.md)
Build: [docs/build.md](docs/build.md)
Uyumluluk: [docs/compatibility.md](docs/compatibility.md)

Özel anahtar veya sertifika bu depoya **hiçbir koşulda** eklenmez; bkz.
`signing/README.md`.
