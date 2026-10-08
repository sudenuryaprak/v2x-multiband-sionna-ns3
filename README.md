# V2X Multiband Resource Allocation with ns-3, 5G-LENA and Sionna RT

Bu repo, otonom araçlar için V2X haberleşmesinde **FR1 ve FR2 çok bantlı kaynak tahsisini** inceleyen bir bitirme projesidir.

Kullanılan ana bileşenler:

- ns-3 3.48
- 5G-LENA v5.1
- Sionna RT
- A3 tabanlı handover
- FR1 / FR2 BWP kullanımı
- SENSOR, CONTROL ve MAP trafik sınıfları

> Repo içinde önceden üretilmiş FR1 ve FR2 Sionna izleri bulunduğu için, yalnızca ns-3 simülasyonlarını çalıştırmak isteyen bir kullanıcının Sionna RT'yi ayrıca kurması zorunlu değildir.

---

## 1. Test edilen ortam

- Ubuntu / WSL2
- ns-3: **3.48**
- 5G-LENA: **v5.1**
- Python: **3.12**
- Sionna RT
- CMake + Ninja
- C++ derleyicisi

Ana ns-3 çalışma klasörü örneği:

```text
~/ns-3-dev-v51
```

---

## 2. Repo yapısı

```text
v2x-multiband-sionna-ns3/
├── ns3/
│   ├── arac-munih-baseline-fr1-duzeltilmis.cc
│   ├── arac-munih-static-loglu-fix.cc
│   ├── arac-munih-dinamik-sensor-histerezis.cc
│   ├── sionna-iz.h
│   ├── iz_munih.csv
│   └── iz_munih_fr2.csv
├── sionna/
│   ├── munih_iz_uret.py
│   ├── munih_iz_uret_fr2.py
│   ├── iz_formati.py
│   └── munih_yorunge.npz
├── results/
│   ├── sonuc-baseline-fr1.txt
│   ├── sonuc-statik.txt
│   └── sonuc-dinamik.txt
├── README.md
└── .gitignore
```

---

## 3. Senaryo özeti

Senaryoda:

- 4 gNB
- 6 araç
- Münih tabanlı araç hareketleri
- A3 tabanlı otomatik handover
- FR1: **3.5 GHz / 20 MHz**
- FR2: **28 GHz / 100 MHz**

kullanılmaktadır.

### Trafik tipleri

| Trafik | Yön | Açıklama |
|---|---|---|
| SENSOR | UL | Araçtan uzak sunucuya yoğun sensör verisi |
| CONTROL | DL | Küçük ve gecikmeye duyarlı kontrol trafiği |
| MAP | DL | Harita / orta yoğunluklu veri |

---

## 4. Çalışan referans senaryolar

### 4.1 Tek Bant FR1 Baseline

Dosya:

```text
ns3/arac-munih-baseline-fr1-duzeltilmis.cc
```

Tüm trafik FR1 üzerindedir:

```text
SENSOR  -> FR1 / BWP0
CONTROL -> FR1 / BWP0
MAP     -> FR1 / BWP0
```

Bu senaryo karşılaştırmalar için baseline olarak kullanılmaktadır.

### 4.2 Statik Çift Bant

Dosya:

```text
ns3/arac-munih-static-loglu-fix.cc
```

Statik eşleme:

```text
CONTROL -> FR1 / BWP0
SENSOR  -> FR2 / BWP1
MAP     -> FR2 / BWP1
```

FR1 ve FR2 için senkron TDD deseni:

```text
DL|S|UL|UL|DL|DL|S|UL|UL|DL|
```

### 4.3 Dinamik Çift Bant + Histerezis

Dosya:

```text
ns3/arac-munih-dinamik-sensor-histerezis.cc
```

Eşleme:

```text
CONTROL -> FR1
MAP     -> FR2
SENSOR  -> dinamik FR1 / FR2
```

SENSOR için histerezis:

```text
FR2'deyken:
FR2 gain < -125 dB  -> FR1'e geç

FR1'deyken:
FR2 gain > -115 dB  -> FR2'ye geç

-125 dB ile -115 dB arasında:
mevcut bantta kal
```

---

## 5. Handover

Handover algoritması sıfırdan yazılmamıştır.

Projede **5G-LENA'nın A3 tabanlı handover mekanizması** kullanılmaktadır.

Basit mantık:

```text
Araç mevcut gNB'ye bağlı
        ↓
Komşu gNB daha iyi hale gelir
        ↓
A3 koşulu sağlanır
        ↓
Handover başlar
        ↓
Araç yeni gNB'ye bağlanır
```

Dinamik bant kararında kalıcı RNTI eşlemesine bağımlı olunmaz. Güncel bağlı hücre bilgisi takip edilir.

---

# 6. Kurulum

## 6.1 Temel paketler

```bash
sudo apt update
sudo apt install -y git build-essential cmake ninja-build python3 python3-venv pkg-config
```

## 6.2 ns-3 3.48

```bash
cd ~
git clone --branch ns-3.48 https://gitlab.com/nsnam/ns-3-dev.git ns-3-dev-v51
cd ~/ns-3-dev-v51
git describe --tags --always
```

## 6.3 5G-LENA v5.1

```bash
cd ~/ns-3-dev-v51
git clone https://gitlab.com/cttc-lena/nr.git contrib/nr
git -C contrib/nr checkout v5.1
git -C contrib/nr describe --tags --always
```

Beklenen:

```text
v5.1
```

## 6.4 Derleme

```bash
cd ~/ns-3-dev-v51
./ns3 configure
./ns3 build -j 2
```

---

# 7. Projeyi indirme

```bash
cd ~
git clone https://github.com/sudenuryaprak/v2x-multiband-sionna-ns3.git
```

---

# 8. Dosyaları ns-3 scratch klasörüne kopyalama

```bash
cd ~/ns-3-dev-v51

cp ~/v2x-multiband-sionna-ns3/ns3/*.cc scratch/
cp ~/v2x-multiband-sionna-ns3/ns3/sionna-iz.h scratch/
cp ~/v2x-multiband-sionna-ns3/ns3/iz_munih.csv scratch/
cp ~/v2x-multiband-sionna-ns3/ns3/iz_munih_fr2.csv scratch/
```

Kontrol:

```bash
ls -lh scratch/arac-munih*.cc
ls -lh scratch/iz_munih*.csv
ls -lh scratch/sionna-iz.h
```

---

# 9. Projeyi derleme

```bash
cd ~/ns-3-dev-v51
./ns3 build -j 2
```

---

# 10. Simülasyonları çalıştırma

## Tek Bant FR1 Baseline

```bash
cd ~/ns-3-dev-v51

./ns3 run "scratch/arac-munih-baseline-fr1-duzeltilmis --sionna=1 --log=4" \
2>&1 | tee sonuc-baseline-fr1.txt
```

## Statik Çift Bant

```bash
./ns3 run "scratch/arac-munih-static-loglu-fix --sionna=1 --log=4" \
2>&1 | tee sonuc-statik.txt
```

## Dinamik Çift Bant + Histerezis

```bash
./ns3 run "scratch/arac-munih-dinamik-sensor-histerezis --sionna=1 --log=4" \
2>&1 | tee sonuc-dinamik.txt
```

---

# 11. Çalışmayı kontrol etme

Handover:

```bash
grep "HANDOVER" sonuc-dinamik.txt
```

BWP kararları:

```bash
grep "BWP" sonuc-dinamik.txt
```

Fatal hata kontrolü:

```bash
grep -E "NS_FATAL|NS_ASSERT|SIGABRT|overlap|Cannot" sonuc-dinamik.txt
```

---

# 12. Örnek sonuçlar

Bu değerler tek bir deterministik simülasyon koşusundandır.

## SENSOR toplam throughput

```text
Tek bant FR1      : 6.96 Mbps
Statik çift bant  : 4.10 Mbps
Dinamik çift bant : 4.87 Mbps
```

## SENSOR ortalama paket kaybı

```text
Tek bant FR1      : yaklaşık %21.34
Statik çift bant  : yaklaşık %53.64
Dinamik çift bant : yaklaşık %44.97
```

## Handover

```text
Tek bant FR1      : 5
Statik çift bant  : 7
Dinamik çift bant : 7
```

Dinamik senaryoda toplam **12 SENSOR BWP değişimi** gözlemlenmiştir.

> Bunlar istatistiksel anlamlılık iddiası taşımaz. Final çalışma için çoklu seed / tekrar koşuları önerilir.

---

# 13. Sionna RT izleri

Hazır izler:

```text
ns3/iz_munih.csv
ns3/iz_munih_fr2.csv
```

Her bant için:

```text
6 araç × 101 zaman örneği × 4 gNB = 2424 satır
```

bulunmaktadır.

Hazır CSV'ler kullanıldığında Sionna RT kurulumu zorunlu değildir.

---

# 14. Sionna izlerini yeniden üretme

Sionna RT yalnızca izleri yeniden üretmek için gereklidir.

Örnek sanal ortam:

```bash
python3 -m venv ~/sionna-env
source ~/sionna-env/bin/activate
```

İz üretimi:

```bash
cd ~/v2x-multiband-sionna-ns3/sionna
python munih_iz_uret.py
python munih_iz_uret_fr2.py
```

Yeni izler ns-3'ün beklediği isimlerle `scratch/` klasörüne kopyalanmalıdır:

```text
iz_munih.csv
iz_munih_fr2.csv
```

> Sionna/TensorFlow/Dr.Jit/LLVM sürümleri sistem ve donanıma göre değişebildiği için bu repoda henüz sabit bir `requirements.txt` bulunmamaktadır.

---

# 15. Bilinen deneysel sorun

Daha sonra denenen bazı FR1-vs-FR2 karşılaştırmalı runtime BWP sürümlerinde handover sırasında:

```text
Cannot RX SRS while receiving DATA.
```

ve bazı testlerde:

```text
Cannot TX while RX.
```

hataları görülmüştür.

Bu nedenle repo içindeki ana dinamik referans:

```text
arac-munih-dinamik-sensor-histerezis.cc
```

dosyasıdır.

---

# 16. Projenin temel karşılaştırması

```text
1. Tek Bant FR1 Baseline
2. Statik Çift Bant
3. Dinamik Çift Bant + Histerezis
```

İncelenen KPI'lar:

- Throughput
- Ortalama gecikme
- Paket kaybı
- Handover sayısı
- BWP değişim sayısı

Planlanan sonraki aşamalar:

- p95 / p99 gecikme
- Deadline violation
- Başarı oranı
- FR1 / FR2 kullanım oranı
- Çoklu seed
- Daha gelişmiş channel-aware seçim
- AI tabanlı kaynak tahsisi

---

# 17. Git ile güncelleme

```bash
git add .
git commit -m "Degisiklik aciklamasi"
git push
```

---

## Proje durumu

- [x] FR1 Sionna izi
- [x] FR2 Sionna izi
- [x] Tek bant FR1 baseline
- [x] Statik çift bant
- [x] Senkron TDD
- [x] A3 handover
- [x] Dinamik SENSOR FR1 / FR2 seçimi
- [x] Histerezisli kararlı dinamik koşu
- [ ] FR1-vs-FR2 karşılaştırmalı yeni dinamik algoritma
- [ ] Çoklu seed / istatistiksel analiz
- [ ] AI tabanlı kaynak tahsisi

---

## Repository

```text
https://github.com/sudenuryaprak/v2x-multiband-sionna-ns3
```
