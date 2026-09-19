# İmzalama

Bu klasöre **private key veya .pfx konulmaz.** Sertifika USB token ya da bulut
HSM'de durur; build makinesi sertifikaya Windows sertifika deposu üzerinden
erişir.

## Geliştirme

```bat
makecert -r -pe -ss PrivateCertStore -n "CN=NuxPrint Test" NuxPrintTest.cer
inf2cat /driver:. /os:10_X64,7_X64
signtool sign /s PrivateCertStore /n "NuxPrint Test" /fd sha256 NuxPrintThermal.cat
bcdedit /set testsigning on     REM YALNIZCA test VM'inde
```

## Üretim

Agent ile aynı OV kod imzalama sertifikası kullanılır:

```bat
infverif /w NuxPrintThermal.inf
inf2cat /driver:. /os:10_X64,7_X64
signtool sign /sha1 <THUMBPRINT> /fd sha256 /tr http://timestamp.digicert.com /td sha256 NuxPrintThermal.cat
signtool verify /pa /v NuxPrintThermal.cat
```

Zaman damgası zorunludur. Test imzalama üretimde kullanılmaz.
