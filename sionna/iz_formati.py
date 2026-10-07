"""
iz_formati.py
Sionna RT tarafinda uretilen kanal izlerini ns-3 tarafindaki sionna-iz.h
okuyucusunun bekledigi CSV formatinda yazar.
"""

import math


KAPSAMA_YOK_DB = -999.0


def _temiz_deger(v):
    """CSV'ye yazilacak sayisal degeri sade string'e cevirir."""
    return f"{float(v):.9g}"


def ustbilgi_yaz(f, sahne, frekans_hz, ornekleme_s, anten, isin_ayarlari, vericiler):
    # sionna-iz.h '#' ile baslayan satirlari ust bilgi olarak okuyor.
    f.write("# surum=sionna-iz-v1\n")
    f.write(f"# sahne={sahne}\n")
    f.write(f"# frekans_hz={_temiz_deger(frekans_hz)}\n")
    f.write(f"# ornekleme_s={_temiz_deger(ornekleme_s)}\n")
    f.write(f"# anten={anten}\n")

    for anahtar, deger in isin_ayarlari.items():
        f.write(f"# isin_{anahtar}={deger}\n")

    # sionna-iz.h bunu ozel olarak:
    # # tx,gnb0,x,y,z
    # biciminde okuyup gNB konum dogrulamasinda kullaniyor.
    for tx_id, p in vericiler.items():
        f.write(
            f"# tx,{tx_id},{_temiz_deger(p[0])},{_temiz_deger(p[1])},{_temiz_deger(p[2])}\n"
        )

    # sionna-iz.h icin zorunlu alanlar:
    # zaman_s, tx_id, rx_id, yol_kazanci_db
    # Diger sutunlar teshis/analiz icin tutuluyor.
    f.write(
        "zaman_s,tx_id,rx_id,rx_x,rx_y,rx_z,mesafe_m,yol_sayisi,yol_kazanci_db\n"
    )


def satir_yaz(
    f,
    zaman_s,
    tx_id,
    rx_id,
    rx_konum,
    mesafe_m,
    yol_sayisi,
    yol_kazanci_db,
):
    if not math.isfinite(float(yol_kazanci_db)):
        kazanc = KAPSAMA_YOK_DB
    else:
        kazanc = float(yol_kazanci_db)

    f.write(
        ",".join(
            [
                _temiz_deger(zaman_s),
                str(tx_id),
                str(rx_id),
                _temiz_deger(rx_konum[0]),
                _temiz_deger(rx_konum[1]),
                _temiz_deger(rx_konum[2]),
                _temiz_deger(mesafe_m),
                str(int(yol_sayisi)),
                _temiz_deger(kazanc),
            ]
        )
        + "\n"
    )
