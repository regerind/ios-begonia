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

## Değişmeyen kurallar
- IMEI bölümlerine (nvram, nvdata, protect1/2, persist, proinfo...) asla yazılmaz. Flaşlama yalnızca boot ve misc.
- Voltaj, saat ve PMIC hattı değişikliği yalnızca kullanıcı onayıyla.
- Test oturumu 60 sn (`WATCH=60`).
- Wallpaper gizlenmez, çözünürlük düşürülmez, Hareketi Azalt/Şeffaflığı Azalt kullanılmaz.
