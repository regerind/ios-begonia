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
    gölgelendiricileri bunların hiçbirini yapmıyor; fark buradan (piksel başına ~12 ns, 270 MHz'lik G76'da).
  - Çeviricinin "Metal'le bit-kesin" öykünmesi bunun yalnızca ~%17'si; çalışma zamanı yaması 0004 ile kaldırıldı
    (y225: en uzun iş 31 → 24–28 ms).
- **Sonraki adımlar (etki sırasıyla, hiçbiri yapılmadı):**
  1. CoreAnimation'a hedefi okumayan (sabit işlevli karıştırma) boru hatlarını seçtirmek: `fc_framebuffer_fetch=0`
     yolu kitaplıkta var; CA'nın bunu aygıtın hangi yanıtına göre seçtiği bilinmiyor (QuartzCore'da tersine mühendislik
     gerek). En büyük kazanç burada olabilir: gölgelendirici küçülür, Mali'nin erken piksel eleme yolu açılır.
  2. PanVK JM'de çizim başı tanımlayıcı kopyalamayı GPU işi yerine CPU'da yapmak (karede ~23 ms'lik vertex/tiler
     süresinin çoğu; `bifrost/panvk_vX_meta_desc_copy.c`).
  3. Çeviricide kalan öykünmeler (koordinat yuvarlama, yarım duyarlık) için hızlı yol.
  4. GPU saati (270 MHz) — kullanıcının onayı gerekir, bu oturumda dokunulmadı.
  Tahminim: 2–3 birlikte en fazla ~2 kat (15–20 kare/sn); GL yolunun hızına ancak 1 ile yaklaşılır. Bu bir tahmin,
  ölçüm değil.
- **Yan bulgular:** çekimlerde zaman zaman renkli pikselli ince yatay çizgi (y212, y218) — çekim aktarımı mı, yerinde
  yüzeylerde önbellek tutarlılığı mı belirsiz; kullanıcının gözle bakması gerek. `metal=3` mediaserverd'ı Metal'e alıp
  çökertiyor (MTLResourceOptions 0x80020 ve eksik `newFunctionWithName:constantValues:pipelineLibrary:error:`):
  `metal=1` + liste kullan. Boru hatları her açılışta yeniden derleniyor (önbellek isabeti 0): ilk açılan her yeni
  görünümde ~100 ms'lik takılmalar.
