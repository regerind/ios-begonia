# Açık hatalar

Durum: **inceleniyor** (veri toplanıyor), **düzeltme denendi** (build'de, sonuç bekleniyor), **beklemede** (sırası gelmedi
ya da onay gerekiyor). En üstteki en önemlisi.

## 1. En sol (Bugün/widget) ve en sağ (Uygulama Arşivi) panelde donma
- **Belirti:** Panele ilk gidişte ~1 s donuyor, sonra kayıyor. Hemen tekrar gidip gelince donma azalıyor. Animasyonun
  kendisi yavaş değil. Kullanıcı (y106 civarı): "aşırı yavaş geliyor", CPU'ya çok yük bindiriyor gibi; Metal
  katmanından şüpheleniyor.
- **Kanıt:** y99/y102: o sırada SpringBoard 87–203% CPU, ana iş parçacığı 80–94%; chronod (widget çizen süreç) 72–77%.
  GPU yalnızca ~35% meşgul.
- **Şüphe:** İlk kullanılan efektlerin (bulanıklık, materyaller) GPU programlarının CPU'da derlenmesi. Mesa'nın shader
  disk önbelleği kapalıydı (`-Dshader-cache=disabled`), yani her açılışta her şey yeniden derleniyordu.
- **Durum:** düzeltme denendi. y104: GL (backboardd) tarafında disk önbelleği açıldı (Mesa yaması 0006, userland 0046).
  y104 ve y105'te (önbellek açık) karelerin yalnızca %9'u 18 ms altındaydı; y106'da (kapalı) %56. Önbellek her karede
  bir şeye mal oluyor; neden olduğu bilinmiyor. y107'den itibaren varsayılan kapalı (`glcache=1` ile açılır). Yani
  panel donması için önbellek çözümü şimdilik işe yaramadı; profiler sonucu bekleniyor.
- Profiler (proc_cpu_test, örnekleyici) SpringBoard'ın ne yaptığını fonksiyon adlarıyla gösterecek; y103/y104'te
  çalışmadı (y104: eski test ikilisi pakete girdi).

## 1b. Ekran kilitlenmiyor
- **Belirti (kullanıcı, y106 civarı):** Ekranı kilitlemeye çalışınca kilitlenmiyor.
- **Şüphe:** Yeni olabilir. y95'teki tap-to-wake değişikliği (0044, karanlık ekranda dokunuşları yutma) ya da güç tuşu
  yolu. Kilitliyken ekranın kararması y93 civarında çalışıyordu.
- **Kanıt:** y101'de 24, y102'de 4 tuş olayı iOS'a ulaştı; y103–y106'da hiç yok (`buttons 0`). Ya o oturumlarda tuşa
  basılmadı, ya da y103'ten sonra bir şey bozuldu.
- **Durum:** inceleniyor (y107'de kullanıcı güç tuşuyla bir kez kilitleyip açacak).

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

## 10b. Sistem uygulamaları internete bağlanmıyor (App Store vb.)
- **Belirti:** Safari internete çıkıyor; App Store gibi Apple servisleri bağlanmıyor.
- **Şüphe:** Ağ değil, cihaz kimliği. Hackintosh'taki SMBIOS meselesinin karşılığı: Apple sunucuları cihazı seri
  numarası, ECID ve aktivasyonla tanıyor. Bizim ECID uydurma ("BEGONIA..."), model iPhone10,3; cihaz büyük ihtimalle
  aktive değil (mobileactivation, lockdownd). Framebuffer ile ilgisi yok.
- **Durum:** beklemede. Önce logda App Store'un hangi hatayla düştüğüne bakılacak (sertifika, aktivasyon, ağ).

## 10c. Modem (hücresel) çalışmıyor
- **Belirti:** SIM, arama ve mobil veri yok.
- **Not:** Bu portta modem desteği hiç yazılmadı. MediaTek modemi (CCCI, MD1) ve iOS'un CommCenter'ı arasında bir köprü
  gerekiyor. Büyük bir proje. Modemin güç ve saat ayarları için onay gerekir; IMEI bölümlerine kesinlikle dokunulmaz
  (modem IMEI'yi oradan okur, salt okunur kalmalı).
- **Durum:** beklemede.

## 11. Diğerleri
- **Bluetooth:** Denetleyici canlı (WMT açık, HCI reset OK); iOS tarafı yok. Kullanıcı tarafından hiç denenmedi;
  büyük ihtimalle çalışmıyor.
- **Sensörler:** Pusula AK09918 (I2C1 0x0c), ışık/yakınlık (0x47, 0x39) görüldü; sürücü ve iOS tarafı yok. İvmeölçer
  ve jiroskop muhtemelen SCP SPI üzerinde.
- **Kamera:** Hat ve saat değişiklikleri için onay gerekiyor.
- **Kalıcı imaj (p7):** Kullanıcı henüz istemedi ("not yet").
- **Hoparlör koruması:** TFA9874 DSP'siz çalışıyor, yani hoparlör koruması yok. Hoparlör -6 dB'de sınırlı.
