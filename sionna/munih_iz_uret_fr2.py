import time
import numpy as np
import sionna.rt
from sionna.rt import load_scene, PlanarArray, Transmitter, Receiver, PathSolver

import iz_formati

# ==================== AYARLAR ====================
SAHNE = "munich"
FREKANS = 28e9          # 3.5e9 -> 28e9
ANTEN = "iso1x1"

GIRDI_NPZ = "munih_yorunge.npz"
CIKTI_CSV = "iz_munih_fr2.csv"   # iz_munih.csv -> iz_munih_fr2.csv

ISIN_AYARLARI = dict(
    max_depth=3,
    los=True,
    specular_reflection=True,
    diffuse_reflection=False,
    refraction=True,
    diffraction = True,
    synthetic_array=True,
    seed=41,
)

# ==================== M.1 CIKTISINI YUKLE ====================
g = np.load(GIRDI_NPZ , allow_pickle =False)
arac_adlari = g["arac_adlari"]
konumlar = g["konumlar"]
zamanlar = g["zamanlar"]
gnb_adlari = g["gnb_adlari"]
gnb_konumlari = g["gnb_konumlari"]

ARABA_SAYISI = konumlar.shape[0]
N = konumlar.shape[1] #adım sayısı
VERICILER = {ad: gnb_konumlari[i].tolist() for i , ad in enumerate(gnb_adlari)}

print(f"{ARABA_SAYISI} arac x {N} zaman adimi = {ARABA_SAYISI*N} alici")
print(f"{len(VERICILER)} verici  ->  {ARABA_SAYISI*N*len(VERICILER)} baglanti-olcumu")

# ==================== SAHNE ====================
scene = load_scene(getattr(sionna.rt.scene , SAHNE) , merge_shapes=False)
scene.frequency = FREKANS
scene.tx_array = PlanarArray(num_rows=1 , num_cols=1 , pattern = "iso" , polarization = "V")
scene.rx_array = PlanarArray(num_rows=1 , num_cols=1 , pattern= "iso" , polarization = "V")
#anten dizilerini ns3 tarafında 4x4 gibi tanımlayacağız burada tekli yaptık.

# ==================== YORUNGELERI SOZLUGE CEVIR ====================
yorungeler = {}
for i , araba_id in enumerate(arac_adlari):
    yorungeler[araba_id] = konumlar[i]
    assert yorungeler[araba_id].shape == (N,3) ,"yorunge sekli yanlis!"

# ==================== CIHAZLARI YERLESTIR ====================
for tx_id , p in VERICILER.items():
    scene.add(Transmitter(name = tx_id , position = p))

rx_adlari = []
rx_kimlik = []

for araba_id , yol in yorungeler.items():
    for k, p in enumerate(yol):
        ad = f"{araba_id}=t{k:03d}"
        scene.add(Receiver(name = ad , position = [float(p[0]) , float(p[1]) ,float(p[2])]))
        rx_adlari.append(ad)
        rx_kimlik.append((araba_id , k))

# ---- SIRA KONTROLU ----
assert list(scene.receivers.keys()) == rx_adlari, "ALICI SIRASI UYUSMUYOR!"
assert list(scene.transmitters.keys()) == list(VERICILER.keys()), "VERICI SIRASI UYUSMUYOR!"
print("sira kontrolu: TAMAM")

# ==================== ISIN IZLEME ====================
p_solver = PathSolver()
t0 = time.time()
paths = p_solver(scene=scene , **ISIN_AYARLARI)
gecen = time.time() - t0

a , tau = paths.cir(normalize_delays=False , out_type="numpy")
print(f"isin izleme: {gecen:.2f} s   |  a sekli: {a.shape}")

# ==================== IZ DOSYASINI YAZ ====================
tx_listesi = list(VERICILER)
satir_sayisi = 0
kapsama_yok= 0
kazanc_min , kazanc_max = np.inf , -np.inf

with open(CIKTI_CSV , "w") as f:
    iz_formati.ustbilgi_yaz(
        f,
        sahne=SAHNE,
        frekans_hz=FREKANS,
        ornekleme_s=float(zamanlar[1] - zamanlar[0]),
        anten = ANTEN,
        isin_ayarlari=ISIN_AYARLARI,
        vericiler=VERICILER,
    )

    for rx_idx , (araba_id , k) in enumerate(rx_kimlik):
        rx_konum = yorungeler[araba_id][k]

        for tx_idx , tx_id in enumerate(tx_listesi):
            tx_konum = np.array(VERICILER[tx_id])
            mesafe = float(np.linalg.norm(rx_konum - tx_konum))

            guc = np.abs(a[rx_idx , 0 ,tx_idx , 0 , : , 0]) ** 2
            yol_sayisi = int((guc > 0).sum())
            toplam = float(guc.sum())
            kazanc = 10.0 * np.log10(toplam) if toplam > 0 else -np.inf

            iz_formati.satir_yaz(
                f,
                zaman_s=float(zamanlar[k]),
                tx_id=tx_id,
                rx_id=araba_id,
                rx_konum=rx_konum,
                mesafe_m=mesafe,
                yol_sayisi=yol_sayisi,
                yol_kazanci_db=kazanc,
            )

            satir_sayisi += 1
            if yol_sayisi == 0:
                kapsama_yok += 1
            else:
                kazanc_min = min(kazanc_min, kazanc)
                kazanc_max = max(kazanc_max, kazanc)

print(f"\n{CIKTI_CSV} yazildi")
print(f"  satir sayisi  : {satir_sayisi}")
print(f"  kapsama yok   : {kapsama_yok}")
print(f"  kazanc araligi: {kazanc_min:.2f} .. {kazanc_max:.2f} dB")






