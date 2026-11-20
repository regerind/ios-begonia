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
- **y164/y170 (2026-11-20 gece):** Açılıştan hemen sonra (kilit açılır açılmaz, 29. sn) Uygulama Arşivi'ne gidip gelmek:
  bir 5 sn penceresinde 14 kare 35 ms üstü (en uzun 84 ms), sonra düzeliyor. Donma yok. Daha geç (34+ sn) gidince
  takılma yok (en uzun 47 ms). İlk 20 sn'de tüm çekirdekler dolu (toplam %450–640): SpringBoard %93–192, coreduetd %93,
  chronod %80, CalendarWidgetExtension %89, healthd, dasd. SpringBoard'ın örnekleri (sembollendi): dyld'nin
  JustInTimeLoader bağlamaları (`trieWalk`, `forEachBindTarget`) ve ObjC başlatma. Bu açılışta bir kerelik bir iş.
  Şüphe: SpringBoard'a eklenen kütüphanemiz (DYLD_INSERT_LIBRARIES) dyld'nin hazır başlatma kapanışını
  kullanmasını engelliyor ve her sembol tek tek bağlanıyor. Bugün paneli otomasyonla açılamıyor (enjekte sağa kaydırma
  sayfa değiştirmiyor), o yüzden en sol panel test edilemedi.

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
- **Durum:** büyük ölçüde düzeldi (y136–y162), bkz. COZULEN-HATALAR. Doğru kurulumla y161: 47–51 kare/sn, karelerin
  %27'si 18 ms altında. Henüz tam 60 değil. y193'ten beri widget köşe düzeltmesi (0057–0059) varsayılan: YouTube
  41–44 kare/sn (y194). `gllivecopy=0` ile 47–51'e döner ama Bugün panelinde köşe işaretleri geri gelir.
- **Kalan en büyük GPU masrafı (y166–y169):** kare başına ~9 tam 1622×1525 geçiş. CoreAnimation havuzdaki büyük bir
  dokuya (program 5, kopya) makassız tek küçük dörtgen çiziyor; sonuç çoğunlukla saydam. Panfrost geçişin alanını görüş
  alanından aldığı için her biri 2,47 Mpx. Denenenler: temizliği görüş alanına sınırlamak (0056, görüş alanı zaten dokunun
  %100'ü: işe yaramadı); otomatik makas (0050): bu geçişler 1800 → 1000 ama kare sürelerinde fark yok. Sıradaki fikir:
  dokunun daha önce temizlenmiş, boş kalan kısmını takip edip temizliği ve çizimi yalnızca dolu bölgeye sınırlamak.
- **Önceki kanıt (y102):** WebKit.Networking 100–134%, WebContent 80–128%, backboardd 85–93%, mediaserverd ~50%. y111'den
  beri AES/SHA donanımı programlara bildiriliyor; MP4 indirirken ağ süreci %20–33.
- **MediaRemoteUI:** Video başlarken %93–100 CPU (y117); bakılmadı.
- **WebKit.Networking (y171–y172, sarma testi):** video yüklenirken ve sarılırken %102–119; bunun ~%40'ı çekirdekte
  (k41–45). En sık örnekler: tek bir WebKit fonksiyonu (`WebKit!3311b0`, 111 örnek; paylaşılan önbellekte yerel sembol
  yok, adı bulunamadı), iş parçacığı havuzu (`__workq_kernreturn`, 70), `__sendmsg` (27), dosya yazma
  (`guarded_pwrite_np`, ağ önbelleği/sqlite), memmove. Şüpheler: (a) çekirdek payı: Wi-Fi alma yolumuz paket başına
  pahalı (halka yoklamalı, her paket kopyalanıyor) ya da TCP/mbuf yolu; (b) kullanıcı payı: WebKit'in disk önbelleğine
  medya yazması. Sıradaki: veri hızını ölçmek (kaç MB/s'ye bu kadar CPU), Wi-Fi alma yolunun maliyeti.

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

## 10d. Denetim Merkezi'nde ses kaydırıcısı bazen görünmüyor
- **Belirti:** Ses kaydırıcısının kutusu tamamen yok (yerinde yalnızca bulanık arka plan).
- **Kanıt:** y155 ve y156 çekimlerinde yok; y140, y157 ve y159 çekimlerinde var (beyaz dolu kutu).
- **Şüphe:** Önce köprü 0054'ten (geç temizlik) sanıldı: y157'de 0054 kapalıyken kaydırıcı vardı. Ama y159'da 0054
  açıkken de var. 0055'in sayaçları da (hedef çizilmeden önce başka yolla kullanıldı mı) hiç artmadı. Büyük olasılıkla
  ses durumunun geç gelmesine bağlı bir zamanlama hatası (ses sunucusunun ne zaman hazır olduğu); 0054 tamamen aklanmış değil.
- **Ek (y163):** y155–y159 yanlış kurulumla (Metal istemcileri yok, SpringBoard GL'de) koşmuştu. Doğru kurulumla
  (y163, 0054 açık) kaydırıcı var. Kayıp büyük olasılıkla o kuruluma özgü.
- **Durum:** gözleniyor (doğru kurulumda görülmedi). Kullanıcı elle görürse yeniden açılır.

## 10e. Bugün panelinde widget köşelerinde renkli işaretler — DÜZELDİ (y193/y199), bkz. COZULEN-HATALAR
Aşağısı araştırmanın kaydı.
- **Belirti (y177, otomasyonla ilk kez açılan Bugün paneli):** Her widget'ın köşelerinde küçük renkli L işaretleri:
  sol üst yeşil, sağ üst mavi, sağ alt kırmızı, sol alt beyaz. Yuvarlatılmış köşenin dışında kalan, saydam olması
  gereken pikseller renkli çiziliyor gibi.
- **Elenenler:** köprü 0054 (y178, kapalıyken de var), maske yüzeyi paylaşımı (y179, `glmask=0`), opaklık dokularının
  kırmızı okunması (y180, `glalpha=0`), SpringBoard'ın Metal yolu (y181, SpringBoard Metal istemcisi değilken de var).
- **Şüphe:** İşaretler widget görüntülerinin kendisinde olabilir (chronod ve widget uzantılarının çizdiği yüzeyler), ya
  da köşeleri dört kanalda tutan bir maske dokusunun yanlış biçimde (renk olarak) okunması.
- **İz (y183):** Widget (sol üst, 102,174–525,597) önce program 3 ile karesinin tamamı düz renkle dolduruluyor
  (karıştırma kapalı, doku 0 = ekranın kendisi: köprünün çerçeve tamponu okuma öykünmesi), sonra program 63 köşeleri
  9 parçayla çiziyor (RG8 512×512 ve 190×190 RGBA dokular, karıştırma yine kapalı). `glfetch=0` ile (y184) widget'lar
  hiç çizilmiyor: CoreAnimation bunlar için çerçeve tamponu okumaya muhtaç. LA08 maskelerinin kanal yönlendirmesi
  kodda doğru görünüyor.
- **Şüphe (en güçlü):** köprünün çerçeve tamponu okuma öykünmesi köşe gölgelendiricisinde (program 63) yanlış piksel
  ya da yanlış kanal veriyor; köşenin dışındaki pikseller "arkadaki renk" yerine maske kanallarının rengini alıyor.
- **Durum:** açık. Sıradaki: program 63'ün gölgelendirici metnini almak (y185/y186'da zamanlama tutmadı: panel
  açılmadan iz alındı); `gltrace_place` + daha geç `gltrace_at` ya da `glshaders=1`.

## 10f. p9'da çok sayıda görsel hata (kullanıcı, 2026-11-20 sabah)
- **Belirti:** Denetim Merkezi siyah geliyor; Bugün görünümünde siyah kutular kalıyor; üst panel (Bildirim Merkezi)
  çekilince ekran simsiyah oluyor; saydamlık tamamen bozuk. YouTube oynatma belirgin yavaş. Sistem akıcılığı iyi.
- **Şüphe (en güçlü):** köprü 0057–0059 (ekranın 1:1 kopyaları bu kareden). Bu yol ekran resmini kare ortasında
  okutuyor; Mesa 0009 o sırada ekrana yeni bellek verirse kare kayboluyor (y187/y188'deki siyah ekranın aynısı). glFlush
  bunu her durumda önlemiyor gibi. Denetim Merkezi ve Bildirim Merkezi 0057'den sonra hiç test edilmedi (son doğru
  test y163, 0057'den önce). y193/y199'daki "temiz" Bugün çekimlerinde ilk widget koyu bir kutuydu: yanlış okundu.
  YouTube yavaşlığı canlı kopyaların bedeli (y194: 41–44 kare/sn).
- **Ders:** bir görüntü düzeltmesinden sonra Bugün, Denetim Merkezi, Bildirim Merkezi ve YouTube çekimleriyle tam tur
  yapılmadan kalıcı imaja konmamalı.
- **Durum:** p10 hazır (p9 + `gllivecopy=0`, SHA256 96022caf…de47); kullanıcının onayını bekliyor.

## 11. Diğerleri
- **Bluetooth:** Denetleyici canlı (WMT açık, HCI reset OK); iOS tarafı yok. Kullanıcı tarafından hiç denenmedi;
  büyük ihtimalle çalışmıyor.
- **Sensörler:** Pusula AK09918 (I2C1 0x0c), ışık/yakınlık (0x47, 0x39) görüldü; sürücü ve iOS tarafı yok. İvmeölçer
  ve jiroskop muhtemelen SCP SPI üzerinde.
- **Kamera:** Hat ve saat değişiklikleri için onay gerekiyor.
- **Kalıcı imaj (p7):** Kullanıcı henüz istemedi ("not yet").
- **Hoparlör koruması:** TFA9874 DSP'siz çalışıyor, yani hoparlör koruması yok. Hoparlör -6 dB'de sınırlı.

## 12. backboardd Metal'de (tek GPU yolu): görüntü doğru, hız yetersiz
- **Durum (y227, 2026-11-20):** Deneysel; varsayılan hâlâ GL yolu. p12 derlenmedi (hız ölçütü tutmuyor).
- **Çalışan:** ana ekran, Denetim Merkezi, Bugün paneli, Ayarlar Metal üzerinden doğru çiziliyor (çekimler y205, y211,
  y212, y215, y218); backboardd 60 sn'lik oturumlarda hiç çökmedi; çökme döngüsü yok. Kurulum ve çözümler:
  NASIL-COZDUK "backboardd'yi Metal'e taşıma".
- **Belirti:** 5–10 kare/sn (GL yolu: animasyonlarda 45–60, YouTube 41–51). YouTube'da 5–6 kare/sn (y216).
- **Kanıt:** GPU %65–71 meşgul, backboardd CPU'su tek çekirdeğin ~%37'si (y214). Karede 2 tam ekran fragment işi,
  her biri ~30 ms (1080x2340) / ~16 ms (1041x1180) — içerikten bağımsız, piksel sayısıyla orantılı (Ayarlar'ın
  koyu ekranında da aynı). GL yolunda en uzun fragment işi ~3 ms. Vertex/tiler zincirleri karede ~23 ms: PanVK JM
  her çizimde tanımlayıcıları GPU'da kopyalayan 2 ek compute işi çıkarıyor (çizim başına 4 iş; GL'de 2).
- **Neden (y218–y225 ile daraltıldı):** fragment tarafı CoreAnimation'ın Metal gölgelendiricilerinin kendisi.
  - Çizimsiz tam ekran geçiş 0,9–1,8 ms (y222) → geçiş/ön-kare/saklama maliyeti değil; AFBC de değil (y218);
    çeviricinin kapsama denetimleri de değil (y220).
  - backboardd'nin fragment gölgelendiricileri ortalama ~3 KB Bifrost kodu (y219). Makinede gerçek sabitlerle çeviri
    (`C:\begonia-private\sh\qc_from_key.py`): sıradan bir görüntü çizimi 278–337 gövde komutu; Metal'e eş cihaz
    profilinde bile 292. İçinde: hedefi okuyup gölgelendiricide karıştırma (framebuffer fetch, 2 renk eki), 32x32 gürültü
    dokusuyla titreme, yarım duyarlık dönüşümleri, doku koordinatını 1/256'ya yuvarlama. GL yolunun ES2 düzeyi
    gölgelendiricileri bunların hiçbirini yapmıyor; fark buradan (piksel başına ~12 ns, 806 MHz'lik G76'da; GPU zaten 806 MHz, y225 logu).
  - Çeviricinin "Metal'le bit-kesin" öykünmesi bunun yalnızca ~%17'si; çalışma zamanı yaması 0004 ile kaldırıldı
    (y225: en uzun iş 31 → 24–28 ms).
- **Sonraki adımlar (etki sırasıyla, hiçbiri yapılmadı):**
  1. CoreAnimation'a hedefi okumayan (sabit işlevli karıştırma) boru hatlarını seçtirmek: `fc_framebuffer_fetch=0`
     yolu kitaplıkta var; CA'nın bunu aygıtın hangi yanıtına göre seçtiği bilinmiyor (QuartzCore'da tersine mühendislik
     gerek). En büyük kazanç burada olabilir: gölgelendirici küçülür, Mali'nin erken piksel eleme yolu açılır.
  2. PanVK JM'de çizim başı tanımlayıcı kopyalamayı GPU işi yerine CPU'da yapmak (karede ~23 ms'lik vertex/tiler
     süresinin çoğu; `bifrost/panvk_vX_meta_desc_copy.c`).
  3. Çeviricide kalan öykünmeler (koordinat yuvarlama, yarım duyarlık) için hızlı yol.
  4. GPU saati: ZATEN 806 MHz (y225: MFGPLL 806000 kHz); 270 MHz notu eskiydi, yapılacak bir şey yok.
  Tahminim: 2–3 birlikte en fazla ~2 kat (15–20 kare/sn); GL yolunun hızına ancak 1 ile yaklaşılır. Bu bir tahmin,
  ölçüm değil.
- **Yan bulgular:** çekimlerde zaman zaman renkli pikselli ince yatay çizgi (y212, y218) — çekim aktarımı mı, yerinde
  yüzeylerde önbellek tutarlılığı mı belirsiz; kullanıcının gözle bakması gerek. `metal=3` mediaserverd'ı Metal'e alıp
  çökertiyor (MTLResourceOptions 0x80020 ve eksik `newFunctionWithName:constantValues:pipelineLibrary:error:`):
  `metal=1` + liste kullan. Boru hatları her açılışta yeniden derleniyor (önbellek isabeti 0): ilk açılan her yeni
  görünümde ~100 ms'lik takılmalar.

  Güncelleme (y228-y229): adım 2 (descriptor kopyasını CPU'ya almak, PanVK 0014) denendi. Kopya çalışıyor (y229:
  66 binden fazla, geri dönüş 0) ama hız değişmedi (6-7 kare / 5 sn). Descriptor kopya işleri darboğaz değil.
  Kalan tek yol: CoreAnimation'ın framebuffer-fetch'siz boru hatlarını seçmesi (QuartzCore RE, adım 1).

  Adım 1 (y230-y237): CoreAnimation'da framebuffer fetch'i kapatma denemesi.
  - backboardd'in cihaza sorduğu yetenekler (`metal_caps_log=1`): supportsFamily: 1001-1005 ve 1008, supportsMemorylessRenderTargets,
    supportsRasterOrderGroups (0), supportsTileShaders (0), supportsNativeHardwareFP16 (0), supportsSIMDGroup (0), YCBCR (0).
    `isFramebufferReadSupported` hiç sorulmuyor: fetch'i seçen ayrı bir yetenek bayrağı yok.
  - Yanıtları kapatmak (`metal_caps_off`): memoryless kapalı -> ikinci ek (fc_attachment_count 2->1) kalkıyor ama fc_framebuffer_fetch 1 kalıyor;
    Apple2/3 ailesi kapalı -> fetch yine 1. CA ortam anahtarları (`ca_env=`: CA_DISABLE_INPLACE_SHADERS, CA_FORCE_GENERIC_SHADERS)
    fetch'i değiştirmedi (ortamın CA'ya ulaştığı doğrulanamadı); CA_DISABLE_TILE_SHADERS backboardd'i yeniden başlattı.
  - Çevrimdışı ölçüm (`qc_from_key.py`, QC_OVERRIDE ile): fc_framebuffer_fetch=0 tek başına gövde komut sayısını yalnız 193 -> 187 yapıyor.
    Asıl düşüş fc_blend_function=0 ile (193 -> 106; 64 -> 22). Bu gövde sayıları PanVK profiliyle (runtime 0004); "280" Metal-uyumlu profildeydi.
  - Cihazda ölçüm (y237, görüntü bozuk): fc_blend_function=0 + fc_framebuffer_fetch=0 zorlanınca 6 -> 8 kare/5 sn, ortalama GPU işi
    650 -> 427 us, ama en uzun iş hâlâ 23 ms. Shader-içi blend/fetch maliyetin ancak %25-30'u.
  - QuartzCore içinde pipeline seçim bayrağını yamamak denenmedi: kazanç tavanı bu ölçümle sınırlı (8 kare). fc_framebuffer_fetch'in tablosu
    dyld cache'te .27.dylddata 0x24a9c8'de (isim işaretçisi); kodu bulunmadı.
  - Kalan şüpheler: yarım duyarlık (supportsNativeHardwareFP16=0 olduğundan OpFConvert'ler), ekran dolduran backdrop/blur geçişleri
    (en uzun iş 23 ms), iş başına bellek bant genişliği.

  Geçiş başına süre (y238, PanVK 0015, `panvk_debug=passtime`): toplam vurucu "1080x2340, 2 renk eki BGRA8, rt0 yüklü / rt1 temiz ve
  saklanmayan, çok çizimli" geçişler. Toplam fragment süresine göre sıralama: j122 (2221 ms, 238 geçiş, ort. 9.3 ms), j101 (2165 ms, 156 geçiş,
  13.9 ms), j49 (1585 ms, 5.4 ms), j45 (1545 ms, 5.8 ms), 1622x1525 ara geçiş j10 (1436 ms, 4.9 ms), j126 (1350 ms, 15.3 ms). En uzun tek iş:
  j238/j263, 26-27 ms. İş sayısı çizim sayısıyla orantılı değil: j15 bile 5 ms (tam ekran bir katman ~1 ms), j101 14 ms. Yani maliyet
  "tam ekranı kaplayan çizim sayısı x ağır CA fragment shader'ı" ve her kare 4+ tam ekran geçiş (her biri tüm kareyi yükleyip saklıyor).
  Kısa 64x64 geçişlerde bile vertex/tiler beklemesi 5-9 ms: uyku beklemesinin uyanma gecikmesi; boru hattı akarken değil, seri beklemede görünüyor.
  Tek seferde müdahale adayları: (1) tam ekran geçişlerin sayısını azaltmak (CA'nın backdrop/grup geçişleri), (2) tam ekran çizimlerin
  fragment maliyetini düşürmek (yarım duyarlık OpFConvert'leri, blend yolu), (3) rt1'in (temiz, saklanmayan) bırakılması.

  y239-y246 (geçiş süreleri sonrası denemeler):
  - Tile boyutu: tüm geçişlerde 16x16 (256 px), v7 için donanım üst sınırı; büyütülemez (y239/y240).
  - Yarım duyarlık (PanVK 0016: shaderFloat16 v7'de açık, supportsNativeHardwareFP16=1): fragment süreleri ~aynı (yalnız j122 9.4 -> 5.2 ms).
    Görüntü doğruluğu ekran görüntüsüyle doğrulanmadı; ürün yoluna almadan önce doğrulanmalı.
  - Her sunulan kare için ~52 render pass (~120 GPU işi gönderimi) ve ~80 ms GPU işi var; GPU %50-55 meşgul, işler kuyrukta ~30 ms bekliyor.
    Yani GPU'nun yarısı boş: iş başına ~0.5 ms'lik gönderim/uyanma boşluğu şüphesi (kernel begonia_mali_service.c bekleme/uyanma).
    `begonia_gpu_spin=2000` (uyku yerine dönme) iş başına GPU süresini 580 -> 380-500 us'ye indirdi, ama koşular aynı iş yükünde olmadığı için
    (dokunuş sayısı değişti, bir koşuda backboardd yeniden başladı) kare hızına etkisi ölçülemedi. Sıradaki: aynı betikle temiz A/B.
  - Küçük geçişler (64x64, 406x34 vb.) çok sayıda: kare başına ~50; her biri en az bir gönderim. Geçiş sayısını azaltmak (CA'nın küçük
    bitmap/glyph geçişlerini birleştirmek) asıl kazanç olabilir.

  Kare kırılımı (y243 geçiş günlüğü + y246 çekirdek istatistiği; ~130 kare, 6793 fragment geçişi, ~50 geçiş/kare):
  - Geçişlerin %31'i >=3 ms (2120): 1760 tanesi 1080x2340 (2xBGRA8, rt0 yüklü, rt1 temiz+saklanmayan, tile 16x16), 300 tanesi 1622x1525 ara geçiş (4.8 ms).
    Kare başına ~13 tam ekran geçiş. >=5 ms olanlar kare başına 4.6 (toplam ~48 ms): j126 14.7 ms, j101 13.9, j131 14.0, j238 19-27; 3-5 ms olanlar
    kare başına 9.1 (~44 ms): j45/j49/j15 ~5 ms, j111 ~3 ms. Tam ekran geçişin sabit maliyeti ~1.5-2 ms (boş geçiş 1-2 ms), üstüne çizimler.
  - <3 ms geçişler: 4673 (kare başına ~35); GPU süreleri önemsiz, maliyetleri gönderim/uyanma boşluğunda.
  - Çekirdek: kare başına ~120-128 iş gönderimi, iş başına ~580 us, GPU %50-55 meşgul (kare 143-150 ms, GPU işi ~70-76 ms), kuyrukta 31-44 ms bekleme
    (y246). Seri koşuda (y243) kuyruk 0.1 ms ama GPU yine %43-46: boşluk kuyruk eksikliği değil, iş başına ~0.5 ms ölü zaman.
  - PanVK BeginRendering (fragment batch) 9147 / ön uç render encoder R5651 (y231) ~ 1.6x: bir Metal geçişi birden fazla PanVK geçişine bölünüyor
    olabilir (doğrulanmadı).
  - Ölçüm sınırı: passtime f değeri bekleme gecikmesini içerir (toplam f 12.5 sn > çekirdeğin GPU süresi ~9 sn).
  - y247: begonia_gpu_spin=2000 kalıcı kazanç vermedi (7-8 kare/5 sn).
  - **y249-y254 (2026-11-20 akşam): 1.6x kuyruğu çözüldü.** Bölünme encoder'da DEĞİL (y249: inst==enc, multi/bar/tile/disp = 0).
    Fazla geçişler `clear_reach.rs`'in "ulaşım temizliği"nden (y250: 7680 encoder'ın 1936'sında; y251 nedeni: clear alanı, render
    alanının 32 px karoya yuvarlanmış hâlini kapsıyor, bunun için tam ekran attachment load+store ile ayrı geziliyor, iki yanında
    Barrier::everything()). Yama 0008 (karo payı için bu pass'i atla): BeginRendering/encoder 1.5-1.8 → ~1.07, 5 sn'de GPU işi 1170 → ~600,
    kuyruk 116 → ≤15 derin, kare 38-55 → 52-63 (aynı seek betiği, y249 vs y254). Hâlâ 45-60 kare (GL) değil.
  - Şerit sürümü (y252) pass sayısını ARTIRDI (sağ+alt şerit ayrı pass); bırakıldı.
  - Hâlâ açık: encoder başına ~13 tam ekran geçiş (CA'nın kendisi), geçişler arası Barrier::everything() ölü zamanı, DontCare clear'ı
    (tiles) yolu. Resim doğrulaması: Control Center doğru (y253); Today/YouTube y255/y256.
  - **rt1 neymiş (y254 telemetrisi, 2026-11-20 akşam):** CA'nın her tam ekran geçişinde `c1` var: aynı boyut, `L0 S0 sm3` = DontCare yükü,
    hiç saklanmayan, MEMORYLESS. Pipeline anahtarlarında (tele_keys, 8 anahtar) yalnızca `c0=` var, `c1=` yok; `fc_color_inputs1:0`,
    `fc_attachment_count:2` yalnız geçişin ek sayısını söylüyor. Yani c1'i hiçbir pipeline yazmıyor/okumuyor; runtime onu her geçişte
    sıfıra temizliyor (clear_reach.rs, memoryless DontCare -> Clear). Tile tamponu yükü küçük (2x32 bit/piksel, 16x16 zaten üst sınır):
    kaldırmanın kazancı düşük beklenir, tek başına denemeye değmez (DENENMEDİ, varsayım).
  - Asıl maliyet: tam ekran blur/backdrop fragment geçişleri (12-19 ms, y258). GL yolu bunları ucuza yapıyor çünkü GL köprüsünde
    "backdrop önceki kareden" (0051) ve geç temizleme (0054) gibi kısayollar var; Metal yolunda CA bunları gerçekten çiziyor.
    Sonraki gerçek kaldıraç = Metal tarafında aynı kısayolun karşılığı (görüntüyü etkiler, sahibinin kararı) ya da blur geçişinin
    daha düşük çözünürlükte çizilmesi (CA'nın kendi kararı, yamalamak zor).
  - **Slot 1 ve ölü zaman (y260-y261, kernel tanılaması, salt-okunur):** 5 sn'de ~3750 GPU işi, 1516'sı fragment; slot 1'in ~2230 zinciri
    ~26000 iş taşıyor: compute ~10550, vertex ~5280, tiler ~5240, write ~4910 (zincir başına ~4.7 compute + 2.4 vertex + 2.3 tiler + 2.2
    write; draw başına ~2 compute). Zincirler KARIŞIK (yalnız vertex/tiler zinciri ~0), bu yüzden fragment ile slot 1'i üst üste
    bindirmek (servis şu an her işi tek tek bitirip sonrakini veriyor) kör bir değişiklikle GÜVENLİ DEĞİL: compute işi önceki fragment
    çıktısını okuyabilir. Slot 1 süresi ~810-850 ms/5 sn (%17), fragment ~1.7 sn (%35), GPU %50 meşgul. İş dışı zaman iş başına ~640 us.
    y262 bu boşluğun iş öncesi hazırlıkta mı, işler arası döngüde mi olduğunu ölçüyor.
  - **Güncelleme (y264-y267):** CPU/GPU sıralılığı giderildi (`fb_gpuseq=1 metal_swapwait=0`, bkz. NASIL-COZDUK). Seek betiği 6-8 -> 11-12 fps, YouTube
    ~11-13 fps. GPU artık %75-85 meşgul: kalan darboğaz GPU işi (tam ekran blur/backdrop pass'leri 12-19 ms). GL yolunun 45-60 fps'ine hâlâ uzak.
  - **Güncelleme (y275-y280):** PanVK 0018 ile seek betiği 5 sn'de 78-110 kare (15-20 fps), YouTube 50-71 kare (10-14 fps, GPU %83-87). Draw başına
    iş 5 -> 3. Kalan: GPU'nun kendi işi (tam ekran blur/backdrop pass'leri 12-19 ms) ve draw başına kalan 1 compute (indirect helper).
  - **AÇIK: backboardd aralıklı SIGKILL (status 0x9)**: y245, y272, y274 (son iki yeni ayarlarla: fb_gpuseq=1 metal_swapwait=0, 0009/0018). Neden bilinmiyor;
    birkaç koşuda tekrarlanmadı. İzlenecek: kill'i kim yolluyor (jetsam? launchd/CA watchdog?).

## 2b. (GERİ GELDİ) backboardd'in jetsam ile öldürülmesi: pencere sunucusu yeniden başlıyor
- **Belirti:** Bazı koşularda backboardd[13] ve SpringBoard yeniden başlıyor (pid değişir, "processes:" satırında iki pid).
  y245 (`status 0x9 reason 1/0x5` = JETSAM_REASON_MEMORY_PROCTHRASHING), y272 (`reason 1/0x4` = MEMORY_VMPAGESHORTAGE), y274 (aynı 0x9). Yaklaşık
  35 koşuda 3 kez (2026-11-20 akşamı).
- **Kanıt:** konsol çıkış notu `exit: backboardd[13] status 0x9 reason 1/0x4`; namespace 1 = JETSAM. Rastgele SIGKILL değil, bellek baskısı.
- **Eski çözüm neden yetmedi:** y146-y148'de `kern.vm_compressor = 2` eklenmişti (COZULEN-HATALAR "Videoda ileri/geri sarınca..."). O satırda "çok
  uzun oturumlarda bellek yine dolabilir" şüphesi yazılıydı. Şimdi 60 sn'lik koşularda bile oluyor; Metal yolu (backboardd Metal, PanVK/CPU
  tabloları, 10 MB'lık yüzeyler, aynı anda daha fazla uçuştaki kare: `fb_gpuseq=1 metal_swapwait=0`) belleği artırmış olabilir. Doğrulanmadı.
- **Şüphe/sonraki adım:** backboardd ve diğer Metal istemcilerinin bellek kullanımını koşu boyunca izlemek (footprint büyüyor mu), uçuştaki kare
  sayısını sınırlamak (fb_queue), jetsam öncesi serbest sayfa sayısını loglamak.
- **Durum:** AÇIK. p12 kriteri "backboardd aynı pid'de kalır" bu yüzden garantilenemiyor.
  - **Bellek ölçümü (y281-y284, kernel BEGONIA MEM, sayfa = 16 KB):** normal bir oturumda serbest sayfa 120-200 (2-3 MB) dibinde, wired ~114-130 bin sayfa
    (~1.8-2.0 GB), aktif+pasif ~45 bin. Toplam ~177 bin sayfa (~2.8 GB). Yeniden başlamadan hemen sonra (y281) wired 34 bin (~540 MB), serbest 74 bin:
    yani ~90 bin sayfa (~1.4 GB) wired bellek backboardd ve istemcilerine ait (GPU tamponları/yüzeyler wired). Sistem sürekli sınırda, küçük bir
    sıçrama jetsam'i (reason 1/0x4, 1/0x5) tetikliyor. Her kare yeni yüzeyde geliyor ("none twice"): 5 sn de ~55-140 yüzey yapılıyor, 52-68 büyük (10 MB).
    Sonraki: hangi sürecin kaç wired bellek tuttuğunu görmek (süreç başına), PanVK BO/tiler heap boyutu, uçuştaki kare sayısını sınırlamak.
  - Bu akşam async swap ile (fb_gpuseq=1 metal_swapwait=0) ~20 koşuda 3 yeniden başlama (y272, y274, y281); y282-y284 ve y275 temiz. Oran eski "10 da 1" ile aynı mertebede.
  - **Süreç başına bellek (y288, footprint):** com.apple.WebKit[177] ~800 MB (10 sn'de +40 MB: büyüyor), WebKit[164] ~195 MB, backboardd ~483 MB,
    SpringBoard ~128 MB; toplam ~1.6 GB (cihazda ~2.8 GB sayfa). En büyük tek tüketici YouTube/Safari WebContent (~800 MB); backboardd'in ~480 MB'ı
    pencere sunucusu için büyük (gerçek iOS'ta yüzlerce MB değil, ~100-150 MB). Jetsam backboardd'i öldürüyor (WebContent'i değil). Sonraki:
    backboardd'in 480 MB'ının dökümü (yüzeyler, aynalar 37 MB, PanVK havuzları), uçuştaki kare sayısı (fb_queue), WebContent için limit.

### p13 sonucu (2026-11-20 akşam, kullanıcı; log yok)
- **Bölüm 12 (hız):** p13 (PanVK 0019: uniform yüklemeleri 16 hizalı) görüntüyü bozmadı ama hız hissedilir biçimde artmadı ("hâlâ çok yavaş"). Host'taki 1.6-2.2x
  derleyici döngü kazancı cihazda karşılık bulmadı. Çıkarım (ölçülmedi): darboğaz shader ALU/yükleme döngüsü değil; ANALIZ bölüm 13'teki hesapla uyumlu
  (ölçülen pass süresi döngü modelinin 5-10 katı). Şüphe: bellek/doku tarafı (LINEAR ekran ve uygulama yüzeyleri), pass başına sabit maliyet. Durum: AÇIK.
- **Bölüm 2b (jetsam):** p13'te de sürüyor; yeniden üretme: video izlerken ekrana dokunup ileri sarmak -> backboardd jetsam. Bellek tarafında bu turda hiçbir
  değişiklik yapılmadı. Durum: AÇIK.

### 4 denemenin sonucu (y292-y295, 2026-11-20 gece): kaydırma takılması AÇIK
- **Ölçülen durum (ana ekranda sağa/sola kaydırma):** ~15-20 fps, bütün kare aralıkları 35 ms üstü, GPU %88-95 meşgul. Kare başına ~50-60 ms GPU işi:
  1041x1180 ara hedef ~16 ms (karede 1 iş), ekran yüzeyi ~11-17 ms (8.6 ms/iş), slot 1 (vertex/tiler/compute) ~17-26 ms.
- **Kanıtlanan tek kazanç:** doğrudan indeksli çizim (PanVK 0021, `PANVK_DIRECT`): ~+%10 kare. p14'te açık.
- **Kazanç vermeyenler (ölçüldü):** 0020 INTERSECT ön-yükleme; 20 us bekleme uykusu; pixel işi ile vertex/tiler zincirini iki slotta aynı anda koşturmak
  (çiftler oluştu, resim doğru, kare sayısı aynı); şerit restart düzeltmesi (varsayım yanlıştı: şerit çizimi yok, çizim başına 6 köşe).
- **Bulunan ama DÜZELTİLMEYEN asıl yük:** slot 1 süresince 4 çekirdeğin hepsi compute ile dolu (5 sn'de ~5.5 milyar çekirdek-döngüsü, 63M warp =
  ~252M iş parçacığı; kare başına ~2.5M). Tiler'ın gördüğü üçgen yalnız ~36 bin/5 sn, çizim başına 6 köşe: yani yük çizimlerin vertex shader'ı değil.
  metal2vulkan'ın compute dispatch sayacı aynı oturumda `mipmap_filter.rs` sitesinden 1818, `ycbcr.rs` sitesinden 1000 dispatch gösteriyor. En olası
  kaynak: CoreAnimation'ın her karede istediği mip zinciri üretiminin (backdrop blur için) compute ile, piksel başına bir iş parçacığıyla yapılması.
  Bu eşleştirme (dispatch sayısı <-> compute döngüsü) dolaylı; dispatch başına süre ölçülmedi.
- **Sonraki adım (yapılmadı):** mip üretimini compute yerine render/blit ile yapmak ya da PanVK'da ucuzlatmak; ardından 1041x1180 blur pass'i (16 ms).
- **Jetsam (2b):** bu dört koşuda görülmedi (YouTube/sarma yalnız y292'de koştu); bellek tarafında değişiklik yapılmadı. AÇIK.
- **Araçlar kaldı:** çekirdekte `begonia_overlap=2` (dilimli istatistik + Mali donanım sayaçları + kalıcı `BEGONIA KEEP` metni), `begonia_prfcnt=1`.
