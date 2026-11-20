# Analiz: backboardd Metal'de neden yavaş, ne yapılabilir (2026-11-20 akşamı)

Bu belge y249-y288 ölçümlerinin bütününe bakar. Telefon şarjdayken cihaza dokunmadan, eldeki verilerle yazıldı.

## 1. Şu anki durum (sayılar)
- Seek betiği: 7-8 fps (sabah) -> 15-20 fps. YouTube: 8-11 -> 10-14 fps. GL yolu: 45-60 fps.
- YouTube'da GPU %83-87 meşgul. Yani artık CPU/GPU sıralılığı ya da iş sayısı değil, **GPU'nun kendi işi** sınırlıyor.
- Kare başına yaklaşık 50 render pass, bunların ~13'ü tam ekran (1080x2340). Tam ekran pass'ler 3-19 ms (en ağırları 12-19 ms), kare başına ~60 ms.
- Kare başına GPU işi yaklaşık 70-80 ms, bu da ~12-14 fps eder. 30 fps için GPU işini ~2x, 60 fps için ~5x azaltmak gerekir.

## 2. Büyük resim: fark GPU'nun işinin ağırlığı
- y219 notu: aynı tam ekran pixel işi GL'de ~3 ms, Metal'de ~30 ms (10x). Bugünkü 14 ms ağır geçiş hâlâ ~4-5x.
- GL yolu hızlı çünkü CoreAnimation'ın GL çizicisi çok daha hafif shader kullanıyor (sabit işlevli blend, dither yok, yarım duyarlık
  dönüşümleri yok, framebuffer fetch yok) ve köprüde kısayollar var (backdrop'u önceki kareden okumak 0051, geç temizleme 0054).
- Metal çizicisi gerçek bir iPhone'un A15 GPU'su için yazılmış: tam ekranda ~150-190 komutluk shader (`fixed_frag_lph_cpf`), piksel başına
  doku koordinat işlemleri (floor/yuvarlama/bölme ~40 komut), blend yolu, dither kuyruğu (~15 komut), yarım duyarlık dönüşümleri.
  Mali-G76 MP4'te (806 MHz) bu ALU sınırlı: 2.5 Mpiksel x ~150 komut ~ 14 ms, yani donanımın makul verimde yapabileceği kadar.
- Sonuç: bu GPU'da Metal yolundan UI için 60 fps beklemek gerçekçi değil; kazanç ancak (a) piksel başına komut sayısını düşürmekten,
  (b) tam ekran pass sayısını düşürmekten, (c) GPU saatinden gelebilir.

## 3. Kapanan yollar (kanıtla)
- **fc_blend_function=0 ile shader'ı küçültmek:** offline çeviri (qc_from_key, Bsov anahtarı) 154 -> 99 komut gösterdi, AMA kısaltılmış
  shader'ın çıktısı sabit 0 (`NClamp(0,0,1)`): CA'nın asıl renk hesabını komple siliyor. Yani eski "shader içi blend maliyetin ancak %25-30'u"
  sonucu yanlış bir karşılaştırmaydı (y237'deki yanlış resim de bundan). Bu sabit rastgele kapatılamaz.
- Descriptor kopyası GPU'da (0014 CPU'da çalışıyor, GPU'ya giden kopya işi 0), runtime dispatch'leri (80 sn'de ~1500), encoder bölünmesi (yok),
  CPU-GPU sıralılığı (giderildi), indeks min/max (giderildi). Bunların hepsi artık bitti.

## 4. Açık kaldıraçlar (tahmini kazanç, risk)
| # | Fikir | Tahmini kazanç | Risk |
|---|-------|----------------|------|
| A | Bifrost kodunun gerçek döngü sayısını ölçüp (offline derleyici) hangi komut gruplarının ağır olduğunu görmek; ardından çeviri tarafında güvenli sadeleştirme (gereksiz half<->float dönüşümleri, her piksel yeniden hesaplanan üniform bölmeler) | %10-30 | düşük-orta (resim farkı yok ya da çok küçük) |
| B | CA'nın tam ekran pass sayısını azaltan davranışı bulmak (ör. aynı hedefe art arda yazan geçişleri tek geçişte birleştirmek, runtime seviyesinde) | %10-25 | orta |
| C | ~~GPU saati artırmak~~ | - | GEÇERSİZ: GPU zaten 806 MHz (MFGPLL 806000 kHz), bu SoC'nin üst frekansı; yükselecek yer yok |
| D | Bellek baskısını azaltmak (jetsam): fb_queue, WebContent bellek sınırı, backboardd 480 MB dökümü | hız değil, kararlılık | düşük |
| E | ~~backboardd'i GL'de bırakmak~~ | - | **İPTAL (sahibinin kararı, 2026-11-20): tek Metal hattı kalır.** Gerekçe: çift hat (GL köprüsü + metal2vulkan) CPU/senkronizasyon yükü ve iki kod tabanı getiriyor. Not: çift hattın CPU maliyeti bu oturumda ÖLÇÜLMEDİ, karar sahibinin |

## 5. Önerilen sıra
1. (A) ölçüm: shader başına gerçek Bifrost döngüleri.
2. (D) kararlılık: jetsam'i azaltmadan kalıcı imaj güvenilmez.
3. (B) pass birleştirme araştırması.
4. C geçersiz, E iptal: tek Metal hattı, yol haritası A -> D -> B (sahibi onayladı).

## 6. Ek bulgular (offline, y254-y275 loglarından)
- **Pass yapısı:** her komut buffer'ı tipik olarak iki pass ile başlıyor: (1) 1082x2244 "own" (CA'nın kendi dokusu) Clear+Store, (2) 1080x2340 ekran yüzeyi
  Clear+Store; bazılarında 64x64 küçük pass ve ekranı Load ile yeniden gezen üçüncü bir geçiş var. Commit başına ~7-9 render encoder (y275: 5 sn'de 72-121 commit,
  +503/+1059 encoder), draw ~100/commit. Yani kare başına ~1 commit, ~8 pass, ~100 draw.
- **Zaman nerede:** y258 pass süresi koşusunda (>=3 ms olanlar) toplam 5625 ms'nin 5017 ms'si (%89) 1080x2340 hedeflerinde; ortalama 7.5 ms/geçiş. 1622x1525 ara geçiş
  327 ms (4.7 ms). Küçük pass'ler zaman olarak önemsiz, iş sayısı olarak önemli (iş başına 34-63 us boşluk, kare başına ~%3-6).
- **c1 (ikinci renk eki):** her pass'te memoryless, DontCare yüklü, saklanmayan, pipeline anahtarlarında yok (yalnız `c0=` var). Hiçbir pipeline yazmıyor/okumuyor.
  Tile 16x16 zaten üst sınır; kaldırmanın kazancı düşük beklenir (denenmedi).
- **Pipeline blend durumu:** anahtarların ~%40'ı (`...Bsov...`, `Badd`) donanım blend'i açık (`c0=80:1:1:5:1:5` = ONE, ONE_MINUS_SRC_ALPHA), %25'i kapalı (`80:0:1:0:1:0`),
  hepsinde `fc_blend_function=0x36` ve `fc_framebuffer_fetch=1`. Bsov shader'ı çevirisinde `OpImageRead` YOK (155 gövde komutu): yani fetch=1 olması bu varyantta
  dst okumuyor; `fc_blend_function` shader'ın asıl renk yolunu seçiyor (0 yapınca çıktı sabit 0).
- **Jetsam:** `jetsamproperties.D22` ve cihaz ağacı `target-type D22`, `compatible D22AP`: öncelikler doğru kurulu (backboardd 170, SpringBoard 160, mediaserverd 140,
  WebContent/GPU 90). Yani backboardd ancak bellek gerçekten tükenince öldürülüyor. Wired (GPU/yüzey) ~1.4 GB kullanıcı alanına ait ve killable değil; asıl çare
  bellek kullanımını azaltmak. Süreç footprint: WebContent ~800 MB (büyüyor), backboardd ~480 MB.

## 7. Telefon döndüğünde yapılacaklar (sırayla, hazır)
1. `y289` imajı hazır (flaşlanmadı): `bifrost_debug=shaderdb` ile Control Center betiği. Çıktı: her shader için komut/tuple/clause/döngü. Amaç: A kaldıracını sayılarla başlatmak.
2. Sonuca göre: en ağır CA shader'larında (instr/cycle) çeviri sadeleştirmesi; ya da pass birleştirme.
3. Jetsam: `fb_queue=1` ve yüzey sayısı denemesi; backboardd belleğinin dökümü.
4. C ve E kapandı (bkz. tablo): tek Metal hattı, sıra A -> D -> B.

## 8. Shader dağılımı (offline çeviri, 79 farklı CA pipeline anahtarı)
- Ortalama gövde komutu 146 (SPIR-V, specialization katlanmadan önce). Ağır aile: **blur çekirdekleri**: `Tn27` 1263 komut (14 doku örneği, tap başına ~90),
  `Tsb3` 461, `Tnb7` 445, `Tds4` 424, `Tmua` 391, `Timg` 278; hafif aileler 30-60 komut. Ağır tam ekran pass'lerin (12-19 ms) bu blur shader'ları olması çok olası.
- **Dikkat:** sampler/doku bayrakları SPIR-V'de specialization constant (`OpSpecConstant`, `OpSelect` ~219 adet Tn27'de); PanVK/NIR bunları pipeline derlemesinde
  katlıyor. Yani SPIR-V komut sayısı Bifrost komut sayısı DEĞİL (spirv-opt -O yalnız 126 -> 118). Gerçek sayı için `bifrost_debug=shaderdb` (y290) gerekli.
- **Pass birleştirme (B) düşük potansiyel:** komut buffer'larında ekran pass'leri arasında hep farklı hedefli pass var (1082x2244 "own" ve 64x64), yani art arda aynı
  hedefe yazan encoder çifti çok az. API düzeyinde birleştirme kazancı ~0 beklenir; B'yi A'nın arkasına bırakmak doğru.
- İndexed draw'da kalan 1 compute (indirect helper) CPU'ya alınabilir ama direct yol indeks desteklemiyor (tiler `indices`, vertex aralığı, öznitelik yamaları
  libpan'daki `panlib_patch_draw` mantığı); tahmini kazanç %5-8, çalışma büyük: sonraya.

## 9. Host ölçümü: gerçek Bifrost sayıları ve ilk shader kazancı (2026-11-20, ~20:15, cihaz yok)
Araç: `tests/bifrost_stats.c` (+ `tests/claim.inc`, `tools/build_bifrost_stats_linux.sh`). CA pipeline anahtarı -> `qc_from_key.py` (metal2vulkan CLI) -> SPIR-V ->
Mesa'nın `spirv_to_nir` + Bifrost v7 derleyicisi (host), `BIFROST_MESA_DEBUG=shaderdb`. Descriptor'lar kabaca indirgenir: sayılar komut/döngü saymak içindir.
Sınırlar: 79 anahtarın bir kısmı araçta derlenmiyor (input attachment/framebuffer fetch yolu indirgenmedi), sampler specialization sabitleri varsayılan
değerde (0) alındı; yani tablo blur ailesi için güvenilir, `fixed_frag` varyantları için değil.

**Bulgu (kök neden, A kaldıracı):** metal2vulkan bir Metal buffer'ını `uint` dizisi olarak okuyor ve ofseti push-constant'tan alıyor: adres = `(pc_kelime & ~3) + sabit`.
NIR ofsetin 16'nın katı olduğunu bilemiyor, Bifrost her yüklemenin kendi boyutuna hizalı olmasını istiyor (`mem_vectorize_cb`), bu yüzden her float AYRI bir yükleme
oluyor. 14 örnekli blur'da 22 yükleme var ve shader yükleme (ldst) ile sınırlı:

| Shader | önce (komut / döngü / arith / ldst) | 16 hizalı (0019) |
|---|---|---|
| `single_pass_blur_3` (Tsb3) | 142 / 12.0 / 3.5 / 12 | 130 / **6.0** / 3.4 / 6 |
| `narrow_blur_7` (Tnb7) | 148 / 8.0 / 3.8 / 8 | 142 / **5.0** / 3.8 / 5 |
| `narrow_blur_27` (Tn27) | 371 / 21.0 / 9.9 / 21 | 345 / **9.6** / 9.6 / 8 |
| `fixed_frag` Timg (sıradan resim) | 85 / 3.0 / 2.0 / 3 | 85 / 3.0 / 2.0 / 3 (değişmedi) |

Yani en ağır tam ekran shader'larında derleyicinin döngü tahmini 1.6-2.2x düşüyor. Bu derleyici tahmini; cihazdaki süre doku/bellek ile de sınırlı olabilir, ÖLÇÜLMEDİ.

**Yama:** `userland/vulkan/patches/0019-panvk-uniform-loads-16-aligned.patch`. Yalnız fragment shader; yalnız "push-constant kelimesi & ~3 (+ sabit)" kalıbı; yüklemeye
`align 16` yazılır, Mesa'nın kendi vektörleştiricisi komşuları 16 baytlık tek yüklemeye toplar (adres değişmez). Dayanak: Metal, buffer ofsetinin veri tipine
hizalı olmasını ister; CA'nın 16 kullandığı VARSAYILDI, kanıtlanmadı. Bu yüzden çizimde denetim var: kelime 16'nın katı değilse `panvk: begonia: buffer offset word
... not a multiple of 16` satırı basılır (ilk 16). `PANVK_NO_UBO_ALIGN16` ortam değişkeni (boot: `ca_env=PANVK_NO_UBO_ALIGN16`) hepsini kapatır.
Risk: hizasız ofset gelirse o çizimin uniform'ları yanlış okunabilir (yanlış renk/blur), çökme beklenmez.

## 10. Doku düzeni (kod okuması, cihazda doğrulanmadı)
- CA'nın kendi dokuları (1082x2244 "own", ara hedefler, ayna dokuları): `VK_IMAGE_TILING_OPTIMAL` -> v7'de U-interleaved (AFBC değil: sRGB ikizli format listesi
  `MUTABLE_FORMAT` getiriyor, PanVK v7'de bunu AFBC'den dışlıyor).
- IOSurface dokuları (1080x2340 ekran yüzeyi ve derleyicinin örneklediği uygulama yüzeyleri): dma-buf, açıkça `DRM_FORMAT_MOD_LINEAR`, yerinde. Yani her karede
  tam ekran LINEAR hedefe çiziliyor ve LINEAR kaynaklardan örnekleniyor. Blur'ların hangisini okuduğu bilinmiyor.
- Forward pixel kill, shader yazdığı eki okuyorsa kapanıyor; bütün CA pipeline'larında `fc_framebuffer_fetch=1`. CRC/transaction elimination PanVK'da hiç açık değil.
- Bunları değiştirmek (ekranı kendi U-interleaved dokuya çizip karede bir kez linear yüzeye kopyalamak; own dokularda AFBC) ayrı işler: YAZILMADI.

## 11. Dört katmanlı planın durumu (dürüst döküm)
| Katman | Durum |
|---|---|
| 1. Shader | 0019 yazıldı, host'ta ölçüldü (yukarıda). CA cihaz profili (A9/A10) incelenmedi. half<->float / bölme sadeleştirmeleri yazılmadı. |
| 2. Pass birleştirme / OPTIMAL yüzey önbelleği / [vertex,tiler] zinciri | Yazılmadı. Pass birleştirme için loglar düşük potansiyel gösteriyor (bölüm 8). |
| 3. Kare sırası | `fb_queue=2` çekirdekte zaten var (0023 ile birlikte); ayrıca ölçülmedi, yeni kod yok. |
| 4. Bellek/jetsam, boş kare atlama | Yazılmadı. Kaynak dökümü için y290'ın GPU VA sayacı hâlâ koşulmadı. |
Host'ta iz yeniden oynatma (trace replay) ve ASan bellek testi YOK: elde yakalanmış kare izi ve Vulkan'ı cihazsız koşturan bir düzenek yok.

## 12. 0019'un varsayımı Metal bildirimiyle DOĞRULANMADI (önemli)
`tools/air_buffer_args.py` (default.metallib -> llvm-dis) CA shader'larının buffer argümanlarının bildirilen hizalarını verdi:
- blur'lar (`narrow_blur_7/27`, `single_pass_blur_3`): `u` = `NarrowBlurUniforms`, boyut 88, **hiza 8**; `edr_scale` half, hiza 2.
- `fixed_frag_lph_cpf`: `uniforms` (CA::OGL::Metal::Uniform) boyut 176 hiza 16; `config_buffer` boyut 52 **hiza 4**; `lod_bias` float hiza 4; `colorP` float4 hiza 16.
Metal yalnızca "ofset tipin hizasının katı" der. Yani blur uniform'ları için garanti 8, 16 değil. CA'nın ayırıcısı her şeyi 16'ya hizalıyorsa 0019 doğru çalışır;
tip hizasına göre ayırıyorsa (ör. 4 baytlık `lod_bias`'tan sonra 8 mod 16) blur uniform'ları yanlış okunabilir. Bu ancak cihazda görülür ("not a multiple of 16" satırı).
Doğru çözüm: çevirici (metal2vulkan) ofset maskesini argümanın bildirilen hizasından üretsin (`& ~(hiza-1)`), PanVK hizayı maskeden okusun; hiza 8 ile blur'da
yüklemeler ikişer birleşir (tahmin: Tn27 ldst 21 -> ~11). Çevirici normal buffer'larda hizayı hiç kullanmıyor; bu değişiklik YAZILMADI.
Bu yüzden iki kalıcı imaj hazırlandı: `p13` (0019 açık) ve `p13n` (aynı imaj, `ca_env=PANVK_NO_UBO_ALIGN16` ile 0019 kapalı = p12 davranışı).

## 13. Beklentiyi sınırlayan hesap (host sayılarından)
- En yaygın katman shader'ı (`fixed_frag` Timg, Bsov): 85 komut, **3 döngü/piksel** (varsayılan sampler sabitleriyle). 14 örnekli blur: 21 döngü.
- Kaba model: 2.5 Mpiksel / 4 (quad) x 21 döngü = ~13 M döngü; MP4 x 3 motor x 806 MHz ile bu ~1.5 ms eder. Ölçülen ağır pass 12-19 ms, ortalama tam ekran 7.5 ms.
  Yani ölçülen süre saf ALU modelinin 5-10 katı: darboğazın önemli kısmı bellek/doku tarafında olmalı (LINEAR yüzeyler, bölüm 10; uniform yüklemelerinin her
  pikselde belleğe gitmesi de buna dahil). 0019 yükleme sayısını azaltır ama kazanç derleyici tahminindeki 2x'ten küçük çıkabilir. Cihazda ölçülmeden iddia edilemez.
- Çeviricinin tasarımı (metal2vulkan `memory/views.rs`, `bind/instantiate.rs`): descriptor tabanı 16'ya hizalanır, push-constant'taki kelime ofsetin 16'ya göre
  KALANI. Yani 0019'un koşulu "Metal ofseti 16'nın katı" ile aynıdır; çevirici AIR hizası >= 16 olan 16 baytlık yaprakları zaten tek `uvec4` okur, hiza 8 olan
  blur yapısını okumaz.
