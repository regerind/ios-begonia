# Çözülen hatalar

**"Çözüldü" kesin değildir.** Her satırda nasıl doğrulandığı ve kalan şüphe yazılı. Bir hata geri dönerse
[ACIK-HATALAR.md](ACIK-HATALAR.md)'ye geri taşınır ve burada neden yetmediği not edilir.

Doğrulama türü: **kullanıcı** (sen elle denedin, çalıştı), **log** (logda görüldü, elle denenmedi), **ikisi**.

| Hata | Build | Nasıl düzeldi (kısaca) | Doğrulama | Şüphe / not |
|---|---|---|---|---|
| Ahizeden ses yok | y99 | Ses yolu: DL1 halka tamponu → DAC → MT6359 receiver; Darwin'in sesi çekirdekte tampona | kullanıcı ("ses çalıştı") | Ayrıntı: [NASIL-COZDUK](NASIL-COZDUK.md#ses) |
| YouTube açınca telefon tamamen donuyor | y95 | Ses sürücüsü, zaten tutulan kilidi bir daha istiyordu (kernel panic); ses seviyesi artık zamanlayıcıda okunuyor | log (panik yok) | — |
| Ses 1 sn çalıp döngüye giriyor / kesiliyor | y99 | Sesteki bir boşluktan sonra yazma konumu geride kalınca tampon "dolu" sanılıyordu; boşluktan sonra 40 ms ileriden yeniden başlıyor | ikisi | — |
| Alt hoparlörden ses yok | y100 | TFA9874 amfisi, Xiaomi'nin tfa98xx.cnt ayarlarıyla, I2S3 üzerinden | kullanıcı | DSP yok: hoparlör koruması yok, -6 dB sınır |
| Çift hoparlörde ahize duyulmuyor | y101 | Ahize tam seviye + receiver +4 dB | kullanıcı ("üst ve alt hoparlör çalışıyor") | Ahizeyi koruyacak bir sınır yok; en yüksek seste dikkat |
| Aramada yalnızca ahize | y101 | SpringBoard CallKit'e bakıyor: aramada yalnızca ahize, "Hoparlör"e basılırsa ikisi | log (rota satırı) | Gerçek bir aramayla henüz denenmedi |
| Parlaklık kaydırıcısı kilitli / çalışmıyor | y94 | iOS'un aradığı `AppleARMBacklight` servisi yazıldı (IODisplayParameters); LM36273'e gidiyor | ikisi | — |
| Kilitliyken ekran açık kalıyor | y93 civarı | SpringBoard'ın "ekran karartıldı" bildirimiyle arka ışık kapanıyor | kullanıcı ("screen now turns off") | — |
| Dokununca uyanma (tap-to-wake) istenmiyor | y95 | Karanlık ekranda dokunuşlar yutuluyor; dokunmatik yarım saniyede bir okunuyor (pil) | log | Kullanıcı ayrıca onaylamadı |
| Wi-Fi çalışmıyor | y5x | MT6781 değerleri MT6785'e taşındı, hatlar onaylı açıldı, ROM yaması, RXD 16 | kullanıcı (ağa bağlandı) | Ayrıntı: [NASIL-COZDUK](NASIL-COZDUK.md#wi-fi). Videoda ağ süreci 1 çekirdek yiyor: açık hata 4 |
| Kalın yazı açınca SpringBoard çöküyor | y58 | SpringBoard Metal istemcisi yapıldı | kullanıcı | SpringBoard'ın Metal'e geçmesi panel donmalarına katkı yapıyor olabilir (açık hata 1) |
| Safari sayfaları siyah | y5x | WebKit GPU ve WebContent süreçleri Metal istemcisi | kullanıcı | Overlay yanıp sönmesi bu yoldan olabilir (açık hata 3) |
| Safari'de bazı öğeler (Google logosu) çizilmiyor | p6 öncesi | Framebuffer fetch reddediliyordu; metal2vulkan'da AcceptUnordered | kullanıcı | — |
| Mor arka plan (tek el modu vb.) | y10 | Wallpaper anlık görüntüsü (.atx) mor açılıyordu; ekrandan alınan resimle değiştirildi (0030) | kullanıcı | Gerçek çözüm değil, geçici yama: .atx hâlâ çözülemiyor (açık hata 6) |
| Fener | y8x | MT6360 flaş LED'i (I2C5), Denetim Merkezi'ne bağlı | kullanıcı | — |
| Durum çubuğunda yanlış pil | 0040 | Pil yüzdesi yenileniyor | kullanıcı | — |
| Takılmalar (GPU bekleme) | y79 | GPU işini beklerken önce 10 kısa kontrol, sonra 50 µs uykular | kullanıcı ("really smooth") | Kalan takılmalar başka kaynaklı |
