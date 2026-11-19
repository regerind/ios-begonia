# Erkam docs

Redmi Note 8 Pro (begonia, MT6785) üzerinde iOS 16 portunun takip defteri. Kod ve commit'ler ayrıntıyı tutar; burası
"ne bozuk, ne düzeldi, nasıl düzeldi, hangi build'de ne oldu" sorularının tek yeri.

| Dosya | Ne var |
|---|---|
| [YAPILACAKLAR.md](YAPILACAKLAR.md) | Sıradaki işler, öncelik sırasıyla |
| [ACIK-HATALAR.md](ACIK-HATALAR.md) | Şu an bozuk olan her şey: belirti, kanıt, şüphe, durum |
| [COZULEN-HATALAR.md](COZULEN-HATALAR.md) | Düzeldi denen her şey: hangi build'de, nasıl doğrulandı, hangi şüpheyle |
| [NASIL-COZDUK.md](NASIL-COZDUK.md) | Büyük konuların hikâyesi: Wi-Fi, parlaklık, ses, GPU bekleme... kök neden ve çözüm |
| [BUILD-LOG.md](BUILD-LOG.md) | Her x/y/p build'i: ne değişti, sonuç ne oldu |

## Kurallar

- **Her build loglanır.** Build alınır alınmaz `BUILD-LOG.md`'ye bir satır; sonuç gelince satır tamamlanır.
- **Sonra hatalar güncellenir.** Yeni ya da hâlâ süren hata `ACIK-HATALAR.md`'ye; düzelen `COZULEN-HATALAR.md`'ye taşınır.
- **"Çözüldü" kesin değildir.** Belirtiyi gizleyip asıl sorunu bırakmış olabiliriz. Bu yüzden her çözülen hatada
  "nasıl doğrulandı" ve "şüphe" yazılır. Doğrulama yalnızca log olabilir, ya da senin elle denemen olabilir; hangisi
  olduğu yazılır. Bir hata geri dönerse `ACIK-HATALAR.md`'ye geri taşınır ve eski çözümün neden yetmediği not edilir.
- Ajan (Claude) bu kuralları repodaki [CLAUDE.md](../CLAUDE.md) üzerinden de alır.
