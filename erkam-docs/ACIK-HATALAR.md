# Açık hatalar

Durum: **inceleniyor** (veri toplanıyor), **düzeltme denendi** (build'de, sonuç bekleniyor), **beklemede** (sırası gelmedi
ya da onay gerekiyor). En üstteki en önemlisi.

## 1. En sol (Bugün/widget) ve en sağ (Uygulama Arşivi) panelde donma
- **Belirti:** Panele ilk gidişte ~1 s donuyor, sonra kayıyor. Hemen tekrar gidip gelince donma azalıyor. Animasyonun
  kendisi yavaş değil.
- **Kanıt:** y99/y102: o sırada SpringBoard 87–203% CPU, ana iş parçacığı 80–94%; chronod (widget çizen süreç) 72–77%.
  GPU yalnızca ~35% meşgul.
- **Şüphe:** İlk kullanılan efektlerin (bulanıklık, materyaller) GPU programlarının CPU'da derlenmesi. Mesa'nın shader
  disk önbelleği kapalıydı (`-Dshader-cache=disabled`), yani her açılışta her şey yeniden derleniyordu.
- **Durum:** düzeltme denendi. y104: GL (backboardd) tarafında disk önbelleği açıldı (Mesa yaması 0006, userland 0046).
  y104'te kare süresi kötüleşti (GL içinde ~5 ms'den ~8 ms'ye); bunun önbellekten mi kullanımdan mı olduğu y105'te
  kontrol ediliyor. Metal/Vulkan (panvk) tarafında önbellek hâlâ kapalı.
- Profiler (proc_cpu_test, örnekleyici) SpringBoard'ın ne yaptığını fonksiyon adlarıyla gösterecek; y103/y104'te
  çalışmadı (y104: eski test ikilisi pakete girdi).

## 2. Videoda ileri/geri sarınca donma, SpringBoard çökmesi
- **Belirti:** YouTube (Safari) videosunda sarınca telefon kilitleniyor, SpringBoard çöküyor; bir kez çökmeden video
  kapandı ve devam etmedi.
- **Kanıt:** Henüz yakalanmadı; çökme 1 dakikalık oturumun dışında kaldı.
- **Durum:** inceleniyor.

## 3. YouTube kontrol katmanı (overlay) hızla yanıp sönüyor
- **Belirti:** Dokununca kontroller görünüp kayboluyor, çok hızlı; altyazı açıkken de öyle. Kullanıcı tam ekran
  düğmesine yerini bildiği için basabiliyor.
- **Şüphe:** WebKit'in Metal ile çizdiği katmanlarda yanlış tamponun gösterilmesi (WebKit.GPU Metal istemcisi).
- **Durum:** inceleniyor.

## 4. Video oynarken her şey ağırlaşıyor (CPU 4–5 çekirdek)
- **Kanıt (y102):** WebKit.Networking 100–134% (video verisi indirmek için çok anormal: Wi-Fi sürücüsü veya ağ
  yolu şüpheli), WebContent 80–128%, backboardd 85–93%, WebKit.GPU 50–77%, mediaserverd ~50% (yazılımla kodek çözme).
- **Durum:** inceleniyor (profiler bekleniyor).

## 5. Ekranı birleştiren katmanın (backboardd) CPU masrafı
- **Kanıt:** Kare başına ~113 çizim; Mesa'nın kendi glDrawElements'inde ~4 ms, karenin tamamı ~10 ms CPU.
  backboardd'nin `com.apple.coreanimation.display.primary` iş parçacığı videoda 40–75%.
- **Not:** Kopyalama değil. Video kareleri ve büyük yüzey kopyaları oturum başına ~60 MB, saniyede 1 MB'den az.
- **Durum:** beklemede (profiler sonrası).

## 6. Duvar kâğıdı ayarı: yükleniyor simgesi, mor ekran, dokununca çökme
- **Şüphe:** iOS'un kayıtlı wallpaper resimleri (.atx, büyük ihtimalle sıkıştırılmış doku, ASTC olabilir) bu yolda
  çözülemiyor ve mor açılıyor. Ana ekran için 0030 yaması var; ayarlar ekranını kapsamıyor.
- **Durum:** beklemede.

## 7. Sarı arka plan
- **Belirti:** Açılışta wallpaper'dan önce sarı bir arka plan; Safari'nin arkasında da görünüyor.
- **Durum:** beklemede (ekran görüntüsüyle bakılacak).

## 8. Safari geçmiş servisi sürekli çöküyor
- **Kanıt (y101):** `com.apple.Safari.History` SIGABRT, art arda 5 kez.
- **Durum:** beklemede.

## 9. Freeform
- **Belirti:** Takılıyor; uzun kesintisiz bir çizgi SpringBoard'ı çökertiyor.
- **Durum:** beklemede.

## 10. Video yazılımla çözülüyor
- Donanım çözücü (MediaTek VDEC) için güç alanı + saat (onay gerekir), M4U, VPU/VCU üretici yazılımı ve iOS
  VideoToolbox bağlantısı gerekiyor. Büyük, ayrı bir proje.
- **Durum:** beklemede; kullanıcı yapılmasını istiyor, araştırma adımı önerildi.

## 11. Diğerleri
- **Bluetooth:** Denetleyici canlı (WMT açık, HCI reset OK); iOS tarafı yok.
- **Sensörler:** Pusula AK09918 (I2C1 0x0c), ışık/yakınlık (0x47, 0x39) görüldü; sürücü ve iOS tarafı yok. İvmeölçer
  ve jiroskop muhtemelen SCP SPI üzerinde.
- **Kamera:** Hat ve saat değişiklikleri için onay gerekiyor.
- **Kalıcı imaj (p7):** Kullanıcı henüz istemedi ("not yet").
- **Hoparlör koruması:** TFA9874 DSP'siz çalışıyor, yani hoparlör koruması yok. Hoparlör -6 dB'de sınırlı.
