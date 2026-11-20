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
  Otomatik testlerde (y109, y114) paneller açılıştan 30+ sn sonra açıldığında duraklama görülmedi (en uzun kare
  35–86 ms). Ağır yük açılışın ilk 10–20 saniyesinde: SpringBoard 100–200%, çoğu dyld (kütüphane yükleme), launchd,
  wifid. Kullanıcının 1 dakikalık oturumlarda panele erken gitmesi bu fırtınaya denk geliyor olabilir; ya da gerçek
  parmak hareketinde başka bir şey var. Kalıcı imajda (açılıştan dakikalar sonra) denenmesi gerekiyor.
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
- **Çözüldü sayılıyor (y146–y148)**, ayrıntı [COZULEN-HATALAR.md](COZULEN-HATALAR.md)'de: jetsam belleği tükenen
  sistemde backboardd'yi öldürüyordu; bellek sıkıştırıcı açıldı. Kullanıcının elle doğrulaması bekleniyor.

## 3. YouTube kontrol katmanı (overlay) hızla yanıp sönüyor
- **Belirti:** Dokununca kontroller görünüp kayboluyor, çok hızlı; altyazı açıkken de öyle. Kullanıcı tam ekran
  düğmesine yerini bildiği için basabiliyor.
- **Şüphe:** WebKit'in Metal ile çizdiği katmanlarda yanlış tamponun gösterilmesi (WebKit.GPU Metal istemcisi).
- **Durum:** inceleniyor.

## 4. YouTube açıkken her şey 30 fps'e düşüyor: GPU doymuş
- **Kanıt (y117–y122):** YouTube sayfasında GPU %82–91 meşgul.
  - 5 sn'de 20–28 bin iş, bunların 11–14 bini çizim geçişi; kuyruk 350'ye kadar uzuyor, iş başına 33–51 ms bekleniyor.
  - Bu işlerin neredeyse tamamı backboardd'den (ekranı birleştiren CoreAnimation, GL yolu). Safari'nin GPU süreci 5 sn'de yalnızca 10–250 iş gönderiyor.
  - Kare başına 37–45 framebuffer değişimi ve 17–36 temizleme var. Her çizim geçişi GPU'da ~300 µs sürüyor (tam ekran boyutunda geçişler).
- **Elenenler:**
  - Değişken bulanıklık değil (y119, `vblur=0`: fark yok).
  - GPU servisinin yoklama gecikmesi değil (y122, 500 µs yoklama: fark yok).
  - Çağrı izi alınamadı (y120/y121).
- **Denenen:** backboardd'yi Metal'e almak (y123): backboardd çöktü, yenisi kare üretemedi.
- **Durum:** inceleniyor. Sıradaki: CRC'siz geçişler (y124); geçişleri neyin ürettiğini bulmak için kare izi
  (`gltrace=KARE-KARE`); CoreAnimation'ın Metal çizicisi (uzun iş).
- **Önceki kanıt (y102):** WebKit.Networking 100–134%, WebContent 80–128%, backboardd 85–93%, mediaserverd ~50%. y111'den
  beri AES/SHA donanımı programlara bildiriliyor; MP4 indirirken ağ süreci %20–33.
- **MediaRemoteUI:** Video başlarken %93–100 CPU (y117); bakılmadı.

## 5. Ekranı birleştiren katmanın (backboardd) CPU masrafı
- **İyileşti (y110–y111):** Video kareleri artık kopyalanmıyor (backboardd videoda %85–93'ten %20–50'ye). Değişiklik
  kontrolü seyrekleşti: kare başına GL 4,2 → 3,3 ms.
- **Kalan:** Çekirdekteki GPU çağrıları toplam ~%6–7 çekirdek (y115). Panele kareler CPU'da ölçeklenerek yazılıyor:
  iPhone X'in 1125×2436'sından panelin 1080×2400'üne, 3 iş parçacığında kare başına ~2,6 ms. GPU ile ölçeklemek
  mümkün ama büyük bir iş.
- **Durum:** kısmen çözüldü.

## 6. Duvar kâğıdı ayarı: yükleniyor simgesi, mor ekran, dokununca çökme
- **Şüphe:** iOS'un kayıtlı wallpaper resimleri (.atx, büyük ihtimalle sıkıştırılmış doku, ASTC olabilir) bu yolda
  çözülemiyor ve mor açılıyor. Ana ekran için 0030 yaması var; ayarlar ekranını kapsamıyor.
- **Durum:** beklemede.

## 7. Sarı arka plan
- **Belirti:** Açılışta wallpaper'dan önce sarı bir arka plan; Safari'nin arkasında da görünüyor.
- **Durum:** beklemede (ekran görüntüsüyle bakılacak).

## 8. Safari geçmiş servisi sürekli çöküyor
- **Kanıt (y101, y117):** `com.apple.Safari.History` SIGABRT, art arda 5 kez. Neden:
  `-[NSURL initFileURLWithPath:isDirectory:]: nil string parameter`. Servis bir dosya yolu istiyor ve boş yol alıyor.
- **Şüphe:** Uygulama grubu konteynerinin yolu bulunamıyor (containermanagerd / `/var/mobile/Containers/Shared/AppGroup`).
  App Store gibi Apple servislerinin bağlanamamasıyla (açık hata 10b) aynı kökten olabilir.
- **Durum:** beklemede.

## 9. Freeform
- **Belirti:** Takılıyor; uzun kesintisiz bir çizgi SpringBoard'ı çökertiyor.
- **Durum:** beklemede.

## 10. Video yazılımla çözülüyor
- Araştırma bitti: [VDEC-ARASTIRMA.md](VDEC-ARASTIRMA.md). Kod çözme mantığı Android'in kapalı `vpud` sürecinde
  (ana CPU'da, yardımcı işlemci ya da üretici yazılımı yok); H.264 yazmaç dizisi hiçbir açık kaynakta yok, MIUI vendor
  imajından tersine mühendislikle çıkarılmalı (haftalar). Güç (SCP_SYS_VDE), saatler (vdec_sel), larb2/IOMMU ve kesme
  bilgileri çıkarıldı. Çıkış biçimi MediaTek'in döşemeli MT21'i; iOS '420v' için dönüştürme gerekir.
- **Durum:** beklemede (yetki var; önce video gösterimindeki kopyalar kaldırılıyor: y110).

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
