// =====================================================================================
//  arac-munih.cc — Munih senaryosu: 4 gNB, 6 arac (A/B/C rotalari, zit yonlu), uplink
//
//  KRITIK: geometri Sionna tarafiyla (munih_yorunge.py) BIREBIR ayni olmali.
//  Baslangic konumu + hiz vektorleri munih_yorunge.npz'den python ile turetildi.
//
//  DINAMIK + HISTERESIS SENSOR TESTI: CONTROL->FR1, MAP->FR2; SENSOR UL, FR2 Sionna kazancina gore FR1/FR2 arasinda degisir.
//
//  Kosturma:
//     ./ns3 run "arac-munih"
//     ./ns3 run "arac-munih --sionna=0"   (karsilastirma: saf 3GPP UMi)
// =====================================================================================

#include "sionna-iz.h"

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AracMunih");


// Handover loglarini insan-okunur yapmak ve saymak icin
std::map<uint64_t, std::string> g_imsiAracAdi;
std::map<uint64_t, uint32_t> g_handoverSayisi;
uint32_t g_toplamHandover = 0;



// =====================================================================================
//  M.9 — Arac-bazli RNTI-farkindalikli BWP yonlendirmesi.
//
// NOT : güncel scene'de handover algoritmasının çalışması için arabalar yeterince
//       mesafe katedemiyor. bu sebeple bwp yönlendirme algoritmasında da gereksiz komplikasyon
//       olmaması açısından gnbler hep sabit gibi kodladım, scene değişirse değiştireceğim
// =====================================================================================
class AracBazliBwpAlgoritmasi : public BwpManagerAlgorithm
{
  public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("ns3::AracBazliBwpAlgoritmasi")
                                .SetParent<BwpManagerAlgorithm>()
                                .SetGroupName("nr")
                                .AddConstructor<AracBazliBwpAlgoritmasi>();
        return tid;
    }

    // STATIK CIFT-BANT ESLEMESI:
    // CONTROL (GBR_V2X) -> FR1 / BWP0
    // SENSOR ve MAP        -> FR2 / BWP1
    uint8_t GetBwpForQosFlow(const NrQosFlow::FiveQi& v) const override
    {
        // Bu loglar sadece ilk kez yazilir; terminali gereksiz yere doldurmaz.
        static bool kontrolYazildi = false;
        static bool sensorYazildi = false;
        static bool haritaYazildi = false;

        if (v == NrQosFlow::GBR_V2X)
        {
            if (!kontrolYazildi)
            {
                std::cout << "[GNB STATIK] CONTROL (GBR_V2X) -> FR1 / BWP0\n";
                kontrolYazildi = true;
            }
            return BWP_FR1;
        }

        if (v == NrQosFlow::NGBR_LOW_LAT_EMBB)
        {
            if (!sensorYazildi)
            {
                std::cout << "[GNB STATIK] SENSOR varsayilan (NGBR_LOW_LAT_EMBB) -> FR2 / BWP1\n";
                sensorYazildi = true;
            }
            return BWP_FR2;
        }

        if (v == NrQosFlow::NGBR_VIDEO_TCP_DEFAULT)
        {
            if (!haritaYazildi)
            {
                std::cout << "[GNB STATIK] MAP (NGBR_VIDEO_TCP_DEFAULT) -> FR2 / BWP1\n";
                haritaYazildi = true;
            }
            return BWP_FR2;
        }

        // Bu senaryoda beklenmeyen baska bir 5QI gelirse FR2'ye gonder.
        return BWP_FR2;
    }

  private:
    static constexpr uint8_t BWP_FR1 = 0;
    static constexpr uint8_t BWP_FR2 = 1;
};


// =====================================================================================
//  DINAMIK SENSOR BWP KONTROLU
//
//  Yalnizca SENSOR UL akisi dinamik ve histerezisli:
//      Mevcut bant FR2 ise:
//          FR2 kazanci < -125 dB  -> FR1 / BWP0
//      Mevcut bant FR1 ise:
//          FR2 kazanci > -115 dB  -> FR2 / BWP1
//      -125 .. -115 dB arasinda mevcut bant KORUNUR.
//
//  CONTROL ve MAP statik kalir.
//  Kontrol 100 ms'de bir yapilir; terminale sadece ilk karar ve bant degisimleri yazilir.
// =====================================================================================

NetDeviceContainer g_dinamikUeDev;
std::vector<std::string> g_dinamikAracAdlari;
std::map<uint16_t, std::string> g_cellIdGnbAdi;
std::map<uint64_t, uint8_t> g_sensorSonBwp;
std::map<uint64_t, bool> g_sensorIlkKararYazildi;
std::map<uint64_t, uint32_t> g_sensorBwpDegisimSayisi;

constexpr uint8_t DIN_BWP_FR1 = 0;
constexpr uint8_t DIN_BWP_FR2 = 1;
constexpr double DIN_SENSOR_FR2DEN_FR1E_ESIK_DB = -125.0;
constexpr double DIN_SENSOR_FR1DEN_FR2YE_ESIK_DB = -115.0;

Time g_dinamikKontrolAraligi = MilliSeconds(100);
Time g_dinamikBitis = Seconds(10.0);
uint32_t g_sensorToplamBwpDegisimi = 0;

void
DinamikSensorBwpKontrol()
{
    const double t = Simulator::Now().GetSeconds();

    for (uint32_t i = 0; i < g_dinamikUeDev.GetN(); ++i)
    {
        Ptr<NetDevice> dev = g_dinamikUeDev.Get(i);
        Ptr<NrUeNetDevice> ue = dev->GetObject<NrUeNetDevice>();
        if (ue == nullptr)
        {
            continue;
        }

        const uint64_t imsi = ue->GetImsi();
        const uint16_t cellId = ue->GetCellId();

        auto gnbIt = g_cellIdGnbAdi.find(cellId);
        if (gnbIt == g_cellIdGnbAdi.end())
        {
            continue;
        }

        const std::string& aracAdi = g_dinamikAracAdlari.at(i);
        const std::string& gnbAdi = gnbIt->second;

        const double kazancFr2 = SionnaIz::Kazanc("FR2", gnbAdi, aracAdi, t);

        // Histerezis:
        // - SENSOR FR2'deyse ancak kanal -125 dB'nin altina dusunce FR1'e gec.
        // - SENSOR FR1'deyse ancak kanal -115 dB'nin ustune cikinca FR2'ye don.
        // - Aradaki bolgede mevcut bandi koru.
        const uint8_t oncekiBwp =
            g_sensorSonBwp.count(imsi) ? g_sensorSonBwp[imsi] : DIN_BWP_FR2;

        uint8_t hedefBwp = oncekiBwp;
        if (oncekiBwp == DIN_BWP_FR2 && kazancFr2 < DIN_SENSOR_FR2DEN_FR1E_ESIK_DB)
        {
            hedefBwp = DIN_BWP_FR1;
        }
        else if (oncekiBwp == DIN_BWP_FR1 &&
                 kazancFr2 > DIN_SENSOR_FR1DEN_FR2YE_ESIK_DB)
        {
            hedefBwp = DIN_BWP_FR2;
        }

        Ptr<BwpManagerUe> bwpManager = NrHelper::GetBwpManagerUe(dev);
        NS_ABORT_MSG_IF(bwpManager == nullptr,
                        "UE BwpManager bulunamadi: " << aracAdi);

        // v5.1 runtime API: bu UE'nin SENSOR 5QI'sini secilen BWP'ye ata.
        bwpManager->SetBwpForQosFlow(
            static_cast<uint8_t>(NrQosFlow::NGBR_LOW_LAT_EMBB),
            hedefBwp);

        const bool ilkKarar = !g_sensorIlkKararYazildi[imsi];

        if (ilkKarar)
        {
            std::cout << std::fixed << std::setprecision(3)
                      << t << "s  [DINAMIK SENSOR ILK] "
                      << aracAdi
                      << "  " << gnbAdi
                      << "  FR2 gain=" << std::setprecision(2) << kazancFr2 << " dB"
                      << "  -> " << (hedefBwp == DIN_BWP_FR2 ? "FR2/BWP1" : "FR1/BWP0")
                      << "\n";
            g_sensorIlkKararYazildi[imsi] = true;

            // Baslangicta SENSOR FR2'ye ayarli. Ilk kontrolde FR1'e gecerse gercek bir degisimdir.
            if (hedefBwp != DIN_BWP_FR2)
            {
                ++g_sensorToplamBwpDegisimi;
                ++g_sensorBwpDegisimSayisi[imsi];
            }
        }
        else if (hedefBwp != oncekiBwp)
        {
            ++g_sensorToplamBwpDegisimi;
            ++g_sensorBwpDegisimSayisi[imsi];

            std::cout << std::fixed << std::setprecision(3)
                      << t << "s  [DINAMIK SENSOR DEGISIM] "
                      << aracAdi
                      << "  " << gnbAdi
                      << "  FR2 gain=" << std::setprecision(2) << kazancFr2 << " dB"
                      << "  " << (oncekiBwp == DIN_BWP_FR2 ? "FR2/BWP1" : "FR1/BWP0")
                      << " -> "
                      << (hedefBwp == DIN_BWP_FR2 ? "FR2/BWP1" : "FR1/BWP0")
                      << "\n";
        }

        g_sensorSonBwp[imsi] = hedefBwp;
    }

    if (Simulator::Now() + g_dinamikKontrolAraligi < g_dinamikBitis)
    {
        Simulator::Schedule(g_dinamikKontrolAraligi, &DinamikSensorBwpKontrol);
    }
}

//============================== HANDOVER ===================================

void
NotifyHandoverStartGnb(std::string context,
                       uint64_t imsi,
                       uint16_t cellId,
                       uint16_t rnti,
                       uint16_t targetCellId)
{
    std::string aracAdi = g_imsiAracAdi.count(imsi) ? g_imsiAracAdi[imsi] : "BilinmeyenArac";

    std::cout << Simulator::Now().GetSeconds() << "s  [HANDOVER BASLADI] "
              << aracAdi << " (IMSI " << imsi << ")"
              << "  cellId " << cellId << " -> " << targetCellId
              << "  (rnti " << rnti << ")\n";
}

void
NotifyHandoverEndOkGnb(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    std::string aracAdi = g_imsiAracAdi.count(imsi) ? g_imsiAracAdi[imsi] : "BilinmeyenArac";

    ++g_handoverSayisi[imsi];
    ++g_toplamHandover;

    std::cout << Simulator::Now().GetSeconds() << "s  [HANDOVER TAMAMLANDI] "
              << aracAdi << " (IMSI " << imsi << ")"
              << "  yeni cellId " << cellId
              << "  (rnti " << rnti << ")\n";
}

//=====================================================================================

struct AracBaslangic
{
    std::string ad;             // iz dosyasindaki rx_id ile birebir ayni olmali
    double x0, y0, z0;
    double vx, vy, vz;
};

int main(int argc, char* argv[])
{
    // =================================================================================
    //  SIONNA TARAFIYLA (munih_yorunge.py) BIREBIR AYNI OLMASI GEREKEN DEGERLER
    // =================================================================================
    struct GnbTanimi { std::string ad; double x, y, z; };
    const std::array<GnbTanimi, 4> GNBLER = {{
        {"gnb0", -40.0, -95.0, 12.0},
        {"gnb1",   0.0,  60.0, 12.0},
        {"gnb2",  30.0, 200.0, 12.0},
        {"gnb3", 120.0,-140.0, 12.0},
    }};

    // munih_yorunge.npz'den python ile turetildi (bkz. sohbet gecmisi / notlar)
    const std::array<AracBaslangic, 6> ARACLAR = {{
        {"A_ileri",  -62.820,  -81.682, 1.5,   3.6261,  13.5223, 0.0},
        {"A_geri",    22.820,  237.682, 1.5,  -3.6261, -13.5223, 0.0},
        {"B_ileri",  -54.129,  -96.554, 1.5, -12.6902,   5.9125, 0.0},
        {"B_geri",  -281.871,    9.554, 1.5,  12.6902,  -5.9125, 0.0},
        {"C_ileri",  202.690, -220.982, 1.5, -12.1171,   7.0125, 0.0},
        {"C_geri",     2.310, -105.018, 1.5,  12.1171,  -7.0125, 0.0},
    }};

    const double FREKANS = 3.5e9;
    const double BANT_GENISLIGI = 20e6;
    const double FREKANS_FR2 = 28e9;
    const double BANT_GENISLIGI_FR2 = 100e6;
    const uint16_t NUMEROLOJI = 1;
    const double TX_GUC_DBM = 30.0;
    const std::string TDD_DESENI = "DL|S|UL|UL|DL|DL|S|UL|UL|DL|";
    const Time SIM_SURESI = Seconds(10.0);   // izin tam kapsadigi sure

    //varsayılan değerler
    std::string izYolu = "scratch/iz_munih.csv";
    const std::string ETIKET = "FR1"; // M.7: cok-iz destegi -- M.8'de "FR2" ikinci bant icin eklenecek
    const std::string ETIKET_FR2 = "FR2";
    std::string izYoluFr2 = "scratch/iz_munih_fr2.csv";

    bool sionnaKullan = true;
    uint32_t logSiniri = 8;

    // --iz komutu ile izYolu ' nu terminalden değiştirebilmemizi sağlar
    // ikinci parametreler ise --help ile alacağımız açıklamadır
    CommandLine cmd(__FILE__);
    cmd.AddValue("iz", "Iz dosyasinin yolu", izYolu);
    cmd.AddValue("sionna", "1: Sionna izi, 0: saf 3GPP UMi (karsilastirma)", sionnaKullan);
    cmd.AddValue("log", "Ilk kac kanal cagrisi ekrana basilsin", logSiniri);
    cmd.Parse(argc, argv);

    // =================================================================================
    //  1. IZI YUKLE + gNB KONUMLARINI DOGRULA
    // =================================================================================
    if (sionnaKullan)
      {
          SionnaIz::Yukle(izYolu, ETIKET);
          SionnaIzYolKaybi::LogSiniri(ETIKET, logSiniri);
          SionnaIzYolKaybi::SayaclariSifirla(ETIKET);
          SionnaIzYolKaybi::YedekFrekansAyarla(ETIKET, FREKANS);

          for (const auto& g : GNBLER)
          {
              auto izGnb = SionnaIz::VericiKonumu(ETIKET, g.ad);
              NS_ABORT_MSG_IF(izGnb.empty(), "Izde " << g.ad << " bulunamadi!");
              double sapma = std::sqrt(std::pow(izGnb[0] - g.x, 2) +
                                       std::pow(izGnb[1] - g.y, 2) +
                                       std::pow(izGnb[2] - g.z, 2));
              std::cout << g.ad << " konum kontrolu [" << ETIKET << "]: sapma=" << sapma << " m\n";
              NS_ABORT_MSG_IF(sapma > 0.01, g.ad << " konumu iz dosyasiyla UYUSMUYOR!");
          }

          // --- FR2 izi (M.8) -- ayni geometri, farkli frekans ---
          SionnaIz::Yukle(izYoluFr2, ETIKET_FR2);
          SionnaIzYolKaybi::LogSiniri(ETIKET_FR2, logSiniri);
          SionnaIzYolKaybi::SayaclariSifirla(ETIKET_FR2);
          SionnaIzYolKaybi::YedekFrekansAyarla(ETIKET_FR2, FREKANS_FR2);

          for (const auto& g : GNBLER)
          {
              auto izGnb = SionnaIz::VericiKonumu(ETIKET_FR2, g.ad);
              NS_ABORT_MSG_IF(izGnb.empty(), "FR2 izinde " << g.ad << " bulunamadi!");
              double sapma = std::sqrt(std::pow(izGnb[0] - g.x, 2) +
                                       std::pow(izGnb[1] - g.y, 2) +
                                       std::pow(izGnb[2] - g.z, 2));
              std::cout << g.ad << " konum kontrolu [" << ETIKET_FR2 << "]: sapma=" << sapma << " m\n";
              NS_ABORT_MSG_IF(sapma > 0.01, g.ad << " konumu FR2 iz dosyasiyla UYUSMUYOR!");
          }
      }

    // =================================================================================
    //  2. DUGUMLER ve MOBILITE  (Sionna yorungesiyle birebir ayni)
    // =================================================================================
    NodeContainer gnbDugumleri;
    gnbDugumleri.Create(GNBLER.size());
    NodeContainer aracDugumleri;
    aracDugumleri.Create(ARACLAR.size());

    MobilityHelper mobGnb;
    mobGnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobGnb.Install(gnbDugumleri);
    for (size_t i = 0; i < GNBLER.size(); ++i)
    {
        gnbDugumleri.Get(i)->GetObject<MobilityModel>()->SetPosition(
            Vector(GNBLER[i].x, GNBLER[i].y, GNBLER[i].z));
    }

    MobilityHelper mobArac;
    mobArac.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobArac.Install(aracDugumleri);
    for (size_t i = 0; i < ARACLAR.size(); ++i)
    {
        auto mm = aracDugumleri.Get(i)->GetObject<ConstantVelocityMobilityModel>();
        mm->SetPosition(Vector(ARACLAR[i].x0, ARACLAR[i].y0, ARACLAR[i].z0));
        mm->SetVelocity(Vector(ARACLAR[i].vx, ARACLAR[i].vy, ARACLAR[i].vz));
    }

    // =================================================================================
    //  3. DUGUM <-> IZ KIMLIGI ESLEMESI
    // =================================================================================
    if (sionnaKullan)
    {
        for (size_t i = 0; i < GNBLER.size(); ++i)
        {
            SionnaIzYolKaybi::VericiKaydet(gnbDugumleri.Get(i)->GetId(), GNBLER[i].ad);
            std::cout << "esleme: dugum " << gnbDugumleri.Get(i)->GetId() << " = "
                      << GNBLER[i].ad << "\n";
        }
        for (size_t i = 0; i < ARACLAR.size(); ++i)
        {
            SionnaIzYolKaybi::AliciKaydet(aracDugumleri.Get(i)->GetId(), ARACLAR[i].ad);
            std::cout << "esleme: dugum " << aracDugumleri.Get(i)->GetId() << " = "
                      << ARACLAR[i].ad << "\n";
        }
    }

    // =================================================================================
    //  4. NR yardimcilari (NewRadio)
    // =================================================================================
    Ptr<NrPointToPointEpcHelper> epc = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> bf = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nr = CreateObject<NrHelper>(); //NrHelper pointleyecek nr ptr oluştur ve onun işaret edeceği NrHelper tipinde nesneyi yarat 
    nr->SetBeamformingHelper(bf);
    nr->SetEpcHelper(epc);

    // =================================================================================
    //  5. CIFT BANT (FR1 + FR2) + KANAL MODELI
    // =================================================================================
    

    CcBwpCreator ccBwpCreator; //ikisi için de kullanılacak çünkü kimlik sayaçlarını paylaşmalılar

    CcBwpCreator::SimpleOperationBandConf fr1Conf(FREKANS , BANT_GENISLIGI , 1);
    OperationBandInfo bantFR1 = ccBwpCreator.CreateOperationBandContiguousCc(fr1Conf);

    CcBwpCreator::SimpleOperationBandConf fr2Conf(FREKANS_FR2, BANT_GENISLIGI_FR2, 1);
    OperationBandInfo bantFR2 = ccBwpCreator.CreateOperationBandContiguousCc(fr2Conf);

    Ptr<NrChannelHelper> chFR1 = CreateObject<NrChannelHelper>();
    chFR1->ConfigureFactories("UMi" , "Default" ,"ThreeGpp");

    Ptr<NrChannelHelper> chFR2 = CreateObject<NrChannelHelper>();
    chFR2->ConfigureFactories("UMi" , "Default" , "ThreeGpp");

    if (sionnaKullan)
    {
        chFR1 -> ConfigurePropagationFactory(SionnaIzYolKaybi::GetTypeId());
        chFR1 -> SetPathlossAttribute("Etiket" , StringValue(ETIKET));

        chFR2 -> ConfigurePropagationFactory(SionnaIzYolKaybi::GetTypeId());
        chFR2 -> SetPathlossAttribute("Etiket" , StringValue(ETIKET_FR2));

        std::cout << "yol kaybi modeli: SionnaIzYolKaybi (FR1 + FR2)\n";
    }
    else
    {
        std::cout << "yol kaybi modeli: 3GPP UMi (varsayilan)\n";
    }

    chFR1->AssignChannelsToBands({bantFR1});
    chFR2->AssignChannelsToBands({bantFR2});

    BandwidthPartInfoPtrVector tumBwpler = CcBwpCreator::GetAllBwps({bantFR1, bantFR2});

    bf->SetAttribute("BeamformingMethod", TypeIdValue(DirectPathBeamforming::GetTypeId()));
    epc->SetAttribute("S1uLinkDelay", TimeValue(MilliSeconds(0)));

    // =================================================================================
    //  6. ANTEN
    // =================================================================================
    nr->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nr->SetUeAntennaAttribute("NumColumns", UintegerValue(4));
    nr->SetUeAntennaAttribute("AntennaElement",
                              PointerValue(CreateObject<IsotropicAntennaModel>()));
    nr->SetGnbAntennaAttribute("NumRows", UintegerValue(4));
    nr->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nr->SetGnbAntennaAttribute("AntennaElement",
                               PointerValue(CreateObject<IsotropicAntennaModel>()));
    
    // =================================================================================
    //  7. CIHAZLARI KUR
    // =================================================================================

    // 5G-LENA ile uyumlu trafik/QoS-bazli BWP yonlendirmesi
    // InstallGnbDevice/InstallUeDevice'dan ONCE olmali (SetTypeId -> ObjectFactory notu)
    // gNB: CONTROL/MAP icin statik trafik->BWP eslemesi.
    nr->SetGnbBwpManagerAlgorithmTypeId(AracBazliBwpAlgoritmasi::GetTypeId());

    // UE: v5.1'de SetBwpForQosFlow(...) ile runtime degistirebilmek icin
    // BwpManagerAlgorithmStatic kullanmak zorundayiz.
    nr->SetUeBwpManagerAlgorithmTypeId(BwpManagerAlgorithmStatic::GetTypeId());

     // Dinamik handover: RSRP tabanli, "en guclu hucre" algoritmasi (3GPP Event A3)
    nr->SetHandoverAlgorithmType("ns3::NrA3RsrpHandoverAlgorithm");
    nr->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
    nr->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(256)));
    
    NetDeviceContainer gnbDev = nr->InstallGnbDevice(gnbDugumleri, tumBwpler);
    NetDeviceContainer ueDev = nr->InstallUeDevice(aracDugumleri, tumBwpler);

    // CellId -> gNB adi eslemesi. Handover sonrasi UE'nin yeni servis gNB'sini
    // GetCellId() ile bulup Sionna FR2 kazancini dogru linkten okuyacagiz.
    for (uint32_t i = 0; i < gnbDev.GetN(); ++i)
    {
        uint16_t cellId = gnbDev.Get(i)->GetObject<NrGnbNetDevice>()->GetCellId();
        g_cellIdGnbAdi[cellId] = GNBLER[i].ad;
    }

    // Dinamik kontrolde kullanilacak UE ve arac adlarini sakla.
    g_dinamikUeDev = ueDev;
    g_dinamikAracAdlari.clear();
    for (const auto& arac : ARACLAR)
    {
        g_dinamikAracAdlari.push_back(arac.ad);
    }

    // UE tarafindaki ilk esleme:
    // CONTROL -> FR1, SENSOR -> FR2, MAP -> FR2.
    // Daha sonra yalnizca SENSOR 5QI'si runtime'da degistirilecek.
    for (uint32_t i = 0; i < ueDev.GetN(); ++i)
    {
        Ptr<BwpManagerUe> bwpManager = NrHelper::GetBwpManagerUe(ueDev.Get(i));
        NS_ABORT_MSG_IF(bwpManager == nullptr, "UE BwpManager bulunamadi!");

        bwpManager->SetBwpForQosFlow(
            static_cast<uint8_t>(NrQosFlow::GBR_V2X),
            DIN_BWP_FR1);
        bwpManager->SetBwpForQosFlow(
            static_cast<uint8_t>(NrQosFlow::NGBR_LOW_LAT_EMBB),
            DIN_BWP_FR2);
        bwpManager->SetBwpForQosFlow(
            static_cast<uint8_t>(NrQosFlow::NGBR_VIDEO_TCP_DEFAULT),
            DIN_BWP_FR2);
    }

    int64_t akim = 1;
    akim += nr->AssignStreams(gnbDev, akim);
    akim += nr->AssignStreams(ueDev, akim);

    // Her gNB'ye iki bant icin de ayni numeroloji/guc ata
    for (size_t i = 0; i < GNBLER.size(); ++i)
    {
        auto gnbPhy1 = NrHelper::GetGnbPhy(gnbDev.Get(i), 0);
        gnbPhy1->SetAttribute("Numerology", UintegerValue(NUMEROLOJI));
        gnbPhy1->SetTxPower(TX_GUC_DBM);
        gnbPhy1->SetAttribute("Pattern", StringValue(TDD_DESENI));

        auto gnbPhy2 = NrHelper::GetGnbPhy(gnbDev.Get(i), 1);
        gnbPhy2->SetAttribute("Numerology", UintegerValue(NUMEROLOJI));
        gnbPhy2->SetTxPower(TX_GUC_DBM);
        gnbPhy2->SetAttribute("Pattern", StringValue(TDD_DESENI));
    }
    // =================================================================================
    //  8. Internet
    // =================================================================================
    auto [uzak, uzakIp] = epc->SetupRemoteHost("100Gb/s", 2500, Seconds(0.0));
    InternetStackHelper internet;
    internet.Install(aracDugumleri);
    Ipv4InterfaceContainer ueIp = epc->AssignUeIpv4Address(ueDev);

    // IP ve IMSI degerlerini arac adlariyla esle.
    // Boylece FlowMonitor ve handover ciktilari "7.0.0.2" yerine "A_ileri" gibi okunabilir.
    std::map<std::string, std::string> ipAracAdi;
    for (uint32_t i = 0; i < ueDev.GetN(); ++i)
    {
        uint64_t imsi = ueDev.Get(i)->GetObject<NrUeNetDevice>()->GetImsi();
        g_imsiAracAdi[imsi] = ARACLAR[i].ad;

        std::ostringstream ipAkis;
        ipAkis << ueIp.GetAddress(i);
        ipAracAdi[ipAkis.str()] = ARACLAR[i].ad;
    }

    nr->AttachToClosestGnb(ueDev, gnbDev);
    nr->AddX2Interface(gnbDugumleri);   // gNB'ler arasi tam-baglantili X2 mesh kurar


     // =================================================================================
    //  9. TRAFIK — uc akis: UL sensor (agir) + DL kontrol (hafif) + DL harita (orta)
    // =================================================================================
    const uint16_t SENSOR_PORT  = 3000;   // UL
    const uint16_t KONTROL_PORT = 3001;   // DL
    const uint16_t HARITA_PORT  = 3002;   // DL

    const uint32_t SENSOR_BOYUT  = 1200;
    const double   SENSOR_HIZ    = 150.0;  // (~1.44 Mbps/arac, ~8.6 Mbps toplam) -- 500'den dusuruldu
    const uint32_t KONTROL_BOYUT = 100;
    const double   KONTROL_HIZ   = 50.0;   // degismedi
    const uint32_t HARITA_BOYUT  = 1000;
    const double   HARITA_HIZ    = 100.0;  // degismedi, once sensor etkisini gorelim

    // --- QoS tanimlari (5QI + port filtresi) ---
    NrQosFlow sensorAkisi(NrQosFlow::NGBR_LOW_LAT_EMBB);
    Ptr<NrQosRule> sensorKurali = Create<NrQosRule>();
    NrQosRule::PacketFilter sensorFiltre;
    // SENSOR UL: arac -> uzak sunucu.
    // Bu nedenle UE tarafinda eslestirilecek port "remote" porttur.
    sensorFiltre.remotePortStart = SENSOR_PORT;
    sensorFiltre.remotePortEnd = SENSOR_PORT;
    sensorKurali->Add(sensorFiltre);

    NrQosFlow kontrolAkisi(NrQosFlow::GBR_V2X);
    Ptr<NrQosRule> kontrolKurali = Create<NrQosRule>();
    NrQosRule::PacketFilter kontrolFiltre;
    kontrolFiltre.localPortStart = KONTROL_PORT;
    kontrolFiltre.localPortEnd = KONTROL_PORT;
    kontrolKurali->Add(kontrolFiltre);

    NrQosFlow haritaAkisi(NrQosFlow::NGBR_VIDEO_TCP_DEFAULT);
    Ptr<NrQosRule> haritaKurali = Create<NrQosRule>();
    NrQosRule::PacketFilter haritaFiltre;
    haritaFiltre.localPortStart = HARITA_PORT;
    haritaFiltre.localPortEnd = HARITA_PORT;
    haritaKurali->Add(haritaFiltre);

    ApplicationContainer sunucuUygulamalari;
    ApplicationContainer istemciUygulamalari;

    // Sensor UL icin TEK sunucu -- butun araclar ayni bulut sunucusuna gonderiyor
    UdpServerHelper sensorAlici(SENSOR_PORT);
    sunucuUygulamalari.Add(sensorAlici.Install(uzak));

    for (size_t i = 0; i < ARACLAR.size(); ++i)
    {
        Ptr<Node> aracDugum = aracDugumleri.Get(i);
        Ptr<NetDevice> aracCihaz = ueDev.Get(i);
        Address aracAdres = ueIp.GetAddress(i);

        // --- UL: arac -> uzak sunucu (sensor, agir) ---
        UdpClientHelper sensorGonderici;
        sensorGonderici.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
        sensorGonderici.SetAttribute("PacketSize", UintegerValue(SENSOR_BOYUT));
        sensorGonderici.SetAttribute("Interval", TimeValue(Seconds(1.0 / SENSOR_HIZ)));
        sensorGonderici.SetAttribute(
            "Remote", AddressValue(addressUtils::ConvertToSocketAddress(uzakIp, SENSOR_PORT)));
        istemciUygulamalari.Add(sensorGonderici.Install(aracDugum));

        // --- DL: uzak sunucu -> arac (kontrol, hafif) ---
        UdpServerHelper kontrolAlici(KONTROL_PORT);
        sunucuUygulamalari.Add(kontrolAlici.Install(aracDugum));

        UdpClientHelper kontrolGonderici;
        kontrolGonderici.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
        kontrolGonderici.SetAttribute("PacketSize", UintegerValue(KONTROL_BOYUT));
        kontrolGonderici.SetAttribute("Interval", TimeValue(Seconds(1.0 / KONTROL_HIZ)));
        kontrolGonderici.SetAttribute(
            "Remote", AddressValue(addressUtils::ConvertToSocketAddress(aracAdres, KONTROL_PORT)));
        istemciUygulamalari.Add(kontrolGonderici.Install(uzak));

        // --- DL: uzak sunucu -> arac (harita, orta) ---
        UdpServerHelper haritaAlici(HARITA_PORT);
        sunucuUygulamalari.Add(haritaAlici.Install(aracDugum));

        UdpClientHelper haritaGonderici;
        haritaGonderici.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
        haritaGonderici.SetAttribute("PacketSize", UintegerValue(HARITA_BOYUT));
        haritaGonderici.SetAttribute("Interval", TimeValue(Seconds(1.0 / HARITA_HIZ)));
        haritaGonderici.SetAttribute(
            "Remote", AddressValue(addressUtils::ConvertToSocketAddress(aracAdres, HARITA_PORT)));
        istemciUygulamalari.Add(haritaGonderici.Install(uzak));

        // --- Bu aracta uc ayri QoS akisi aktive et ---
        nr->ActivateDedicatedQosFlow(aracCihaz, sensorAkisi, sensorKurali);
        nr->ActivateDedicatedQosFlow(aracCihaz, kontrolAkisi, kontrolKurali);
        nr->ActivateDedicatedQosFlow(aracCihaz, haritaAkisi, haritaKurali);
    }

    sunucuUygulamalari.Start(MilliSeconds(200));
    istemciUygulamalari.Start(MilliSeconds(200));
    sunucuUygulamalari.Stop(SIM_SURESI);
    istemciUygulamalari.Stop(SIM_SURESI);

    if (sionnaKullan)
    {
        std::cout << "\nDinamik SENSOR + HISTERESIS AKTIF: "
                  << "FR2->FR1 < " << DIN_SENSOR_FR2DEN_FR1E_ESIK_DB
                  << " dB, FR1->FR2 > " << DIN_SENSOR_FR1DEN_FR2YE_ESIK_DB
                  << " dB. Kontrol araligi=100 ms\n";
        g_dinamikBitis = SIM_SURESI;
        Simulator::Schedule(MilliSeconds(300), &DinamikSensorBwpKontrol);
    }
    else
    {
        std::cout << "\nUYARI: --sionna=0 iken dinamik SENSOR secimi kapali; SENSOR FR2'de kalir.\n";
    }

    // =================================================================================
    //  10. OLCUM + KOSTUR
    // =================================================================================
    FlowMonitorHelper fm;
    NodeContainer olculecek;
    olculecek.Add(uzak);
    olculecek.Add(aracDugumleri);
    Ptr<FlowMonitor> monitor = fm.Install(olculecek);

    Config::Connect("/NodeList/*/DeviceList/*/NrGnbRrc/HandoverStart", //handover log bağlama
                    MakeCallback(&NotifyHandoverStartGnb));
    Config::Connect("/NodeList/*/DeviceList/*/NrGnbRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOkGnb));

    if (sionnaKullan && logSiniri > 0)
    {
        std::cout << "\n--- ilk " << logSiniri << " kanal cagrisi ---\n";
    }

    Simulator::Stop(SIM_SURESI);
    Simulator::Run();

    // =================================================================================
    //  11. DOGRULAMA
    // =================================================================================
    std::cout << "\n================= SONUC =================\n";

    if (sionnaKullan)
    {
        uint64_t cagri = SionnaIzYolKaybi::CagriSayisi(ETIKET);
        uint64_t yedek = SionnaIzYolKaybi::YedekSayisi(ETIKET);
        uint64_t cagriFr2 = SionnaIzYolKaybi::CagriSayisi(ETIKET_FR2);
        uint64_t yedekFr2 = SionnaIzYolKaybi::YedekSayisi(ETIKET_FR2);

        std::cout << "Izden okunan cagri (gNB<->arac) [" << ETIKET << "] : " << cagri << "\n";
        std::cout << "Friis yedegi     (arac<->arac) [" << ETIKET << "] : " << yedek << "\n";
        std::cout << "Izden okunan cagri (gNB<->arac) [" << ETIKET_FR2 << "]: " << cagriFr2 << "\n";
        std::cout << "Friis yedegi     (arac<->arac) [" << ETIKET_FR2 << "]: " << yedekFr2 << "\n";

        if (cagri == 0 || cagriFr2 == 0)
        {
            std::cout << ">>> UYARI: en az bir bant hic kullanilmadi -- kanal modeline baglanmamis!\n";
        }
        else
        {
            std::cout << ">>> Her iki iz de basariyla kullanildi.\n";
        }
    }

    monitor->CheckForLostPackets();
    auto sinif = DynamicCast<Ipv4FlowClassifier>(fm.GetClassifier());
    double sure = (SIM_SURESI - MilliSeconds(200)).GetSeconds();

    std::cout << "\n--- akis sonuclari ---\n";
    std::cout << "Arac       | Trafik   | Yon | Throughput | Gecikme    | Kayip\n";
    std::cout << "-----------------------------------------------------------------\n";

    for (auto& [id, st] : monitor->GetFlowStats())
    {
        auto t = sinif->FindFlow(id);
        double mbps = st.rxBytes * 8.0 / sure / 1e6;
        double gecikme =
            st.rxPackets ? 1000.0 * st.delaySum.GetSeconds() / st.rxPackets : 0.0;
        double kayip =
            st.txPackets ? 100.0 * (st.txPackets - st.rxPackets) / st.txPackets : 0.0;

        std::ostringstream kaynakIpAkis;
        kaynakIpAkis << t.sourceAddress;
        std::ostringstream hedefIpAkis;
        hedefIpAkis << t.destinationAddress;

        std::string kaynakIp = kaynakIpAkis.str();
        std::string hedefIp = hedefIpAkis.str();

        std::string trafik = "Bilinmiyor";
        std::string yon = "?";
        std::string aracAdi = "Bilinmeyen";

        if (t.destinationPort == SENSOR_PORT)
        {
            trafik = "SENSOR";
            yon = "UL";
            if (ipAracAdi.count(kaynakIp))
            {
                aracAdi = ipAracAdi[kaynakIp];
            }
        }
        else if (t.destinationPort == KONTROL_PORT)
        {
            trafik = "CONTROL";
            yon = "DL";
            if (ipAracAdi.count(hedefIp))
            {
                aracAdi = ipAracAdi[hedefIp];
            }
        }
        else if (t.destinationPort == HARITA_PORT)
        {
            trafik = "MAP";
            yon = "DL";
            if (ipAracAdi.count(hedefIp))
            {
                aracAdi = ipAracAdi[hedefIp];
            }
        }

        std::cout << std::left << std::setw(10) << aracAdi << " | "
                  << std::setw(8) << trafik << " | "
                  << std::setw(3) << yon << " | "
                  << std::right << std::setw(7) << std::fixed << std::setprecision(2)
                  << mbps << " Mbps | "
                  << std::setw(7) << gecikme << " ms | "
                  << std::setw(6) << kayip << " %\n";
    }

    std::cout << "\n--- dinamik SENSOR + HISTERESIS BWP ozeti ---\n";
    std::cout << "FR2->FR1 esigi: " << DIN_SENSOR_FR2DEN_FR1E_ESIK_DB << " dB\n";
    std::cout << "FR1->FR2 esigi: " << DIN_SENSOR_FR1DEN_FR2YE_ESIK_DB << " dB\n";
    std::cout << "Toplam SENSOR BWP degisimi: " << g_sensorToplamBwpDegisimi << "\n";
    for (uint32_t i = 0; i < ueDev.GetN(); ++i)
    {
        uint64_t imsi = ueDev.Get(i)->GetObject<NrUeNetDevice>()->GetImsi();
        uint32_t adet = g_sensorBwpDegisimSayisi.count(imsi)
                            ? g_sensorBwpDegisimSayisi[imsi]
                            : 0;
        uint8_t sonBwp = g_sensorSonBwp.count(imsi)
                             ? g_sensorSonBwp[imsi]
                             : DIN_BWP_FR2;

        std::cout << "  " << std::left << std::setw(10) << ARACLAR[i].ad
                  << " : " << adet << " degisim"
                  << "  | son=" << (sonBwp == DIN_BWP_FR2 ? "FR2/BWP1" : "FR1/BWP0")
                  << "\n";
    }

    std::cout << "\n--- handover ozeti ---\n";
    std::cout << "Toplam tamamlanan handover: " << g_toplamHandover << "\n";
    for (const auto& [imsi, aracAdi] : g_imsiAracAdi)
    {
        uint32_t adet = g_handoverSayisi.count(imsi) ? g_handoverSayisi[imsi] : 0;
        std::cout << "  " << std::left << std::setw(10) << aracAdi
                  << " : " << adet << "\n";
    }

    Simulator::Destroy();
    return 0;
}
