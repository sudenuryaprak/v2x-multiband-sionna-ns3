// =====================================================================================
//  sionna-iz.h — Sionna iz dosyasini okuyan ve ns-3 kanal modeline baglayan siniflar
//
//  M.7 GUNCELLEMESI: cok-bant destegi. Her iz artik bir ETIKET ile ("FR1", "FR2" gibi)
//  ayri ayri saklaniyor -- ayni anda birden fazla iz bellekte durabiliyor.
//
//  Iki sinif var:
//    SionnaIz          : iz dosyalarini okur, (etiket, tx, rx, t) -> yol kazanci sorgusuna cevap verir
//    SionnaIzYolKaybi  : ns-3'un PropagationLossModel'inden turer, yukaridakine deleg eder.
//                        Her nesne kendi "Etiket" Attribute'una sahip -- hangi bandin
//                        izini kullanacagini bu belirliyor.
//
//  Header-only tasarim: butun metotlar sinif govdesinde tanimli (ortuk inline),
//  statik veri uyeleri 'inline' ile isaretli. Boylece birden cok .cc dosyasi
//  bu basligi sorunsuz include edebilir.
// =====================================================================================
#ifndef SIONNA_IZ_H
#define SIONNA_IZ_H

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/propagation-module.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{

// =====================================================================================
//  IZ DEPOSU
//  Birden fazla iz dosyasini "etiket" (bant adi) ile ayirt ederek bellekte tutar.
//  Butun uyeler STATIK: tum SionnaIzYolKaybi ornekleri (her bant icin bir tane)
//  bu tek depoyu paylasiyor, ama her biri kendi etiketine bakiyor.
// =====================================================================================
class SionnaIz
{
  public:
    static constexpr double KAPSAMA_YOK_DB = -999.0;

    // ---------------------------------------------------------------------------------
    /// Iz dosyasini okur, verilen ETIKET altina kaydeder.
    /// Ayni etiket tekrar Yukle() ile cagrilirsa eski veri ustune yazilir.
    static void Yukle(const std::string& yol, const std::string& etiket = "varsayilan")
    {
        std::ifstream dosya(yol);
        NS_ABORT_MSG_IF(!dosya.is_open(), "Iz dosyasi acilamadi: " << yol);

        std::map<std::string, int> sutunIdx;
        std::string satir;
        bool baslikOkundu = false;
        uint32_t satirSayisi = 0;

        IzVerisi& veri = m_veri[etiket];
        veri.seriler.clear();
        veri.ustbilgi.clear();
        veri.vericiKonum.clear();

        while (std::getline(dosya, satir))
        {
            if (satir.empty())
            {
                continue;
            }

            // ---- Yorum satirlari: ust bilgi ve verici tanimlari ----
            if (satir[0] == '#')
            {
                std::string icerik = satir.substr(1);
                while (!icerik.empty() && icerik[0] == ' ')
                {
                    icerik.erase(0, 1);
                }

                if (icerik.rfind("tx,", 0) == 0) // "tx,gnb0,-60.0,0.0,15.0"
                {
                    auto p = Parcala(icerik);
                    if (p.size() >= 5)
                    {
                        veri.vericiKonum[p[1]] = {std::stod(p[2]), std::stod(p[3]),
                                                  std::stod(p[4])};
                    }
                }
                else
                {
                    auto esittir = icerik.find('=');
                    if (esittir != std::string::npos)
                    {
                        veri.ustbilgi[icerik.substr(0, esittir)] = icerik.substr(esittir + 1);
                    }
                    else
                    {
                        veri.ustbilgi["surum"] = icerik;
                    }
                }
                continue;
            }

            // ---- Ilk yorum-disi satir: sutun basligi ----
            if (!baslikOkundu)
            {
                auto adlar = Parcala(satir);
                for (size_t i = 0; i < adlar.size(); ++i)
                {
                    sutunIdx[adlar[i]] = static_cast<int>(i);
                }
                for (const auto& gerekli : {"zaman_s", "tx_id", "rx_id", "yol_kazanci_db"})
                {
                    NS_ABORT_MSG_IF(sutunIdx.find(gerekli) == sutunIdx.end(),
                                    "Iz dosyasinda '" << gerekli << "' sutunu yok");
                }
                baslikOkundu = true;
                continue;
            }

            // ---- Veri satiri ----
            auto p = Parcala(satir);
            double t = std::stod(p[sutunIdx["zaman_s"]]);
            std::string tx = p[sutunIdx["tx_id"]];
            std::string rx = p[sutunIdx["rx_id"]];
            double kazanc = std::stod(p[sutunIdx["yol_kazanci_db"]]);

            veri.seriler[{tx, rx}].push_back({t, kazanc});
            ++satirSayisi;
        }

        // Zamana gore sirala -- ikili arama icin sart
        for (auto& [anahtar, seri] : veri.seriler)
        {
            std::sort(seri.begin(), seri.end());
        }

        veri.yuklendi = true;
        std::cout << "Iz yuklendi [" << etiket << "]: " << yol << "  (" << satirSayisi
                  << " satir, " << veri.seriler.size() << " baglanti, " << veri.vericiKonum.size()
                  << " verici)\n";
    }

    // ---------------------------------------------------------------------------------
    /// (etiket, verici, alici) uclusu icin t anindaki yol kazanci [dB].
    /// Ornekler arasinda dB cinsinden dogrusal ara deger hesaplar.
    static double Kazanc(const std::string& etiket, const std::string& txId,
                         const std::string& rxId, double t)
    {
        auto itVeri = m_veri.find(etiket);
        NS_ABORT_MSG_IF(itVeri == m_veri.end() || !itVeri->second.yuklendi,
                        "SionnaIz: '" << etiket << "' etiketiyle yuklenmis iz yok. "
                                      << "Once SionnaIz::Yukle(yol, \"" << etiket
                                      << "\") cagrilmali");
        const auto& seriler = itVeri->second.seriler;

        auto it = seriler.find({txId, rxId});
        if (it == seriler.end() || it->second.empty())
        {
            return KAPSAMA_YOK_DB; // bu baglanti izde yok
        }
        const auto& seri = it->second;

        if (t <= seri.front().first)
        {
            return seri.front().second;
        }
        if (t >= seri.back().first)
        {
            return seri.back().second;
        }

        auto ust = std::lower_bound(
            seri.begin(),
            seri.end(),
            std::make_pair(t, -std::numeric_limits<double>::infinity()));
        auto alt = ust - 1;

        if (alt->second <= KAPSAMA_YOK_DB + 1.0 || ust->second <= KAPSAMA_YOK_DB + 1.0)
        {
            return KAPSAMA_YOK_DB;
        }

        double oran = (t - alt->first) / (ust->first - alt->first);
        return alt->second + oran * (ust->second - alt->second);
    }

    // ---------------------------------------------------------------------------------
    static void Ozet(const std::string& etiket)
    {
        auto itVeri = m_veri.find(etiket);
        if (itVeri == m_veri.end())
        {
            std::cout << "SionnaIz::Ozet: '" << etiket << "' yuklenmemis\n";
            return;
        }
        const IzVerisi& veri = itVeri->second;

        std::cout << "\n--- [" << etiket << "] Ust bilgi ---\n";
        for (const auto& [k, v] : veri.ustbilgi)
        {
            std::cout << "  " << k << " = " << v << "\n";
        }
        std::cout << "--- Vericiler ---\n";
        for (const auto& [ad, p] : veri.vericiKonum)
        {
            std::cout << "  " << ad << " : (" << p[0] << ", " << p[1] << ", " << p[2] << ")\n";
        }
        std::cout << "--- Baglantilar ---\n";
        for (const auto& [anahtar, seri] : veri.seriler)
        {
            std::cout << "  " << anahtar.first << " -> " << anahtar.second << " : "
                      << seri.size() << " ornek,  t = " << seri.front().first << " .. "
                      << seri.back().first << " s\n";
        }
    }

    /// Ust bilgiden bir alan okur (yoksa varsayilan doner). Orn. "frekans_hz".
    static std::string UstBilgi(const std::string& etiket, const std::string& anahtar,
                                const std::string& varsayilan = "")
    {
        auto itVeri = m_veri.find(etiket);
        if (itVeri == m_veri.end())
        {
            return varsayilan;
        }
        auto it = itVeri->second.ustbilgi.find(anahtar);
        return (it == itVeri->second.ustbilgi.end()) ? varsayilan : it->second;
    }

    /// Izdeki verici konumu (dogrulama icin).
    static std::vector<double> VericiKonumu(const std::string& etiket, const std::string& txId)
    {
        auto itVeri = m_veri.find(etiket);
        if (itVeri == m_veri.end())
        {
            return {};
        }
        auto it = itVeri->second.vericiKonum.find(txId);
        return (it == itVeri->second.vericiKonum.end()) ? std::vector<double>{} : it->second;
    }

  private:
    static std::vector<std::string> Parcala(const std::string& satir)
    {
        std::vector<std::string> parcalar;
        std::stringstream akis(satir);
        std::string p;
        while (std::getline(akis, p, ','))
        {
            parcalar.push_back(p);
        }
        return parcalar;
    }

    /// Tek bir izin tum verisi -- eskiden sinifin dogrudan uyeleriydi, artik
    /// etiket basina bir tane var.
    struct IzVerisi
    {
        std::map<std::pair<std::string, std::string>, std::vector<std::pair<double, double>>>
            seriler;
        std::map<std::string, std::string> ustbilgi;
        std::map<std::string, std::vector<double>> vericiKonum;
        bool yuklendi = false;
    };

    /// etiket ("FR1", "FR2", ...) -> o bandin iz verisi
    inline static std::map<std::string, IzVerisi> m_veri;
};

// =====================================================================================
//  ns-3 KANAL MODELI SARMALAYICISI
//  Her nesne kendi "Etiket" Attribute'una sahip -- hangi bandin izini kullanacagini
//  bu belirliyor. Sayaclar/ayarlar da etikete gore ayrilmis static bir map'te
//  tutuluyor (Ptr<> almadan, dogrudan SionnaIzYolKaybi::CagriSayisi("FR1") gibi
//  cagirabilmek icin -- NrChannelHelper nesneyi bizim yerimize olusturuyor,
//  sonradan Ptr<> ile ayar yapamiyoruz).
// =====================================================================================
class SionnaIzYolKaybi : public PropagationLossModel
{
  public:
    SionnaIzYolKaybi() = default;
    ~SionnaIzYolKaybi() override = default;

    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::SionnaIzYolKaybi")
                .SetParent<PropagationLossModel>()
                .SetGroupName("Propagation")
                .AddConstructor<SionnaIzYolKaybi>()
                .AddAttribute("Etiket",
                              "Bu nesnenin hangi iz'i (bandini) kullanacagi -- "
                              "SionnaIz::Yukle()'de kullanilan etiketle ayni olmali",
                              StringValue("varsayilan"),
                              MakeStringAccessor(&SionnaIzYolKaybi::m_etiket),
                              MakeStringChecker());
        return tid;
    }

    // ---- Senaryo bunlari cagirarak ns-3 dugumu <-> iz kimligi eslemesini kurar ----
    // Bu esleme banttan bagimsiz -- bir dugumun adi (orn. "gnb0") hangi bandin
    // kanalindan sorulursa sorulsun ayni, o yuzden hala tek, paylasilan bir tablo.
    static void VericiKaydet(uint32_t nodeId, const std::string& txId)
    {
        m_vericiler[nodeId] = txId;
    }

    static void AliciKaydet(uint32_t nodeId, const std::string& rxId)
    {
        m_aliciler[nodeId] = rxId;
    }

    // ---- Teshis (artik ETIKET parametreli -- her bandin kendi sayaci) ----
    static uint64_t CagriSayisi(const std::string& etiket)
    {
        return m_durum[etiket].cagriSayisi;
    }

    static uint64_t YedekSayisi(const std::string& etiket)
    {
        return m_durum[etiket].yedekSayisi;
    }

    static void YedekFrekansAyarla(const std::string& etiket, double hz)
    {
        m_durum[etiket].yedekFrekansHz = hz;
    }

    static void LogSiniri(const std::string& etiket, uint32_t n)
    {
        m_durum[etiket].logSiniri = n;
    }

    static void SayaclariSifirla(const std::string& etiket)
    {
        m_durum[etiket].cagriSayisi = 0;
        m_durum[etiket].yedekSayisi = 0;
        m_durum[etiket].logSayaci = 0;
    }

  private:
    // ---------------------------------------------------------------------------------
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override
    {
        uint32_t idA = a->GetObject<Node>()->GetId();
        uint32_t idB = b->GetObject<Node>()->GetId();

        std::string txId;
        std::string rxId;

        auto vA = m_vericiler.find(idA);
        auto aB = m_aliciler.find(idB);
        auto vB = m_vericiler.find(idB);
        auto aA = m_aliciler.find(idA);

        // m_durum static oldugu icin const metottan degistirilebiliyor
        // (static uyeler nesnenin degil, sinifin parcasi -- const'tan etkilenmiyor)
        Durum& durum = m_durum[m_etiket];

        if (vA != m_vericiler.end() && aB != m_aliciler.end())
        {
            txId = vA->second; // a = verici, b = alici  (downlink)
            rxId = aB->second;
        }
        else if (vB != m_vericiler.end() && aA != m_aliciler.end())
        {
            txId = vB->second; // ters yon (uplink) -- yol kaybi karsilikli
            rxId = aA->second;
        }
        else
        {
            // Izde olmayan baglantilar (arac<->arac girisimi) icin Friis yedegi.
            // Ayrinti icin arac-munih.cc / eski notlar.
            ++durum.yedekSayisi;

            double d = a->GetDistanceFrom(b);
            if (d < 1.0)
            {
                d = 1.0;
            }
            const double ISIK_HIZI = 299792458.0;
            double kayipDb = 20.0 * std::log10(4.0 * M_PI * d * durum.yedekFrekansHz / ISIK_HIZI);
            return txPowerDbm - kayipDb;
        }

        double t = Simulator::Now().GetSeconds();
        double kazanc = SionnaIz::Kazanc(m_etiket, txId, rxId, t);

        ++durum.cagriSayisi;
        if (durum.logSayaci < durum.logSiniri)
        {
            ++durum.logSayaci;
            std::cout << "  [IZ:" << m_etiket << "] t=" << t << "s  " << txId << " -> " << rxId
                      << "   kazanc=" << kazanc << " dB\n";
        }

        return txPowerDbm + kazanc;
    }

    // ---------------------------------------------------------------------------------
    int64_t DoAssignStreams(int64_t stream) override
    {
        return 0; // model tamamen belirlenimci, rastgele sayi tuketmiyor
    }

    /// Bir bandin sayac/ayar durumu -- eskiden sinifin dogrudan static uyeleriydi,
    /// artik etiket basina bir tane var.
    struct Durum
    {
        uint64_t cagriSayisi = 0;
        uint64_t yedekSayisi = 0;
        double yedekFrekansHz = 3.5e9;
        uint32_t logSiniri = 0;
        uint32_t logSayaci = 0;
    };

    std::string m_etiket = "varsayilan"; // ns-3 Attribute uzerinden ayarlanir (nesneye ozel)

    inline static std::map<uint32_t, std::string> m_vericiler; // dugum id -> "gnb0"
    inline static std::map<uint32_t, std::string> m_aliciler;  // dugum id -> "A_ileri"
    inline static std::map<std::string, Durum> m_durum;        // etiket -> bant-bazli durum
};

} // namespace ns3

#endif // SIONNA_IZ_H