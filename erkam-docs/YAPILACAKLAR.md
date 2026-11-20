# Yapılacaklar

Öncelik sırasıyla. Biten iş buradan silinir, [COZULEN-HATALAR.md](COZULEN-HATALAR.md)'ye yazılır.

1. **Panel donması (açık hata 1):**
   - y105 sonucu: shader önbelleği ikinci açılışta işe yarıyor mu? Kare maliyeti artışı önbellekten mi?
   - Çalışırsa aynı önbelleği Metal/Vulkan (panvk) tarafında da aç.
   - Profiler ile SpringBoard'ın ana iş parçacığının ne yaptığını fonksiyon adlarıyla gör.
2. **Video:**
   - Sarınca donma/çökme: çökmeyi oturum içinde yakala.
   - Overlay yanıp sönmesi: WebKit'in Metal katmanları.
   - WebKit.Networking'in 1 çekirdek yemesi: Wi-Fi alma yolu.
3. **backboardd CPU masrafı:** kare başına ~113 çizim, ~10 ms. Mesa'nın çizim başına masrafını azalt.
4. **Duvar kâğıdı ayarları:** mor ekran, yükleniyor, çökme. Sarı arka plan.
5. **Safari.History çökmesi.**
6. **Freeform:** takılma ve uzun çizgide çökme.
7. **VDEC (donanım video çözme):** önce yalnızca araştırma (Xiaomi kaynağından güç/saat sırası, üretici yazılımı, komut
   arayüzü). Donanıma dokunmadan önce güç alanı ve saatler için kullanıcı onayı.
8. **Bluetooth:** iOS tarafı.
9. **Sensörler:** pusula, ışık/yakınlık; ivmeölçer/jiroskop (SCP).
10. **Kamera:** hat ve saat onayı gerekiyor.
11. **Kalıcı imaj p7+:** kullanıcı isteyince.


### Metal tek hat: sıradaki işler (2026-11-20 akşamı, ANALIZ-2026-11-20-METAL-HIZ.md bölüm 9-12)
1. Cihazda p13'ü doğrula (0019: uniform yüklemeleri 16 hizalı): blur'lu ekranlar doğru mu, fps. Yanlışsa p13n'e dön. Tek seferlik log koşusu için sahibinin onayı gerek (yeni plan y imajlarını yasaklıyor).
2. Çeviricide (metal2vulkan) buffer ofset maskesini AIR'in bildirdiği hizadan üret; PanVK hizayı maskeden okusun (0019'u varsayımdan garantiye çevirir).
3. `fixed_frag` ailesini `tests/bifrost_stats.c` ile ölçebilmek: input attachment yolu ve gerçek sampler specialization değerleri.
4. Ekran yüzeyi LINEAR dma-buf'a yerinde çiziliyor: kendi U-interleaved dokuya çizip karede bir kez kopyalamayı ölç.
5. Bellek/jetsam: y290'daki GPU VA sayacı hâlâ koşulmadı; LRU sınırı tasarımı ondan sonra.
## Değişmeyen kurallar
- IMEI bölümlerine (nvram, nvdata, protect1/2, persist, proinfo...) asla yazılmaz. Flaşlama yalnızca boot ve misc.
- 2026-11-20 ~00:55, kullanıcı: GPU'yu ve video çözücüyü (VDEC) çözmek için IMEI dışında tam yetki ("sınır yok, çok
  invazif gitmemeye çalış"). GPU ve VDEC için güç alanı, saat ve PMIC işleri artık ayrı onay istemiyor. Diğer işlerde
  voltaj, saat ve PMIC değişikliği yine onayla.
- Test oturumu 60 sn (`WATCH=60`).
- Wallpaper gizlenmez, çözünürlük düşürülmez, Hareketi Azalt/Şeffaflığı Azalt kullanılmaz.
- 2026-11-21: kaydırma için sıradaki adaylar (her biri bir y koşusu ister): (1) `M2V_HOT=1` çalışma zamanı tek başına
  (boot'a sığıyor mu / açılıyor mu; olmazsa kullanıcının onayladığı gibi sistem bölümüne), (2) kext yüzey tutma tek
  başına (`cdba4af`), (3) kare başına ikinci tam ekran geçişinin (64×64 geçişinden sonra yeniden yükleme) birleştirilmesi.
  y311'de (1) ve (2) birlikte denendi ve imaj açılmadı: ayrı ayrı denenmeli.
