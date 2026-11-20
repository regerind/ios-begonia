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
| C | GPU saati 806 -> ~900 MHz | ~%10 | **sahibinin onayı gerekir** (CLAUDE.md), ısı |
| D | Bellek baskısını azaltmak (jetsam): fb_queue, WebContent bellek sınırı, backboardd 480 MB dökümü | hız değil, kararlılık | düşük |
| E | Stratejik: backboardd'i GL'de bırakıp Metal'i yalnızca uygulamalar/oyunlar için kullanmak (asıl hedef "oyunlar için Metal") | UI 45-60 fps korunur | sahibinin kararı |

## 5. Önerilen sıra
1. (A) ölçüm: shader başına gerçek Bifrost döngüleri.
2. (D) kararlılık: jetsam'i azaltmadan kalıcı imaj güvenilmez.
3. (B) pass birleştirme araştırması.
4. (C) ve (E) sahibine bırakılan kararlar.
