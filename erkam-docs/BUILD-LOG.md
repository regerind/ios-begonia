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
| y105 | 2026-11-20 00:0x | y104 + doğru derlenmiş profiler; önbellek ikinci açılışta dolu | Profiler: task_for_pid başarılı ama port işe yaramıyor (task_threads 0x10000003; platform süreçlerinin kontrol portu). Kare 8,3 ms (GL içinde 7 ms), %80'i 18–35 ms: önbellek doluyken de yavaş, gerileme önbellekten görünüyor |
| y106 | 2026-11-20 00:1x | Profiler `task_read_for_pid` ile; `glenv=MESA_SHADER_CACHE_DISABLE=true` (shader önbelleği kapalı, karşılaştırma için) | Önbellek kapalı: karelerin %56'sı 18 ms altında (y105 açık: %9). Önbellek gerilemenin sebebi. Profiler iş parçacıklarını görüyor ama ARM_THREAD_STATE64 reddediliyor (4). `buttons 0`: oturumda hiç tuş olayı yok (y101'de 24, y102'de 4). Kullanıcı: ekran kilitlenmiyor, paneller aşırı yavaş |
| y107 | 2026-11-20 00:3x | Shader önbelleği varsayılan kapalı (`glcache=1` ile açılır); profiler birleşik (unified) durum biçimiyle | Kareler %82'si 18 ms altında (önbellek kapalı: iyi). `buttons 4` ama kullanıcı: güç tuşuyla kilitleme çalışmıyor. Paneller hâlâ çok yavaş. Profiler: iş parçacığı portu dönüştürülmüyor (4; iOS platform süreçlerini korur) |
| y108 | 2026-11-20 00:4x | `cs_relax_platform_task_ports=1` (geliştirme çekirdeğinin kendi anahtarı; yalnızca test, profiler Apple süreçlerini okuyabilsin) | Profiler **çalıştı**. Ama Apple'ın gizli fonksiyonları `<redacted>`, bizim kütüphaneler çıplak adres. Oturumda panel kaydırılmadı (yalnızca kilit açma betiği) |
| y109 | 2026-11-20 01:0x | Profiler: kütüphane+ofset ve framework!ofset adları, chronod ve WebContent de örnekleniyor; otomatik betik `y_script_panes.txt` (kilit açma, Bugün paneli, Uygulama Arşivi, `com.hakios.open.video` MP4) | Açılış sonrasında (34–55 sn) paneller sakin: SpringBoard 4–19%, chronod 2–19%. Ağır yük (SpringBoard 100–200%, çoğu dyld) açılışın ilk 10–20 sn'sinde. backboardd: köprünün `surface_changed`'ı (her karede ~1 ms, 4–6 MB okuma; 17.000 kontrolde 1 gerçek değişiklik) ve 1080p '420v' video kareleri CPU'da kopyalanıyor (24 karede 71 MB). WebKit.Networking: `recvmsg` + boringssl (TLS) |
| y110 | 2026-11-20 01:3x | Video kareleri GPU ile düzlem düzlem paylaşılıyor (Mesa 0007, userland 0047: Y → GL_R8, CbCr → GL_RG8; `glplanes=0` eskisi) | (sonuç bekleniyor) |
