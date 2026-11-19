# Build log

Her build bir satır. **Sonuç** sütunu oturum bitince doldurulur. y94'ten öncesi için `docs/SESSION-2026-11-19*.md`.

- Loglar: `C:\begonia-private\logs\<tag>-console.txt`
- Test imajları bir kez iOS açar, sonra kurtarma çekirdeğine düşer. Aynı imaj iki kez çalışmaz; yeni bir tag gerekir.

| Build | Tarih | Değişiklik | Sonuç |
|---|---|---|---|
| y94 | 2026-11-19 22:19 | `AppleARMBacklight` servisi (ilk derleme bir include eksikliğiyle başarısız oldu, düzeltildi); ses: Darwin'in sesi ahizeye | Parlaklık kaydırıcısı **çalıştı**. YouTube'da donma: kernel panic (ses kilidi iki kez istendi) |
| y95 | 22:30 | Panik düzeltmesi; tap-to-wake kapalı; oturum 60 sn'ye döndü | Panik yok. Ses 1 sn çalıp döngüye girdi ("sink full": 14,7 sn'nin 12,9'u reddedildi) |
| y96 | 22:37 | DL1 konumu yazmaç geçersizse zamandan; DL1 yazmaçları loglanıyor | Oturumda ses çalınmadı (veri yok) |
| y96r | 22:40 | Aynı imaj yeniden flaşlandı | iOS açılmadı: tek seferlik kilit (aynı imaj ikinci kez çalışmaz) |
| y97 | 22:44 | y96'nın aynısı, yeni kilit (`run=2`) | 1 sn ses. DL1_CUR doğru (0xab380640); 39 sn'nin 34'ü reddedildi |
| y98 | 22:48 | DL1 durumu 0,5/1/2/4/8 sn'de not ediliyor | Donanım sorunsuz akıyor; yazıcı geride kalınca tampon "dolu" sanılıyor (kök neden bulundu) |
| y99 | 22:52 | Boşluk sonrası 40 ms ileriden yeniden başlama; proc_cpu_test (süreç CPU) | **Ahizeden ses çalıştı** (kullanıcı). CPU: panel sırasında SpringBoard 96–198%, chronod 72%; videoda ~4 çekirdek |
| y100 | 23:0x | TFA9874 alt hoparlör; sol/sağ kanal; CallKit rotası; backboardd iş parçacıkları (ilk derleme 0045'teki kaçış hatasıyla başarısız oldu) | **Alt hoparlör çalıştı**; ahize duyulmadı. backboardd: display.primary 40–75% |
| y101 | 23:2x | Ahize tam seviye + receiver +4 dB; aramada "Hoparlör" seçimi; not alanı 8 KiB | **Üst ve alt hoparlör çalışıyor** (kullanıcı). Safari.History 5 kez çöktü |
| y102 | 23:3x | proc_cpu_test: SpringBoard / chronod / backboardd / WebKit.GPU iş parçacıkları | Panelde SpringBoard ana iş parçacığı 80–94%; videoda WebKit.Networking 100–134% |
| y103 | 23:4x | Örnekleyici profiler (her 10 ms, fonksiyon adlarıyla) | Profiler hiç örnek toplamadı (sebep bilinmiyor) |
| y104 | 23:5x | Mesa shader disk önbelleği (GL): Mesa 0006, userland 0046; profiler tanılama sayaçları | Profiler yine yok (eski test ikilisi pakete girdi). Kare maliyeti arttı: GL içinde ~8 ms, kareler %93 18–35 ms bandında. Önbellekten mi, kullanımdan mı? Kontrol ediliyor |
| y105 | 2026-11-20 00:0x | y104 + doğru derlenmiş profiler; önbellek ikinci açılışta dolu | (sonuç bekleniyor) |
