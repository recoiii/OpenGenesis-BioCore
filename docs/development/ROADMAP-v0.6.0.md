# OpenGenesis-BioCore v0.6.0 — Kabul Edilen Yol Haritası

Tarih: 28 Eylül 2026  
Tema: **Cohort Analysis Workspace — kohort tanımlama, karşılaştırma ve izlenebilir analiz**  
İterasyonlar: **089–098**  
Durum: 28 Eylül 2026 tarihinde Recep Çelik tarafından yol haritası kabul edilmiştir (“kabul edildi”). Uygulama başlatılmamıştır. Bu kayıt, iterasyon veya release için bağımsız Gemini/Claude ACCEPT kaydı değildir.

## 1. Amaç ve sürümün yeri

v0.3 çoklu örnek matrisi, varyant karşılaştırması, association ve anotasyon yeteneklerini; v0.4 workflow/DAG ve recovery altyapısını; v0.5 proje, örnek ve toplu analiz çalışma alanını sağlamayı hedefleyen gelişim çizgisidir. v0.6 önerisi bu katmanlar arasında araştırmacının kullanabileceği kalıcı bir kohort analizi akışı kurar.

Kullanıcı proje örneklerinden bir kohort oluşturur; vaka/kontrol gruplarını açıkça tanımlar; kullanılacak run/attempt ve çıktıları seçer; veri uyumluluğu ve dışlama nedenlerini görür; mevcut matris ve association servislerini çalıştırır; sonuçları filtreler ve aynı analiz kimliğine bağlı rapor üretir.

Bu sürümün değeri yeni istatistiksel yöntem sayısından çok, mevcut bilimsel yeteneklerin proje içindeki örnek kimlikleriyle güvenilir biçimde kullanılabilmesidir. Örnek metadata'sındaki sonraki değişiklikler geçmiş analizin grup üyeliğini veya sonucunu değiştirmez.

Hazırlık öncesindeki kayıtlarda kesinleşmiş v0.6 teması bulunmamıştır; bu belgede önerilen tema 28 Eylül 2026 tarihinde proje sahibi tarafından kabul edilmiştir. v0.5 yol haritası doğrudan okunmuştur; tamamlanma kapsamı önceki çalışma kayıtlarından alınmıştır. Bu hazırlıkta güncel depo kaynakları ve release referansları doğrulanmamıştır. Aşağıdaki entegrasyonlar uygulanmış veya mevcut kodda eksikliği kesinleşmiş özellikler olarak sunulmaz; 089'da kaynak eşleştirmesiyle netleştirilir.

## 2. Başlangıç ve korunacak mimari

- Başlangıç, yayımlanmış v0.5.0 ile ilişkili nihai kabul edilmiş kaynak kimliğidir. 089 öncesinde tag, frozen ref, commit/tree ve kaynak arşivinin SHA-256 değeri doğrulanır. İsimden hareketle `accepted/iteration-088` var kabul edilmez; önceki candidate commit'i nihai baseline yerine konmaz.
- Önerilen dal `v0.6.0-dev`; belge dal oluşturmaz, release veya varsayılan dal ayarını değiştirmez.
- C++20, Drogon, SQLite, yerel tarayıcı arayüzü ve Windows/Linux korunur. Yeni scheduler, DAG, genotipleme, matris veya association motoru yazılmaz.
- v0.5'te bulunan sonuç özeti, rapor ve matris bağlantıları yeniden yapılmaz. Yeni kapsam; kalıcı kohort tanımı, sabit analiz üyeliği, gruplama, örnek kimliği eşleştirme ve kohort düzeyinde analiz yaşam döngüsüdür.
- Her analizde örnek kimlikleri, grup etiketleri, seçilen artifact/run/attempt, referans kimliği, parametreler, yöntem sürümü ve dışlama kararları sabitlenir. “En son başarılı sonuç” çalıştırma sırasında yeniden çözümlenmez.
- Kohort analizi yalnız tamamlanmış ve doğrulanmış mevcut çıktıları tüketir. Çalışan batch'in arkasına dinamik DAG fan-in eklenmez. Yeni çıktılar ancak yeni analiz snapshot'ına alınabilir.
- Proje sınırları korunur. v0.6'da projeler arası kohort, otomatik örnek birleştirme ve aynı biyolojik bireye ait kayıtları tahminle çözme yoktur.
- Şema/protokol numarası kaynak incelemesi sonrası belirlenir. Migration gerekiyorsa eski veritabanının kopyası üzerinde ileri geçiş, kesinti ve tutarlılık doğrulanır; desteklenmeyen downgrade vaat edilmez.

## 3. İterasyonlar

| İterasyon | Kapsam | Somut kabul ölçütü |
|---|---|---|
| **089 — Scope, Baseline & Cohort Contracts** | Kaynak envanteri, v0.5/v0.3 servis eşleştirmesi, kohort ve analiz sözleşmeleri, API/UI taslağı, test fixture'ları | Mevcut/yeniden kullanılacak/yeni davranışlar dosya ve sembol düzeyinde eşleştirilir. Desteklenen girdi ve istatistik yöntemleri kesin listelenir. Baseline kimliği, kaynak limitleri ve gerekiyorsa migration planı kaydedilir. |
| **090 — Cohort Registry & Group Definitions** | Projeye bağlı kohort tanımı, sürümlü üyelik, vaka/kontrol etiketleri ve açık dışlamalar | Yinelenen örnek, olmayan örnek, yanlış proje ve çelişkili gruplar reddedilir. Eksik grup sessizce kontrol sayılmaz. Metadata değişikliği geçmiş üyelik snapshot'ını değiştirmez. Kayıtlar atomiktir. |
| **091 — Artifact Selection & Compatibility Gate** | Örnek → run/attempt → artifact bağlama; referans, varyant gösterimi ve örnek eşleştirme doğrulaması | Her örnek için seçilen sonuç görünürdür. Çok örnekli VCF sütunları açık ve tekil eşlenir. Referansı belirsiz veya uyumsuz girdiler sessizce birleştirilmez. Değişmiş/kayıp dosya yürütmeyi engeller. |
| **092 — Cohort Matrix Integration** | Sabitlenmiş girdilerden mevcut matris servisinin çağrılması; kimlik, missingness ve çıktı bağlantıları | Girdi sırası değişse de kanonik örnek/varyant eşleştirmesi korunur. Bir varyantın dosyada bulunmaması otomatik 0/0 değildir. No-call, hom-ref ve mevcut motorun desteklemediği kayıtlar ayrılır. Bilinen fixture matrisiyle sonuç doğrulanır. |
| **093 — Cohort QC & Inclusion Decisions** | Mevcut metriklerle örnek/varyant özeti; analiz öncesi dahil etme/dışlama önizlemesi | Eksik metrik sıfır gösterilmez; metrik tanımı ve paydası bellidir. Filtreler sürümlü ve görünürdür. Kullanıcı onayından sonra analiz üyeliği sabit kalır. Grup başına kalan sayılar ve dışlama gerekçeleri raporlanır. |
| **094 — Case/Control Analysis Integration** | Mevcut association motoruna snapshot üzerinden grup ve matris aktarımı | Vaka/kontrol yönü, test edilen allel/model, no-call politikası ve etkin örnek sayısı açıkça gösterilir. Desteklenen test, etki ölçüsü ve çoklu test davranışı 089 sözleşmesine uyar. Sıfır hücre, boş grup ve hesaplanamayan sonuçlar sessiz düzeltmeyle gizlenmez. |
| **095 — Cohort Execution, Recovery & Lineage** | Mevcut job/workflow altyapısıyla kalıcı analiz durumu, iptal, yeniden başlatma, retry ve attempt geçmişi | Duplicate submit tek mantıksal analiz üretir. Kesinti sonrası sonuç yanlışlıkla tamamlandı sayılmaz. Retry önceki attempt'e bağlanır; başarılı çıktı üzerine yazılmaz. Değişmiş girdiyle eski checkpoint kullanılmaz. |
| **096 — Cohort Results Explorer & Reports** | Sayfalı sonuç tablosu, ortak/özgül varyant görünümleri, mevcut anotasyon bağlantıları ve kohort raporu | UI/CSV veya TSV/JSON çıktıları aynı snapshot ve filtre tanımını taşır. Görüntü filtresi istatistiksel test evrenini değiştirmez. Paydalar, dışlamalar, yöntem ve referans görünürdür. Büyük tablo bütünü tarayıcıya yüklenmez. |
| **097 — Integrated Workspace & E2E** | Proje içinde kohort akışının tamamlanması, rehber ve sentetik örnek proje | Örnek seçimi → gruplama → uyumluluk → matris/QC → analiz → recovery/retry → rapor akışı UI'dan tamamlanır. Eski projeler ve tek örnek/batch akışları çalışır. Küçük, sonucu bilinen veri ile E2E doğrulanır. |
| **098 — Hardening & Release Closure** | Regresyon, native Windows/Linux, kurulum/paketleme, belgeler ve bağımsız nihai inceleme | Yeni özellik eklenmez. Aynı kaynak kimliğine bağlı platform testleri, migration/recovery E2E ve paket smoke kanıtı tamamlanır. Gemini ve Claude ACCEPT sonuçları tam nihai adaya bağlanır. |

Bağımlılık sırası: 089 sözleşmeler → 090–091 kimlik ve girdi → 092–094 bilimsel servis entegrasyonu → 095 yürütme sağlamlaştırma → 096 sonuçlar → 097 E2E → 098 kapanış. İş durumları, idempotency ve UI/API sözleşmeleri 089'da tanımlanır; 095'te ilk kez tasarlanmaz. 092–094 mevcut yürütme yolunu kullanır; ayrı geçici yürütme motoru kurulmaz.

## 4. Bilimsel doğruluk sınırları

1. **Eksik çağrı referans genotipi değildir.** VCF'te kaydın yokluğu için çağrılabilirlik kanıtı olmadan genotip türetilmez. Mevcut motorun desteklemediği temsil veya ploidy açıkça reddedilir ya da kayıtlı dışlama nedeni olur.
2. **Ortak referans ve temsil zorunludur.** Contig adını değiştirmek tek başına genom derlemesi uyumunu kanıtlamaz. Otomatik liftover yoktur; referans kimliği ve normalizasyon sözleşmesi açık olmalıdır.
3. **Analiz birimi nettir.** Aynı örneğin iki attempt'i iki birey sayılamaz. Teknik tekrarlar sessizce birleştirilmez; bağımsız biyolojik örnek eşleştirmesi kullanıcının açık metadata'sına dayanır.
4. **Desteklenen yöntemler korunur.** v0.6 yeni kovaryat düzeltmesi, popülasyon yapısı/akrabalık modeli veya yeni istatistiksel yöntem geliştirme sürümü değildir. Bu yetenekler yoksa rapor bunlar yapılmış gibi sunulmaz.
5. **Test evreni sabitlenir.** Mevcut çoklu test düzeltmesi varsa yöntem ve test ailesi kaydedilir; yoksa p değerleri düzeltilmemiş olarak işaretlenir. Sonuç tablosunda filtreleme yeniden anlamlılık hesabı yapmaz.
6. **Rapor araştırma çıktısıdır.** Otomatik klinik tanı, nedensellik veya ACMG sınıflandırması üretilmez. HTML/tablolar kullanıcı girdilerini güvenli işler; spreadsheet formül enjeksiyonu ve HTML enjeksiyonu kontrol edilir.

## 5. Kapsam dışı

RNA-seq/miRNA diferansiyel ifade, GO/KEGG/GSEA; yeni SV/CNV veya somatik calling; yeni GWAS/regresyon/soy bileşeni modelleri; klinik yorumlama; dinamik DAG fan-out/fan-in; otomatik veri indirme; tam taşınabilir arşiv/replay; bulut/dağıtık yürütme; çok kullanıcılı yetkilendirme; plugin marketplace.

RNA-seq/miRNA ve zenginleştirme ayrı bir sonraki sürüm adayı olabilir; bu belge v0.7 kapsamını dondurmaz. GitHub görünürlük/organik büyüme çalışması önceki karar doğrultusunda v1.0 aşamasında kalır.

## 6. Kanıt ve bağımsız kabul düzeni

Roadmap uygulama öncesinde Gemini kapsam/tutarlılık incelemesine sunulur. Her iterasyon bir önceki kabul edilmiş frozen baseline'dan ilerler. Candidate hazırlanması kabul değildir.

Her iterasyon teslimi:

- Exact candidate kaynak ZIP'i; commit/tree, baseline ve kaynak SHA-256 kimliği.
- Tam dört Markdown inceleme parçası: **01 kapsam/baseline/sözleşmeler**, **02 uygulama farkı ve değişen kaynaklar**, **03 testler/regresyon/hata senaryoları**, **04 kanıtlar/sınırlar/açık bulgular/verdict talebi**. Değişen dosyalar ve gerekli bağlam eksiksiz kapsanır; yalnız özet bağımsız kod incelemesi yerine geçmez.
- Kaynak arşivi ve dört parçanın gerçek SHA-256 değerlerini içeren `SHA256SUMS.txt`; hashler üretilmeden yazılmaz.
- Aynı commit'e ait CI sonuçları; uygulanmış testler ile planlanmış testler ayrılır. Test sayısı veya ACCEPT yüzdesi önceden vaat edilmez.

**Gemini ACCEPT gelmeden freeze ve sonraki iterasyona geçiş yoktur.** Bulgular yeni candidate ile kapatılır; değişmiş kaynak eski kabulü devralmaz. Kabul edilmiş referanslar yerinde değiştirilmez. Son kapanışta **Gemini + Claude ACCEPT** aynı nihai adaya bağlanır; düzeltme kaynak kimliğini değiştirirse etkilenen kanıt ve inceleme yenilenir. Release kimliğine etki eden değişiklikler son inceleme öncesinde tamamlanır.

Önerilen doğrulama matrisi: Linux GCC Debug/Release, Clang Debug ve ASan+UBSan; native Windows MSVC Debug/Release; çıkarılmış dağıtım paketinden kurulum/açılış smoke; eski proje migration; durdurma/recovery/retry. Kesin işler ve mevcut test tabanı 089'da depoyla eşleştirilir. Windows kanıtı yalnız Linux geçişiyle tamamlanmış sayılmaz.

## 7. Zorunlu test senaryoları ve ölçek

Sentetik fixture'lar: yinelenen örnek; başında sıfır/Unicode içeren kimlik; aynı örnekte iki attempt; çok örnekli VCF eşleştirme hatası; eksik grup; boş kohort; tüm örnekleri dışlayan filtre; hom-ref/no-call/absent ayrımı; uyumsuz/belirsiz referans; desteklenmeyen ploidy; sıfır hücreli tablo; bozulmuş artifact; duplicate submit; kesinti; iptal; disk dolması; migration hatası; eski raporun metadata düzenlemesinden etkilenmemesi.

Bilimsel beklenen değerler aynı üretim işlevinden türetilmez; küçük elle denetlenebilir matris ve çapraz tablolar kullanılır. Regresyonlar mevcut v0.3 sonuç semantiğini ve v0.4/v0.5 yürütme davranışlarını korur. Gerçek hasta verisi CI veya inceleme paketlerine konmaz.

089'da kayıtlı donanımda örnek sayısı, varyant sayısı ve doluluk oranı ayrı ölçülür; bellek, süre, çıktı boyutu ve UI yanıtı için somut bütçeler belirlenir. Büyük veride sayfalama/akış ve iptal davranışı doğrulanır. Ölçülmemiş “milyonlarca varyant” veya belirli RAM'de sınırsız çalışma vaadi verilmez. Limit aşımı açıklayıcı hata üretmeli, yarım sonuç başarılı gösterilmemelidir.

## 8. Tamamlanma tanımı

v0.6.0 sonunda araştırmacı aynı proje içinde sabitlenmiş bir kohort ve vaka/kontrol analizi oluşturabilmeli; her sayının hangi örnek, sonuç, referans ve parametreden geldiğini izleyebilmelidir. Önceden tamamlanmış analizler örnek düzenlemesinden etkilenmemeli; kesinti ve retry geçmişi korunmalıdır. Eski projeler açılmalı; aynı kaynak kimliği platform/paket kanıtlarından ve nihai bağımsız incelemelerden geçmelidir.

10 iterasyon hedefi bir takvim taahhüdü değildir. Kaynak incelemesinde temel servis eksikliği veya kapsam büyümesi ortaya çıkarsa bu, ilgili iterasyona gizlice eklenmez; gerekçeli roadmap revizyonuyla yeniden değerlendirilir.

Proje sahibi ve geliştirici: **Recep Çelik**. Bu yol haritasındaki **ChatGPT katkısı**: AI destekli kapsam tasarımı, mimari değerlendirme, iterasyon planlama ve kabul ölçütlerinin hazırlanması. Gemini ve Claude incelemeleri gelecekteki kabul adımlarıdır; bu belge bunların yapıldığını iddia etmez.
