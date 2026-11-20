# Nasıl çözdük

Her konu: belirti, kök neden, çözüm, ilgili dosyalar. Commit'ler ayrıntıyı tutar.

## Wi-Fi
- **Belirti:** Wi-Fi hiç yoktu, sonra ağlar görünmüyordu.
- **Kök neden:**
  - Sürücüde MT6781'in PMIC (güç yönetimi) ve pin değerleri vardı. Yanlış yazmaçlara yazıyordu, VM18 hattına dokunuyordu.
  - VCN33_1 hattı 3,5 V'a ayarlıydı.
  - Kalibrasyon cevapsız kalıyordu: ROM yaması (PDA) yüklenmemişti.
  - Açılış olayları yanlış okunuyordu: RXD halkası 16 olmalıydı.
- **Çözüm:**
  - Platform katmanı MT6785'e taşındı (3635cab).
  - PMIC erişimi ortak kilitle yapılıyor (`begonia_pwrap_take/give`).
  - Senin onayınla: `begonia_wifi_rails=1` (VCN18 + VCN33_1 açma bitleri), `begonia_wifi_rails=2` (VCN33_1 kapalıyken 3,5 V → 3,3 V).
  - ROM yaması yükleniyor (70cd3db). Açılış RXD 16 (e3740a4). Uyarlanır yoklama (7a51e32).
- **Dosyalar:** `kernel/bsd/dev/begonia_wifi.c`, `begonia_wifi_fw.c/.h`.
- **Açık kalan:** Video sırasında WebKit ağ süreci 1 çekirdek yiyor (açık hata 4).

## Parlaklık
- **Belirti:** Kaydırıcı ekranı değiştirmiyordu, sonra kaydırıcı kilitliydi.
- **Kök neden 1:** Bu telefonun parlaklığı panelin DCS 0x51 komutu ya da ekran PWM'i değil. Parlaklık, I2C0 0x11'deki
  LM36273 çipinin yazmaçlarında: BB_LSB 0x04 / BB_MSB 0x05 (11 bit), BL_ENABLE 0x08 (LK'nin değeri 0x17), BC1 0x60 (PWM kapalı).
- **Kök neden 2:** iOS (CoreBrightness) `AppleARMBacklight` sınıfında, `IODisplayParameters` yayınlayan bir IOService arıyor.
  Bulamayınca kaydırıcı hareket etmiyor ve backboardd 0,5'te kalıyor (y93).
- **Çözüm:**
  - `begonia_backlight.c`: 0..255 seviye, karesel eğri, en az 8/2047.
  - `BegoniaBacklight.cpp`: `AppleARMBacklight` servisi (brightness 0..65536). iOS'un yazdığı değer LM36273'e gidiyor (y94).
  - Kilitte ekran: SpringBoard'ın `com.apple.springboard.hasBlankedScreen` bildirimi → arka ışık 0, açılınca eski seviye (0043).
- **Sadece bu çipin 0x04, 0x05, 0x08 yazmaçları yazılır;** panelin ±5,5 V besleme yazmaçlarına (0x09..0x0e) dokunulmaz.

## Ses
- **Ahize (y94–y99):**
  - Yol: AFE'nin DL1 bellek arayüzü, 64 KiB halka tampondan (0xab380000) çalıyor → DAC bağlantısı → MT6359 kodeğinin receiver çıkışı.
  - Darwin'in `/dev/hakios-audio`'ya yazdığı ses, çekirdekteki "sink" ile tampona giriyor. Çalınan kısım 20 ms'de bir zamanlayıcıyla sessizleştiriliyor.
  - Onaylı: ses güç alanı (SCP_SYS_AUD), ses saat seçicileri, VAUD18 LDO açma biti (`begonia_audio=1`).
- **y94 paniği (YouTube'da donma):** Sink, hakios.audio.c'nin `audio_lock` kilidi tutulurken çağrılıyor. Sink'in içinden
  `hakios_audio_volume()` aynı kilidi tekrar istedi → kernel panic. Çözüm: ses seviyesini zamanlayıcı okuyor.
- **y95–y98, 1 saniye çalıp kesilme:**
  - Önce okuma konumu yazmacından (DL1_CUR) şüphelendik; y97'de doğru olduğu görüldü.
  - Gerçek neden: sesteki bir boşlukta okuma konumu yazma konumunu geçince, "yazıcı geride" durumu "tampon dolu" sanılıyordu (39 sn'nin 34'ü reddedildi).
  - Çözüm (y99): yazıcı en fazla yarım tampon önde tutuluyor. Daha fazlası "geride kaldı" demek ve 40 ms ileriden yeniden başlıyor.
- **Alt hoparlör (y100):**
  - TFA9874, I2C6 0x34. Xiaomi'nin `tfa98xx.cnt` dosyası 646 bayt: DSP programı yok ("9874_dummy.patch").
  - Dosyadaki aygıt listesi (gizli yazmaç anahtarı + 47 bit alanı) ve "speaker" profili (48 kHz) koda geçirildi.
  - Başlatma: I2C reset → ayarlar → MANSCONF → AMPE → PWDN=0.
  - Besleme: AFE'nin I2S3 çıkışı (GPIO67 LRCK, 68 DO, 71 BCK; AFE_I2S_CON3 48 kHz, 32 bit). I2S saati amfiden önce açılıyor, çünkü amfinin PLL'i ona kilitleniyor.
- **Çift hoparlör ve aramalar (y100–y101):**
  - Sol kanal alt hoparlöre, sağ kanal ahizeye.
  - `kern.begonia_audio_route`: 0 ikisi, 1 ahize, 2 hoparlör.
  - y100'de ahize duyulmadı (0 dB, yarım seviye). y101'de tam seviye ve +4 dB yapıldı.
  - SpringBoard her saniye CallKit'e bakıyor: aramada yalnızca ahize; arama ekranında "Hoparlör" seçiliyse ikisi (0045).
- **Dosyalar:** `kernel/bsd/dev/begonia_audio.c`, `begonia_i2c.c` (`begonia_i2c_amp_ready`), `userland/patches/0045`.

## GPU bekleme (akıcılık)
- **Belirti:** Takılmalar, ısınma.
- **Çözüm:** GPU işini bekleyen çağıranlar önce 10 kez hızlıca bakıyor, sonra 50 µs uyuyor (`wait_job`,
  `begonia_mali_service.c`). Kullanıcı y79'da "çok akıcı" dedi; korunuyor.
- **Açık kalan:**
  - Panel donmaları ve videodaki yük başka kaynaklı (açık hatalar 1, 4, 5).
  - GPU kuyruğu: iki yol (backboardd'nin GL'i ve Metal istemcilerinin Vulkan'ı) aynı kuyruğa iş gönderiyor. GPU ~%35 meşgul; darboğaz CPU tarafında görünüyor.

## GPU: CPU'daki çizim maliyetini kaldırma (2026-11-20 gecesi, devam ediyor)
- **Ölçüm aracı:** `tests/proc_cpu_test.c` örnekleyici profiler. Her 10 ms'de SpringBoard, backboardd, WebKit ve
  chronod'un çalışan iş parçacıklarının konumunu okuyor. `cs_relax_platform_task_ports=1` gerekiyor; bu, geliştirme
  çekirdeğinin kendi anahtarı ve yalnızca test imajına giriyor. Adları bilgisayarda
  `C:\begonia-private\sh\samples_sym.py` çözüyor.
- **Shader disk önbelleği (y104–y106):** Denendi, geri alındı. Önbellek açıkken karelerin %9'u 18 ms altındaydı,
  kapalıyken %56. Varsayılan kapalı (`glcache=1`).
- **Video kareleri (y110):** 1080p '420v' kareler her karede CPU'da kopyalanıyordu (24 karede 71 MB).
  - Artık düzlem düzlem GPU'ya veriliyor: Y → GL_R8, CbCr → GL_RG8 (Mesa 0007, userland 0047).
  - backboardd video sırasında %85–93'ten %20–50'ye indi.
  - **Doğrulama sürüyor:** y111'de video alanı simsiyahtı. Paylaşımdan mı, yoksa videonun henüz yüklenmemesinden mi olduğu y112'de (`glplanes=0`) karşılaştırılıyor.
- **Değişiklik kontrolü (y111):** Köprü, kopyalanan yüzeylerin belleğini her karede tarıyordu (~1 ms, 4–6 MB).
  IOSurface'ın seed sayacı 17.000 kontrolde yalnızca 1 değişikliği kaçırmıştı. Artık seed değişmediyse 30 karede bir
  bakılıyor (0048). Kare başına GL maliyeti 4,2 ms'den 3,3 ms'ye indi.
- **YouTube'da 30 fps (y117–y143):**
  - Teşhis: GPU'yu backboardd (CoreAnimation'ın GL yolu) dolduruyordu.
    - Her bulanık arka plan, o ana kadar çizilmiş ekranı okuyor.
    - CoreAnimation aynı geçici dokuları her bulanıklıkta yeniden kullanıyor.
    - Mesa (panfrost) bu yüzden ana ekranı kare başına onlarca kez gönderip yeniden yüklüyordu.
  - İki düzeltme, ikisi birlikte etkili:
    1. Mesa 0009: okunmakta olan bir dokuya yazılacaksa, okuyan işi göndermek yerine dokuya yeni bellek verilir. panfrost bunu CPU yazmaları için zaten yapıyordu.
    2. Köprü 0051: bulanık arka planlar önceki karenin kopyasından okunur.
  - Sonuç: YouTube açıkken 60 fps'teki kareler %0–1'den %20–66'ya çıktı, karelerin yarısı ~18–20 ms. Sayfa, Denetim Merkezi ve ana ekran görüntülerde doğru.
  - Kapatma anahtarları: `glbackdrop=0`, `PAN_RENAME=0`.
  - Kalan: kare başına ~5 adet 1622×1525'lik arka plan yakalaması ve ana ekranın kendi ağır çizimi.
  - Ölçüm araçları: Mesa 0008 gönderme nedenlerini ve büyük grupları sayıyor; köprü bunları özetinde yazıyor; çekirdek GPU işlerini süreç başına sayıyor.
- **Kriptografi (y111):** Çekirdek, BCM2837 (Raspberry Pi 3) kart ayarıyla derlendiği için programlara "AES/SHA
  donanımı yok" diyordu. corecrypto ve boringssl yazılımda çalışıyordu: WebKit ağ sürecinin üçte biri TLS'teydi.
  Artık CPU'nun kendi ID_AA64ISAR0 yazmacından okunuyor (çekirdek yaması 0020). Etkisi henüz ayrıca ölçülmedi.

## I2C ve fener
- **I2C:** 10 bus için yoklamalı FIFO sürücüsü (`begonia_i2c.c`).
  - LK'nin hiç kurmadığı bus'lar "herkese cevap veriyordu". Çözüm: bus 5'in yapılandırması kopyalanıyor (`clone`).
  - Bütün I2C pinleri mod 1. Sensör pinleri GPIO61/62 mod 2'deydi (SCP), mod 1'e alındı.
- **Fener:** MT6360 (I2C5 0x34), FLED_EN 0x7e, akım FLED1_TOR 0x75. Denetim Merkezi'ne bağlı (0041).

## Kalın yazı, Safari ve Metal
- **Kalın yazı:** SpringBoard çöküyordu. EAGL ve CoreImage kancaları yetmedi; SpringBoard Metal istemcisi yapılınca düzeldi.
- **Safari:** Sayfalar siyahtı; WebKit GPU ve WebContent süreçleri Metal istemcisi yapıldı.
- **Google logosu gibi öğeler:** metal2vulkan framebuffer fetch'i reddediyordu → AcceptUnordered (bridge 0002, runtime 0001).

## Tap-to-wake (y95)
- Karanlık ekranda dokunuşlar yutuluyor; ekranı yalnızca güç tuşu açıyor (0044).
- Ekran karanlıkken dokunmatik 500 ms'de bir okunuyor (`begonia_touch.c`, `begonia_backlight_dark()`).

## backboardd'yi Metal'e taşıma: tek GPU yolu (2026-11-20, y204–y219, devam ediyor)

Hedef: ekranı birleştiren süreç (backboardd) de Metal ile çizsin; GL köprüsü devreden çıksın. Başlangıçta (y202/y203)
siyah ekran ve 0–1 kare/sn vardı. Sırayla bulunan nedenler ve çözümler:

1. **Kurulum: `metal=3` değil `metal=1` + istemci listesinde backboardd.** `metal=3` mediaserverd'a da Metal veriyor;
   o da çöküp yeniden başlayarak hem logu dolduruyor hem sistemi yavaşlatıyordu. "0–1 kare/sn"nin çoğu buydu (y204:
   7–8 kare/sn).
2. **Siyah ekran: takas GPU bitmeden gidiyordu.** CoreAnimation `waitUntilScheduled`'dan hemen sonra yüzeyi ekrana
   veriyor; Apple GPU'larında çekirdek GPU'nun bitmesini bekletir, bizde beklemiyordu ve her kare yeni bir yüzeyde
   olduğu için hep boş yüzey kopyalanıyordu. Çözüm (ön yüz): backboardd'de çizim yapan komut arabelleği için
   `waitUntilScheduled` GPU'nun tamamlanmasını bekliyor (`metal_swapwait=0` kapatır). y205: görüntü geldi.
3. **Framebuffer-fetch üçgen üçgen bölünüyordu.** CA'nın bütün gölgelendiricileri hedefi okuyor (programlanabilir
   karıştırma). PanVK v7 "sıralı ek erişimi"ni bildirmediği için köprü her üçgeni ayrı geçişe bölüyordu (karede ~180
   GPU işi). PanVK giriş eklerini zaten karo arabelleğinden okuyor; yama 0011 özelliği v7'de de bildiriyor.
4. **Her IOSurface kopyalanıyordu (asıl büyük maliyet).** PanVK `VK_EXT_external_memory_host` sunmadığı için çalışma
   zamanı her yüzeyi kendi belleğinde bir kopya olarak tutuyor, her komut arabelleğinde CPU ile içeri/dışarı
   kopyalıyor, üstündeki dokuları da her kullanımdan önce GPU ile "gölge" görüntüye yeniden kopyalıyordu (bir tanesi
   9 MB'lik duvar kâğıdı). Çözüm üç parça: PanVK 0013 (uzantıyı listeler, her işaretçiyi reddeder → çalışma zamanı
   dma-buf yoluna geçer); köprüde `nmb_buffer_new_no_copy_surface` (taşıma katmanının yüzey tanımlayıcılarını —
   çekirdeğin GL yolunda da kullandığı "yüzey → GPU tamponu" isteği — dma-buf kaynağı olarak verir); çalışma zamanı
   yaması 0003 (Linux dışında tanımlayıcıyı dosya boyutuyla kabul eder; yoksa her yanıt sessizce reddediliyordu).
   `metal_surfaces=0/1/2` (kapalı / yalnızca backboardd / hepsi; varsayılan 1). y211: 8 kare/sn, Denetim Merkezi doğru.
5. **CA'nın kendi bitmapleri.** Glif/maske/görüntü bitmapleri özel `newLinearTextureWithDescriptor:` çağrısıyla
   tampon üstü doku oluyor ve yine her karede gölgeye kopyalanıyordu. Çözüm (ön yüz, yalnızca backboardd): "ayna doku"
   — sıradan bir doku; commit'te baytların özeti alınır, yalnızca değiştiyse yüklenir. Özet büyük dokularda dönüşümlü
   olarak satırların sekizde birini kontrol eder (`metal_mirror=0` kapatır). Bilinen sınır: bir bitmap yerinde
   değişirse en geç 8 commit sonra görülür; tampona GPU ile yazılan değişiklik görülmez.

Doğrulama (çekimler): ana ekran (y205), Denetim Merkezi (y211, y218), Ayarlar (y212), Bugün paneli (y215) doğru
çiziliyor; backboardd hiçbir oturumda çökmedi, çökme döngüsü yok.

6. **Çeviricinin "Metal'le bit-kesin" öykünmesi.** metal2vulkan, ölçülmemiş her sürücüde Metal'in yuvarlama/NaN
   davranışını her gölgelendiricide hesaplıyor. Çalışma zamanı yaması 0004 PanVK için cihazın kendi dönüşümlerini kabul
   ediyor (18 "sürücü olgusu"). Kazanç küçük (%10–20); bedeli yuvarlama eşiklerinde cihazın kendi sonucu.

**Açık kalan: hız.** 5–10 kare/sn (GL yolunda animasyonlarda 45–60). Darboğaz GPU ve asıl neden CoreAnimation'ın Metal
gölgelendiricilerinin ağırlığı (hedefi okuyarak gölgelendiricide karıştırma, gürültüyle titreme, yarım duyarlık): tam
ekran geçiş ~25–30 ms (GL yolunda ≤3 ms); ayrıca PanVK her çizim için 2 ek "tanımlayıcı kopyalama" işi çıkarıyor (karede
~23 ms). Elenenler: boru hattı yeniden derleme, işleyicilerde kopya, AFBC/ön-kare gölgelendiricisi, geçişin kendi
maliyeti (çizimsiz geçiş 1–2 ms), kapsama denetimleri. Ayrıntı ve sonraki adımlar: ACIK-HATALAR 12.

**Deney anahtarları (önyükleme argümanları):** `metal_tele=1` (5 sn'de bir sayaçlar), `metal_swapwait=0`,
`metal_surfaces=0/1/2`, `metal_mirror=0`, `metal_contain=0`, `metal_nodraw=1`, `panvk_debug=<PANVK_DEBUG>`.
**Araçlar:** `C:\begonia-private\sh\tele.sh TAG` (sayaçlar), `tele_keys.py` (boru hattı anahtarları), `qc_from_key.py`
(CA boru hattını makinede çevirip komut sayar; `out/metal2vulkan-target-host/release/metal2vulkan`). Çekirdek, her 5 sn'nin
en yavaş fragment işinin çerçeve arabelleği tanımlayıcısını `BEGONIA GPU FBD` satırlarıyla yazdırır.

## Metal yolunda fazladan render pass'leri: ulaşım temizliği (2026-11-20, y249-y254)
**Sorun:** PanVK'da BeginRendering sayısı Metal render encoder sayısının ~1.6 katıydı; her pass tam ekranda ~1-2 ms sabit maliyet ve iş
başına ~0.5 ms GPU boşluğu demek.
**Kök neden:** encoder'lar bölünmüyordu (0005: inst==enc). Asıl kaynak metal2vulkan runtime'ının `clear_reach.rs`'indeki "ulaşım
temizliği": Metal'in Clear yükü render alanını 32 px karoya yuvarlayıp temizler; runtime bunu taklit etmek için, alan karo katı değilse,
tüm attachment'ı load+store eden AYRI bir render pass açıyordu (+ iki yanında `Barrier::everything()`), encoder'ların %25-34'ünde.
**Çözüm:** `userland/metal/runtime-patches/0008-reach-clear-margin-skipped.patch`: çizimli pass'te yalnızca yuvarlama payı (<32 px) için
bu pass açılmaz; ana alanı pass'in kendi load-op clear'ı temizler (karo tabanlı GPU'da bedava). Teşhis yamaları: 0005 (encoder başına
instance sayısı), 0006 (her render-pass başlangıç noktası sayacı), 0007 (nedenler).
**Doğrulama:** log (BeginRendering ≈ encoder, sites[3]=0, kare 38-55 → 52-63 aynı betikle) + ekran görüntüsü (Control Center doğru).
**Kalan şüphe:** Metal payı temizler, biz bırakıyoruz; payın içeriği hiçbir program tarafından sayılamaz ama atlas gibi alt-dikdörtgen
çizimlerinde komşu texel'ler artık temizlenmiyor (Metal'de siliniyordu). Resim şu ana kadar bozulmadı; Today/YouTube doğrulaması sürüyor.
Not: m2v konsol satırları her satır iki kez iç içe basılıyor ("33283328" = 3328).

## CPU ve GPU sırayla çalışıyordu: swap beklemesi çekirdeğe taşındı (2026-11-20, y260-y267)
**Sorun:** Metal yolunda GPU hep ~%50 meşguldü (iş başına ~640 us "ölü zaman"). Kernel tanılaması (BEGONIA GPU GAPS): iş öncesi hazırlık
6-8 us, kuyruk doluyken işler arası 54-183 us (toplam ~0.2-0.7 sn/5 sn); asıl boşluk (~2.3 sn/5 sn) kuyruk BOŞKEN: GPU iş bekliyor.
**Kök neden:** ön yüz (build_metal_frontend_ios.py, y205) backboardd'de çizim yapan command buffer'ın GPU'da TAMAMLANMASINI waitUntilScheduled
içinde CPU'da bekliyordu (siyah kare önlemek için: çekirdek swap'ı GPU bitmeden gösteriyordu çünkü Metal yolu yüzeyi "çizildi" diye
işaretlemiyor). Sonuç: CPU bir sonraki kareyi ancak GPU bitince kodlamaya başlıyor, CPU süresi + GPU süresi toplanıyor.
**Çözüm:** `patches/0023-framebuffer-wait-for-gpu-seq.patch` (boot-arg `fb_gpuseq=1`): SwapEnd'de çekirdek GPU servisinin son iş numarasını
(`hakios_mali_submitted_seq`, kernel/bsd/dev/begonia_mali_service.c) alır; ekran sürücüsünün sunum iş parçacığı o numaraya kadar
bekleyip kareyi gösterir (SWAP_GPU_MS=500). Ön yüzün CPU beklemesi `metal_swapwait=0` ile kapatılır (zaten vardı). İkisi birlikte kullanılmalı.
**Doğrulama:** log + resim. Seek betiği (aynı betik, y263 -> y264): 5 sn de 33-41 -> 57-60 kare (6-8 -> 11-12 fps), GPU meşgul %50 -> %75-84.
YouTube (y256 -> y266): 44-56 -> 55-67 kare, GPU %85. Control Center (y265), YouTube (y266), Today (y267) resimleri doğru, "shown as it is" yok.
**Kalan şüphe:** swap'ın beklediği numara SwapEnd anındaki global son iş; başka bir sürecin (WebKit) işleri de beklenir (sıralı kuyruk, zarar yok
ama gecikme). Süreç dışında yüzey işaretlemesi yapılmadı. Sürücü GPU'ya 500 ms'den fazla cevap vermezse kare "olduğu gibi" gösterilir.

## Indexed draw başına 3 fazladan GPU işi: PanVK indeks min/max'ı CPU'ya (2026-11-20, y268-y280)
**Sorun:** Slot 1 zincirlerinde her draw için `[write, compute, compute, vertex, tiler]` (iş türleri 2 4 4 5 7) vardı; 5 sn'de ~15000 compute + ~7000 write.
Desc kopyası değildi (PanVK 0017: GPU'ya giden kopya işi 0; 0014 CPU kopyası 116k çağrıda %100 başarılı). Runtime'ın kendi dispatch'leri de değildi
(0009: 80 sn'de ~1500).
**Kök neden:** PanVK JM `CmdDrawIndexed`, bir `VkDrawIndexedIndirectCommand` yazıp `panvk_cmd_draw_indirect`'e gidiyor. O yol, indeks aralığını bulmak için
HER indexed draw'da (1) bir WRITE_VALUE işi, (2) tüm indeks tamponunu tarayan bir min/max compute işi ve (3) vertex/tiler işlerini yamalayan indirect-helper
compute işi çalıştırıyor. CoreAnimation draw'larının çoğu indexed (quad).
**Çözüm:** `userland/vulkan/patches/0018-panvk-cpu-index-minmax.patch`: tampon bir belleğe bağlıysa ve o bellek CPU'ya eşlenmişse (`mem->addr.host`) ve
indexCount <= 8192 ise, `CmdDrawIndexed` en küçük/en büyük indeksi (primitive restart değerini atlayarak) CPU'da bulup min/max sonuç alanına yazar;
write ve search işleri hiç eklenmez, yalnız helper compute kalır (5 iş -> 3). Kapatma: `PANVK_CPU_INDEX_MINMAX=0`. Eşlenmemiş bellekte eski GPU yolu.
**Doğrulama:** log (zincir örüntüsü `4 5 7`, write 0; seek betiği 5 sn'de 57-60 -> 78-110 kare, ~11 -> 15-20 fps) + ekran görüntüsü (Control Center y276,
Today y278, YouTube y280 doğru). YouTube 5 sn'de 50-71 kare (y266: 55-67; GPU %83-87, orada GPU darboğaz).
**Kalan şüphe:** min/max KAYIT anında okunuyor; indeks verisi kayıttan sonra, GPU çalışmadan önce değişirse (Metal'de CPU commit'e kadar yazabilir)
yanlış vertex aralığı olur (eksik/çöp köşe). Şu ana kadar resimde bozulma görülmedi. y272 ve y274'te backboardd SIGKILL (status 0x9) ile yeniden
başladı (y245'te de olmuştu); tekrar koşuda (y273, y275) olmadı, yamayla ilişkisi KANITLANMADI ama dışlanmadı: ~16 koşuda 2 kez.
