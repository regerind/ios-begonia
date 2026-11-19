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
