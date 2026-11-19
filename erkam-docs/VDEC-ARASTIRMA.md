# VDEC (donanım video çözücü) araştırması — MT6785 / begonia

Tarih: 2026-11-20. Yalnızca araştırma: cihaza dokunulmadı, derleme yapılmadı.

Kaynak ağaç: `MiCode/Xiaomi_Kernel_OpenSource`, dal `begonia-q-oss`. İndirilen dosyaların yerel kopyaları
`C:\begonia-private\dl\vdec\` altında, yol `/` yerine `_` ile düzleştirilmiş adla duruyor (örnek:
`drivers/media/platform/mtk-vcu/mtk_vcu.c` → `drivers_media_platform_mtk-vcu_mtk_vcu.c`). Aygıt ağacı:
`C:\begonia-private\dl\audio2\mt6785.dts` (aşağıda "dts:satır").

Kısaltmalar: VCU = MediaTek'in "Video Communication Unit" yolu (kodek mantığı kullanıcı alanındaki `vpud`
sürecinde çalışır). SPM = güç alanı denetleyicisi. CG = saat kapısı. larb = SMI yerel bellek hakemi. M4U = MediaTek IOMMU.

---

## 1. Hangi sürücü, çözme nerede çalışıyor, hangi üretici yazılımı?

**Sonuç: MT6785, V4L2 `mtk-vcodec` + `mtk-vcu` yolunu kullanıyor. Çözme işinin mantığı (bit akışı ayrıştırma,
yazmaç programlama) kapalı kaynaklı kullanıcı alanı süreci `vpud` içinde, ana işlemcide (AP) çalışıyor. Ayrı bir
yardımcı işlemci ve çekirdeğin yüklediği bir üretici yazılımı dosyası yok.**

- Yapılandırma: `arch/arm64/configs/begonia_user_defconfig` → `CONFIG_VIDEO_MEDIATEK_VCODEC=y`,
  `CONFIG_VIDEO_MEDIATEK_VCU_WO_FUSE=y`, `CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT=y`. `CONFIG_VIDEO_MEDIATEK_VPU` yok.
- `drivers/media/platform/Kconfig:275-288`: `VIDEO_MEDIATEK_VCODEC` → `select VIDEO_MEDIATEK_VCU`
  ("This driver rely on VCU driver to communicate with VPU"). `:226-233`: WO_FUSE = FUSE'suz, doğrudan ioctl ile IPI.
- `drivers/media/platform/mtk-vcodec/Makefile`: VCU açıkken derlenen çözücü yalnızca
  `vdec/vdec_common_if.c` + `vdec_vcu_if.c` (kodekten bağımsız ortak katman). `vdec_h264_if.c`, `vdec_vp9_if.c`
  yalnızca VPU (MT8173 tarzı) yolunda derleniyor, MT6785'te kullanılmıyor.
- Platforma özel parçalar: `drivers/media/platform/mtk-vcodec/mt6785/` (`mtk_vcodec_dec_pm.c`,
  `mtk_vcodec_intr.c`, `vcodec_dvfs.c`, `vcodec_bw.c`).
- VCU sürücüsü `drivers/media/platform/mtk-vcu/mtk_vcu.c`:
  - `:66` `#define VCU_PATH "/dev/vpud"`; `:1094-1101` aygıtı açan süreç adı `vpud` ise vcuid=0 (çözücü/kodlayıcı),
    `mdpd` ise 1, `camd` ise 2.
  - `:340-445` `vcu_ipi_send`: çekirdek iletiyi `VCU_SET_OBJECT` ioctl'ü ile `vpud`'a iletiyor, 6 sn ACK bekliyor
    (`IPI_TIMEOUT_MS`, `:71`).
  - `:1815-1828` (probe): dts `vcu` düğümündeki `reg` alanlarını `map_base[]` olarak saklıyor;
    `:1176-1210` `mtk_vcu_mmap`: `vpud` (vcuid 0) bu fiziksel yazmaç bölgelerini **önbelleksiz olarak doğrudan
    kendi adres alanına eşliyor**. Yani VDEC yazmaçlarını kullanıcı alanı yazıyor.
  - `:581-614` `vcu_set_gce_cmd`: `vpud` ayrıca GCE (komut kuyruğu motoru) üzerinden yazmaç yazma/okuma/bekleme
    komut listeleri gönderebiliyor (`VCU_GCE_SET_CMD_FLUSH`, `include/uapi/linux/mtk_vcu_controls.h:101-109,172`).
  - `:899-907` `vcu_load_firmware` boş: `return 0`. Çekirdekte `request_firmware` çağrısı yok.
- IPI ileti kimlikleri `drivers/media/platform/mtk-vcodec/vdec_ipi_msg.h:58-81`: `AP_IPIMSG_DEC_INIT 0xA000`,
  `DEC_START 0xA001`, `DEC_END`, `DEC_DEINIT`, `DEC_RESET`, `SET_PARAM`, `QUERY_CAP`; geri yönde
  `VCU_IPIMSG_DEC_WAITISR 0xC000`, `GET_FRAME_BUFFER 0xC001`, `PUT_FRAME_BUFFER`, `LOCK_CORE 0xC003`, `UNLOCK_CORE`.
  Paylaşılan bellek yapısı `struct vdec_vsi` (`:355-361`): boş bit akışı halkası, boş/gösterime hazır kare halkaları,
  resim bilgisi, renk tanımı.
- Görev bölüşümü (`vdec_vcu_if.c:209-250`): `vpud` "kesmeyi bekle" dediğinde çekirdek kesmeyi bekliyor
  (`WAITISR`), "çekirdeği kilitle" dediğinde güç/saat açıyor (`LOCK_CORE` → `vdec_decode_prepare`), kare tamponu
  istediğinde V4L2 kuyruğundan veriyor. Kesme işleyici `mt6785/mtk_vcodec_intr.c:65-105`.
- Desteklenen formatlar bile çekirdekte sabit değil: `mtk_vcodec_dec.c:34-75` listeyi `vpud`'dan sorguluyor.
- Diğer "VPU"lar karıştırılmamalı: `drivers/misc/mediatek/vpu/mt6785/` ve `CONFIG_MTK_VPU_SUPPORT=y` APU/yapay zekâ
  işlemcisine ait, video ile ilgisi yok. `CONFIG_MTK_TINYSYS_SCP_SUPPORT=y` SCP (sensör/ses yardımcı işlemcisi) için;
  VCU yolu SCP kullanmıyor. Eski `/dev/Vcodec` sürücüsü (`drivers/misc/mediatek/videocodec/`,
  `MTK_VIDEOCODEC_DRIVER default n`) MT6785 için alt dizine sahip değil ve açık değil.

**Gereken "üretici yazılımı":** dosya değil, Android vendor bölümündeki kapalı kullanıcı alanı ikilileri:
`/vendor/bin/vpud` (yol `mtk_vcu.c:66` ile doğrulandı) ve onun yüklediği MediaTek kodek kütüphaneleri
(`libvcodecdrv.so`, `libvcodec_utility.so`, `libvcodec_oal.so` gibi adlar — **doğrulanmadı**, MT6785 vendor
imajında listelenmeli). Bu kütüphaneler Android bionic + ioctl arayüzüne bağlı; iOS'ta doğrudan çalışmaz.

**Açık kaynak yazmaç düzeyi belge:** MT6785 VDEC için bulunamadı (**doğrulanmadı / yok**). Ana hat Linux'taki
MT8183/MT8192/MT8195 "stateless" çözücüleri de yazmaçları SCP üretici yazılımına (`mediatek/mt8183/scp.img`)
yazdırıyor; açık sürücüde H.264 yazmaç dizisi yok.

---

## 2. Güç, saatler, larb, M4U ve adresler

### 2.1 Fiziksel adresler (dts)

| Blok | Adres | Kaynak |
|---|---|---|
| VDEC tüm bölge (vpud'un eşlediği) | `0x16000000`, boyut `0x40000` | dts:3285-3291 (`vcu` düğümü, ayrıca VENC `0x17020000`, VENC_LT `0x19002000`) |
| VDEC_SYS = vdec_gcon (CG) | `0x16000000` (+0x1000) | dts:3334-3337 |
| VDEC_MISC (kesme yazmacı) | `0x16025000` | dts:3337 |
| vdec_mbist | `0x16001000` | dts:3366-3369 |
| smi_larb2 (VDEC larb'ı) | `0x16010000`, kesme GIC_SPI 330 | dts:3447-3454 |
| vld / vld_top | `0x16020000` / `0x16020800` | dts:3371-3385 |
| mc | `0x16021000` | dts:3387 |
| avc_vld / avc_mv (H.264) | `0x16022000` / `0x16023000` | dts:3392-3400 |
| vdec_pp | `0x16024000` | dts:3402 |
| vdtop | `0x16025000` | dts:3407 |
| vp6 / vld2 / avs_vld / vp8_vld | `0x16026000` / `0x16026800` / `0x16027000` / `0x16027800` | dts:3412-3430 |
| hevc_vld / vp9_vld | `0x16028000` / `0x16028400` | dts:3432-3440 |
| vdec_core0_vad | `0x1602c000` | dts:3442 |
| VDEC kesmesi | GIC_SPI 329, seviye düşük | dts:3342, dts:3374 |
| SPM (güç) | `0x10006000` | dts:457-465 (`scpsys` reg[1]) |
| infracfg_ao (veri yolu koruması) | `0x10001000` | `scpsys` reg[0] |
| topckgen (saat seçici) | `0x10000000` | dts:445, `scpsys` reg[3] |
| smi_common | `0x14019000` | dts:3082, `scpsys` reg[4] |
| M4U0 (iommu0) | `0x10220000`, GIC_SPI 187 | dts:848-857 |
| M4U1 | `0x1024f000`, GIC_SPI 192 | dts:859-865 |
| GCE | `0x10228000` | dts:1608, dts:1854 |

Not: `gce` düğümündeki `vdec_gcon_base = <0x18800000 ...>` (dts:1618) ve `vdec_base = <0x17010000 ...>` başka
SoC'den kalma görünüyor; gerçek VDEC adresi `vcu`/`vdec_gcon` düğümlerindeki `0x1600xxxx` (**çelişki, VDEC_SYS
okuması ile doğrulanmalı**).

`scpsys` reg sırası `drivers/clk/mediatek/clk-mt6785-pg.c:5060-5064` ile doğrulandı: 0=infracfg, 1=spm, 2=infra,
3=ckgen, 4=smi_common.

### 2.2 Güç alanı: `SCP_SYS_VDE` (SPM "VDE")

Kaynak: `drivers/clk/mediatek/clk-mt6785-pg.c`.

- `VDE_PWR_CON = SPM + 0x300` → **`0x10006300`** (`:173`). `PWR_STATUS = SPM+0x160` (`0x10006160`),
  `PWR_STATUS_2ND = SPM+0x164` (`0x10006164`) (`:165-166`). VDE durum biti **bit 31** (`:522`).
- PWR_CON bitleri (`:252-256`): `RST_B` bit0, `ISO` bit1, `PWR_ON` bit2, `PWR_ON_2ND` bit3, `CLK_DIS` bit4;
  `VDE_SRAM_PDN` bit8, `VDE_SRAM_PDN_ACK` bit12 (`:574-576`).
- Veri yolu koruması: `INFRA_TOPAXI_PROTECTEN_MM_SET/CLR/STA1` = infracfg + `0x2D4/0x2D8/0x2EC`
  (`0x100012D4/…D8/…EC`), VDE adım 1 maskesi **bit1** (`:485-486`). `SMI_COMMON_SMI_CLAMP/SET/CLR` =
  smi_common + `0x3C0/0x3C4/0x3C8` (`0x140193C0/…C4/…C8`), VDE adım 2 maskesi **bit2** (`:245-247`, `:487-488`).
- Açma sırası `spm_mtcmos_ctrl_vde(STA_POWER_ON)` (`:3976` işlevi, açma dalı `:4045-4110` civarı):
  1. `PWR_ON=1`, `PWR_ON_2ND=1`;
  2. `PWR_STATUS` ve `PWR_STATUS_2ND` bit31 = 1 olana kadar bekle;
  3. `CLK_DIS=0`, `ISO=0`, `RST_B=1`;
  4. `SRAM_PDN` (bit8) = 0, `SRAM_PDN_ACK` (bit12) = 0 olana kadar bekle — kaynakta not: "Need f_fvdec_ck for SRAM
     PDN delay IP", yani **`vdec_sel` saati önceden açık olmalı**;
  5. `SMI_COMMON_SMI_CLAMP_CLR` ← bit2;
  6. `INFRA_TOPAXI_PROTECTEN_MM_CLR` ← bit1.
- Kapatma ters sırada, koruma ACK'leri beklenerek (`:3986-4045`).
- Ebeveyn: `PGATE(SCP_SYS_VDE, pg_vde, pg_dis, vdec_sel, SYS_VDE)` (`:4882`): **DIS (ekran) alanı açık olmalı** ve
  `vdec_sel` ön saattir. Ekran zaten çalıştığı için DIS açık.
- `POWERON_CONFIG_EN = SPM+0x000`, `SPM_PROJECT_CODE 0xB16` tanımlı (`:164`, `:249`); VDE işlevinde bu kilidin
  açılması yapılmıyor (yorum boş). Önyükleyici sonrası SPM yazmalarının açık olduğu varsayılıyor (**doğrulanmadı**,
  ilk adımda `PWR_CON` yazıp geri okuyarak kontrol edilmeli).

### 2.3 Saatler

Kaynak: `drivers/clk/mediatek/clk-mt6785.c`.

- `vdec_sel` seçici (`:1229-1232`): `CK_CFG_12` = topckgen + `0xE0` (SET `0xE4`, CLR `0xE8`) (`:264-266`),
  alan **bit 19:16**, kapı (pdn) **bit 23**, güncelleme yazmacı topckgen + `0x008` **bit 19**
  (makro alan sırası `clk-mtk.h:162-168` ile doğrulandı). Bayrak `CLK_IS_CRITICAL`.
- Ebeveyn listesi (`:924-935`), indeks sırasıyla: 0 `clk26m`, 1 `univpll_d2_d4`, 2 `mainpll_d3`, 3 `univpll_d2_d2`,
  4 `mainpll_d2_d2`, 5 `univpll_d3`, 6 `univpll_d5`, 7 `univpll_d5_d2`, 8 `mainpll_d2`, 9 `univpll_d2`,
  10 `univpll_192m_d2`. MHz değerleri univpll = 1248, mainpll = 1092 varsayımıyla 156 / 364 / 312 / 273 / 416 /
  249.6 / 124.8 / 546 / 624 / 96 (**PLL hızları doğrulanmadı**).
- mmdvfs adımları (dts:3121-3123): `vdec_step0 = 624`, `step1 = 416`, `step2 = 312` MHz. Yüksek adımlar vcore
  gerilimine bağlı (mmdvfs/vcore DVFS); ilk denemede 312 MHz veya altı güvenli seçim.
- VDEC CG'leri, vdec_gcon `0x16000000` içinde, **ters mantık** (`mtk_clk_gate_ops_setclr_inv`: SET'e 1 yazmak saati
  AÇAR) (`:2851-2909`):
  - `VDEC_VDEC` "vdec_cken": SET `0x16000000` / CLR `0x16000004`, bit0;
  - `VDEC_LARB1` "vdec_larb1_cken" (VDEC'in kendi larb'ı; küresel adı SMI larb2): SET `0x16000008` / CLR
    `0x1600000C`, bit0.
  - `MT_CCF_BRINGUP` = 0 (`:28`) olduğundan açılışta otomatik açılmıyor (`:3695-3698`).
- Sürücünün kullandığı saat adları (dts:3345-3362, `mt6785/mtk_vcodec_dec_pm.c:100-125`): `MT_SCP_SYS_DIS`,
  `MT_SCP_SYS_VDE`, `MT_SCP_SYS_VEN`, `MT_CG_SMI_COMMON`, `MT_CG_GALS_COMM0/1`, `MT_CG_VDEC`, `MT_CG_VENC`.
- Saat açma (`mtk_vcodec_dec_pm.c:149-159`): `smi_bus_prepare_enable(SMI_LARB2, "VDEC")` ardından
  `clk_prepare_enable(MT_CG_VDEC)`. smi_larb2 düğümünün saatleri `SCP_SYS_VDE` + `VDEC_VDEC` (dts:3451). Yani zincir:
  DIS açık → mm saatleri / SMI_COMMON CG → vdec_sel → VDE güç alanı → VDEC CG'leri → larb2.
- Not: dts `vdec_gcon` düğümünde `mediatek,larb = <&smi_larb1>` (dts:3341) yazıyor, ama kod `SMI_LARB2`
  kullanıyor; doğru larb **larb2 @ 0x16010000** (M4U port tablosu da bunu söylüyor).

### 2.4 SMI larb2 ve M4U portları

- `include/dt-bindings/memory/mt6785-larb-port.h:51-63` ve `drivers/misc/mediatek/m4u/mt6785/m4u_port.h:49-61`:
  larb2 = "VDEC", 12 port: 0 `HW_VDEC_MC_EXT`, 1 `UFO_EXT`, 2 `PP_EXT`, 3 `PRED_RD_EXT`, 4 `PRED_WR_EXT`,
  5 `PPWRAP_EXT`, 6 `TILE_EXT`, 7 `VLD_EXT`, 8 `VLD2_EXT`, 9 `AVC_MV_EXT`, 10 `UFO_ENC_EXT`, 11 `RG_CTRL_DMA_EXT`.
- MT6785 eski M4U sürücüsünü (`drivers/misc/mediatek/m4u/3.2` + `mt6785/`) kullanıyor
  (`drivers/misc/mediatek/m4u/Makefile`: `obj-$(CONFIG_MACH_MT6785) += 3.2/`); dts'deki `iommus` satırları
  `#ifdef CONFIG_MTK_IOMMU_V2` içinde ve bu yapılandırma begonia'da açık değil (**defconfig'de yok; Kconfig varsayılanı
  doğrulanmadı**).
- Larb başına port MMU anahtarı (`drivers/misc/mediatek/m4u/mt6785/m4u_reg.h:432-437`):
  `SMI_LARB_NON_SEC_CONx(port) = larb + 0x380 + port*4`, bit0 = MMU açık; güvenli eşi `0xF80 + port*4`.
  larb2 için: `0x16010380 … 0x160103AC`. Bit0 = 0 ise port fiziksel adresle (MMU'suz) çalışır
  (`m4u_hw.c:1333-1338`).
- larb2 ayarları (`drivers/misc/mediatek/smi/mt6785/smi_conf.h:70-73`): `SMI_LARB_CMD_THRT_CON = 0x300256`,
  `SMI_LARB_SW_FLAG = 1`.
- VDEC'in çok sayıda DMA portu var; MMU'suz denemede tüm tamponlar **fiziksel olarak bitişik** olmalı.

---

## 3. Tek H.264 karesinin en kısa yolu, çıkış formatı

### 3.1 Vendor yolunda bir karenin akışı (çekirdek tarafı doğrulandı)

`vdec_common_if.c` + `vdec_vcu_if.c` + `mtk_vcodec_dec.c`:
1. `AP_IPIMSG_DEC_INIT` (kodek fourcc) → `vpud` örnek oluşturur, `vdec_vsi` paylaşılan belleğini döner
   (`vdec_common_if.c:175`).
2. Bit akışı tamponu (dma-buf, `bs->dma_addr`) `vsi->dec.bs_dma`'ya yazılır (`:237-242`); kare tamponunun her düzlemi
   (`fb->fb_base[i].dma_addr`, Y ve C) aktarılır (`:225`, `:254`).
3. `AP_IPIMSG_DEC_START` (`:274`) → `vpud` başlığı ayrıştırır; gerekirse `GET_FRAME_BUFFER` ister, `LOCK_CORE` ile
   güç/saati açtırır, yazmaçları (doğrudan mmap veya GCE) yazar, `WAITISR` ile kesmeyi bekletir.
4. Kesme (`mt6785/mtk_vcodec_intr.c:65-105`, `mtk_vcodec_intr.h:25-29`): `VDEC_SYS+0` okunur, bit4
   (`MTK_VDEC_HW_ACTIVE 0x10`) 0 olmalı; `VDEC_MISC + 0xA4` okunur, bit16 (`0x10000`) = başarılı çözüm;
   temizleme: `0xA4`'e `|0x11` sonra `&~0x10` yazılır. Yani **`0x160250A4` kesme durumu/temizleme yazmacı.**
5. `vpud` hazır kareyi `list_disp` halkasına koyar; V4L2 yakalama kuyruğundan çıkar.

### 3.2 Yazmaç düzeyi H.264 dizisi

**Doğrulanmadı / kaynakta yok.** SPS/PPS/dilim başlığını ayrıştırıp `avc_vld` (`0x16022000`), `vld`, `mc`, `pp`
yazmaçlarını programlayan kod tamamen `vpud`/kodek kütüphanelerinde. Elde etmenin üç yolu:

- **(a) Android'de iz kaydı:** `vpud` yazmaçların bir kısmını GCE komut listesi olarak gönderiyor; bunlar
  `VCU_GCE_SET_CMD_FLUSH` ioctl'ünde (adres, değer, maske dizileri) çekirdekten geçiyor ve çekirdek tarafında
  loglanabilir. Doğrudan mmap ile yapılan yazmalar için ise `vpud`'un `mmap`'ini engelleyip sayfa hatasıyla
  yakalama (kullanıcı alanı mmiotrace) gerekir. Bu, Android'i telefonda değiştirilmiş çekirdekle çalıştırmayı
  gerektirir (cihaz işi; sonraki aşama).
- **(b) Tersine mühendislik:** stok MIUI begonia vendor imajındaki `vpud` ve kodek `.so`'larını (cihaza dokunmadan,
  indirilebilir ROM'dan) disassembler ile incelemek.
- **(c) Kardeş SoC belgeleri:** MT8183 (aynı kuşak, VDEC yine `0x16000000`) için açık sürücü var, ama yazmaç
  programlama orada da SCP üretici yazılımında; doğrudan yardımcı değil (**doğrulanmadı**).

### 3.3 Çıkış piksel formatı

- MediaTek fourcc'leri `include/uapi/linux/videodev2.h:690-728`: `MT21C` ('M','T','2','1', sıkıştırılmış blok),
  `MT21` ('M','M','2','1', 8 bit blok, iki ayrı düzlem), `MT21S` (iki bitişik düzlem), 10 bit ve UFO (sıkıştırılmış)
  türevleri, `MT10`.
- Ana hat Linux belgesi (https://docs.kernel.org/userspace-api/media/v4l/pixfmt-yuv-planar.html): MM21 = luma 16x32
  döşeme, kroma 16x16 döşeme, luma ve kroma döşeme sayısı aynı, iki ayrı düzlem. Bu, NV12'nin döşemeli hâli
  (kroma UV yan yana).
- MT6785'in hangi çıkış formatlarını sunduğu `vpud`'dan sorgulanıyor (`mtk_vcodec_dec.c:34-75`), çekirdekte sabit
  değil. Doğrusal NV12 çıkışı olup olmadığı **doğrulanmadı**. MediaTek'in bu kuşağında çözücü çıkışı genelde blok
  (MT21/MT21C) ve doğrusal NV12'ye MDP (RDMA → WROT) ile çevriliyor; UFO sıkıştırması ayrı bir port
  (`UFO_EXT`) ile açılıp kapanıyor (`mtk_vcodec_dec_pm.c:326-329` "if (0) /* UFO */").
- **iOS için:** IOSurface `'420v'` (`kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`) doğrusal NV12. MT21'den
  çevirme gerekli. Seçenekler:
  1. İşlemcide döşeme açma (360p'de kare başına ~345 KB; 16x32 blok kopyası, NEON ile ucuz). Libyuv'da
     `MM21ToNV12` var (**sürüm doğrulanmadı**).
  2. GPU (panfrost/Metal) gölgelendiricisiyle döşeme açma.
  3. MDP donanımı (`mdp_rdma0 0x14001000` vb., larb1 portları `MDP_RDMA0/WROT0`) — ayrı bir sürücü işi, en sona.
  İlk adımda (1) yeterli: 0.5 çekirdeklik yazılım çözmenin yerine birkaç ms'lik kopya.

---

## 4. VP9

- Donanımda VP9 çözme bloğu var: dts `vp9_vld@16028400` (dts:3437-3440), ayrıca `hevc_vld@16028000`,
  `vp8_vld@16027800`.
- Sürücü VP9 için bant genişliği tablosu tutuyor (`mtk_vcodec_dec_pm.c:44`, `:306-308`) ve VP9'u kodek listesinde
  sayıyor (`mtk_vcodec_dec.c:1285`).
- MediaTek ürün sayfası (Helio G90 serisi, https://www.mediatek.com/products/smartphones/mediatek-helio-g90):
  oynatma H.264, H.265/HEVC, VP-9; 4K30. CNX Software G90T için aynı listeyi veriyor
  (https://www.cnx-software.com/2019/07/30/mediatek-helio-g90-g90t-processors-are-designed-for-gaming-smartphones/).
  AnandTech tablosu (https://www.anandtech.com/show/14684) yalnızca H.264/HEVC yazıyor — kaynaklar arasında küçük
  çelişki var, ama dts'deki `vp9_vld` bloğu donanımın varlığını destekliyor.
- Sonuç: VP9 donanımı **var**, fakat yazmaç dizisi yine kapalı. Öncelik H.264 olmalı; iOS Safari'de YouTube
  çoğunlukla `avc1` alıyor (iOS 16'da VP9 yalnızca belirli durumlarda; **doğrulanmadı**).

---

## 5. iOS tarafı: VideoToolbox çözücü eklentileri

### 5.1 iOS 16.5'te doğrulananlar (yerel SDK: WSL `/root/theos/sdks/iPhoneOS16.5.sdk`)

- `System/Library/VideoDecoders/` içinde `.tbd` saplamaları (her biri bir `.videodecoder` dylib'i, gerçek kodları
  dyld paylaşılan önbelleğinde):
  - `AVD.tbd` → install-name `/System/Library/VideoDecoders/AVD.videodecoder`, tek dışa aktarım `_AVDRegister`
  - `H264H8.tbd` → `_H264H8Register`
  - `VCPMP4V.tbd` → `_VCPMP4VRegister`, `_VCPMP4VRegisterInternal`
  - `AV1SW.tbd` → `_AV1Decoder_CreateInstance`, `_AV1RegisterDecoder` (yazılım AV1)
  - ayrıca `AppleProResHWDecoder`, `AppleProResSWDecoder`, `JPEGH1`, `MP4VH8`, `VCH263`.
  - Desen: her eklentinin tek işi bir `…Register` işlevi; bu da VideoToolbox'a kendini kaydediyor.
- `VideoToolbox.tbd` içindeki eklenti API'si (özel, başlık yok):
  - Kayıt: `_VTRegisterVideoDecoder`, `_VTRegisterVideoDecoderWithInfo` (kodlayıcı için `_VTRegisterVideoEncoder…`).
  - Çözücü nesnesi: `_VTVideoDecoderGetClassID`, `_VTVideoDecoderGetTypeID`, `_VTVideoDecoderGetCMBaseObject`,
    `_VTVideoDecoderGetFigBaseObject` (CMBase/FigBase sınıf modeli: vtable'lı CF nesnesi).
  - Oturuma geri çağrılar: `_VTDecoderSessionEmitDecodedFrame`, `_VTDecoderSessionEmitDecodedTile`,
    `_VTDecoderSessionCreatePixelBuffer`, `_VTDecoderSessionGetPixelBufferPool`,
    `_VTDecoderSessionSetPixelBufferAttributes`, `_VTDecoderSessionGetDestinationPixelBufferAttributes`,
    `_VTDecoderSessionCleanUpAfterDecode`, `_VTDecoderSessionTrace`.
- Telefona giden rootfs (`/root/begonia-ios/upstream/hakios-xnu-kernel/sysimage/rootfs/System/Library/VideoDecoders`)
  yalnızca `AV1SW.metallib` içeriyor; Info.plist'li paket yok. Yani iOS'ta çözücüler disk taramasıyla değil,
  VideoToolbox içindeki bir listeden/önbellekteki dylib'lerden yükleniyor (**yükleme mekanizması doğrulanmadı**).

### 5.2 Donanım yolu ve iPhone X

- AppleAVD: A12 ve sonrası SoC'lerdeki çekirdek video hızlandırıcı sürücü ailesi; çözümün önemli kısmı çekirdekte
  (h26forge makalesi, https://alum.mit.edu/www/wrv/h26forge.pdf; Dmitriev sunumu,
  https://www.slideshare.net/MaxDmitriev/cinematimenewpdf: `AppleAVDUserClient`, `createDecoder`,
  `decodeFrameFig`). Apple Silicon AVD tersine mühendisliği: https://github.com/eiln/avd.
- Bizim IPSW iPhone10,3/10,6 (A11). A11'in donanım çözücü sürücüsü AppleAVD değil (makaleye göre AVD A12 ile
  başlıyor); A11'de hangi `.videodecoder`'ın donanım yolu olduğu (`H264H8`? ) **doğrulanmadı**. Bizim için önemsiz:
  Apple'ın donanım çekirdek eklentileri bu telefonda zaten yok, bu yüzden H.264 şu an yazılımla çözülüyor
  (`ACIK-HATALAR.md` madde 4: mediaserverd ~%50).

### 5.3 Üçüncü taraf çözücünün yapması gerekenler

Kamuya açık başlık yok; aşağıdaki arayüz şekli sembol adlarından ve Apple'ın macOS 15'teki resmi karşılığı
MediaExtension `MEVideoDecoder`'dan (https://developer.apple.com/documentation/mediaextension) çıkarım —
**ayrıntılar doğrulanmadı, `AV1SW.videodecoder` paylaşılan önbellekten çıkarılıp disassemble edilerek doğrulanmalı**
(dışa aktarılan `AV1Decoder_CreateInstance` + `AV1RegisterDecoder` tam bir yazılım eklentisi örneği):

1. Kayıt: `VTRegisterVideoDecoder('avc1', CreateInstance)` (veya `…WithInfo` ile bir sözlük: kodek tipi, ad, kimlik,
   donanım hızlandırmalı mı).
2. `CreateInstance(codecType, allocator, &decoder)`: `VTVideoDecoderGetClassID()` sınıfından CMBase nesnesi oluştur.
3. vtable işlevleri (MediaExtension karşılıklarıyla): `StartSession(decoder, session, formatDescription)` (avcC'den
   SPS/PPS), `DecodeFrame(decoder, frameRefCon, sampleBuffer, flags, &infoFlags)`, `CopySupportedPropertyDictionary`,
   `SetProperties`, `CanAcceptFormatDescription`, `FinishDelayedFrames`, `Invalidate/Finalize`.
4. Çıkış: `VTDecoderSessionSetPixelBufferAttributes` ile `'420v'` + IOSurface iste,
   `VTDecoderSessionCreatePixelBuffer` ile IOSurface destekli `CVPixelBuffer` al, içine NV12 yaz,
   `VTDecoderSessionEmitDecodedFrame(session, frameRefCon, status, infoFlags, imageBuffer)` ile teslim et.
5. Sıralama: H.264'te B-kareleri için gösterim sırası; ilk sürümde Main/High profilinde yeniden sıralama
   `vpud` benzeri mantıkla bizim katmanda yapılmalı.

Yükleme seçenekleri (hepsi **doğrulanmadı**):
- (A) Kendi dylib'imizi mediaserverd / WebKit GPU sürecine yükleyip (Theos ortamı hazır) `VTRegisterVideoDecoder`
  ile H.264 için önce/üstün kaydolmak.
- (B) Sistemdeki bir `.videodecoder` kaydını (örn. `H264H8Register`) kancalamak.
- Hangi kaydın seçildiği (ilk/son kayıt, donanım önceliği, `kVTVideoDecoderSpecification_*` anahtarları)
  disassembly ile belirlenmeli.

### 5.4 Çekirdek (HakiOS XNU) tarafı

AppleAVD'nin yaptığı işi bizim küçük bir IOKit sürücüsü yapmalı: VDE güç/saat açma-kapama, `0x16000000` bölgesini
(veya alt bloklarını) kullanıcı istemcisine eşleme ya da yazmaç listesi çalıştırma, GIC_SPI 329 kesmesini bekleme,
IOSurface sayfalarının fiziksel adreslerini (veya M4U eşlemesini) verme. GPU servisimizin IOSurface eşleme kodu
yeniden kullanılabilir.

---

## Önerilen sıra

Her adım ayrı bir test görüntüsü; IMEI bölümlerine (nvram, nvdata, protect*, persist, proinfo …) hiçbir adımda
yazılmaz, bu iş yalnızca MMIO yazmaçlarına dokunur.

1. **Yalnız okuma sondası.** `PWR_STATUS` (`0x10006160`) bit31, `VDE_PWR_CON` (`0x10006300`), `CK_CFG_12`
   (`0x100000E0`), `INFRA_TOPAXI_PROTECTEN_MM_STA1` (`0x100012EC`), `SMI_COMMON_SMI_CLAMP` (`0x140193C0`) değerlerini
   logla. VDE kapalıyken `0x1600xxxx` okumak veri yolunu kilitleyebilir: **güç kapalıyken VDEC bölgesine dokunma.**
   Risk: düşük.
2. **Saat + güç açma.** `vdec_sel` → `univpll_d2_d2` (312 MHz, indeks 3) veya daha düşük; `CK_CFG_12_CLR` ile
   bit 19:16 ve bit23'ü temizle, `CK_CFG_12_SET` ile 3<<16 yaz, `0x10000008` bit19 ile güncelle. Sonra §2.2'deki VDE
   açma sırası, ardından `0x16000000` ← 1 ve `0x16000008` ← 1 (CG'ler). Doğrulama: `VDEC_SYS+0` okunuyor, kilitlenme
   yok. Risk: orta (yanlış sıra veri yolunu kilitleyip yeniden başlatır; kalıcı hasar beklenmez). Bekleme
   döngülerine zaman aşımı koy.
3. **Kesme hattı.** GIC_SPI 329'u XNU'da bağla; `0x160250A4` ile temizle. Risk: düşük.
4. **larb2 / M4U.** İlk deneme MMU'suz: `0x16010380 + 4*port` bit0 = 0, tamponlar fiziksel bitişik. M4U şu an ekran
   için açık mı, ayarı ne, kontrol edilmeli (M4U0 ekranla paylaşılıyor; yanlış yazı ekranı bozar). Risk: orta.
5. **Yazmaç dizisini elde etme (asıl iş).** Stok MIUI vendor imajından `vpud` + kodek kütüphanelerini (ağdan,
   cihaza dokunmadan) inceleme; gerekirse sonra Android'de GCE ioctl izi. Tek bir 360p I-karesinin yazmaç izi hedef.
   Risk: süre (haftalar), kapalı kodun lisansı (yalnızca davranış öğrenilmeli, kod kopyalanmamalı).
6. **Tek kare çözme testi** (çekirdek içi, sabit bit akışı): MT21 çıktısını framebuffer'a dökerek gözle doğrula.
7. **MT21 → NV12** işlemcide döşeme açma; sonra gerekirse GPU.
8. **IOKit kullanıcı istemcisi** (güç kilidi, yazmaç listesi gönderme, kesme bekleme, IOSurface → fiziksel adres).
9. **VideoToolbox eklentisi:** önce `AV1SW.videodecoder`'ı disassemble edip vtable'ı doğrula; sonra H.264 eklentisini
   mediaserverd'ye yükle, `com.hakios.open.video` MP4 ile test et, sonra Safari/YouTube.
10. **Sonra:** DVFS (416/624 MHz yalnızca vcore ayarı doğrulandıktan sonra), VP9/HEVC, UFO, MDP ile dönüştürme.

En büyük risk 5. adım: H.264 yazmaç dizisi için açık kaynak yok. 1–4 arası adımlar ve iOS eklenti iskeleti (9'un
ilk yarısı) bundan bağımsız olarak hemen yapılabilir.
