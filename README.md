# V2X Multiband Resource Allocation with ns-3, 5G-LENA and Sionna RT

Bu proje, otonom araclar icin V2X haberlesmesinde FR1 ve FR2 bantlarini kullanan
cok bantli kaynak tahsisi senaryolarini inceler.

## Ortam

- ns-3: 3.48
- 5G-LENA: v5.1
- Sionna RT
- Ubuntu / WSL2

## Senaryolar

### 1. Tek Bant FR1 Baseline
Dosya:
`ns3/arac-munih-baseline-fr1-duzeltilmis.cc`

Tum SENSOR, CONTROL ve MAP trafigi FR1 uzerinden tasinir.

### 2. Statik Cift Bant
Dosya:
`ns3/arac-munih-static-loglu-fix.cc`

- CONTROL -> FR1
- SENSOR -> FR2
- MAP -> FR2

### 3. Dinamik Cift Bant
Dosya:
`ns3/arac-munih-dinamik-sensor-histerezis.cc`

- CONTROL -> FR1
- MAP -> FR2
- SENSOR -> kanal durumuna gore FR1 / FR2

Histerezis:
- FR2 -> FR1: FR2 gain < -125 dB
- FR1 -> FR2: FR2 gain > -115 dB

## Handover

Handover icin 5G-LENA'nin A3 tabanli handover mekanizmasi kullanilmaktadir.

## Sionna RT

FR1 ve FR2 kanal izleri Sionna RT ile uretilmistir.

- `ns3/iz_munih.csv`
- `ns3/iz_munih_fr2.csv`

Sionna iz uretim kodlari `sionna/` klasorundedir.
