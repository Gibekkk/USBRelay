#include "app_core.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
// usbrelay-gui.exe dibuild dengan -mwindows (tanpa console), jadi
// std::cerr tidak terlihat kalau gagal dobel-klik -- tampilkan MessageBox
// juga supaya error tidak "hilang" begitu saja.
#include <windows.h>
#endif

// Deklarasi dari gui_ui.cpp
void run_gui(AppCore& core, const std::string& channelsConfigPath, int argc, char* argv[]);

static void printUsage(const char *prog)
{
    std::cout
        << "Penggunaan:\n"
        << "  " << prog << " [OPSI]\n\n"
        << "OPSI:\n"
        << "  --config FILE          Path file konfigurasi device map (aturan\n"
        << "                         VID:PID -> auto ON/OFF relay).\n"
        << "                         Default: ../config/device_map.conf\n"
        << "  --channels-config FILE Path file konfigurasi channel AKTIF (1-16)\n"
        << "                         yang dipakai GUI + sistem round-robin.\n"
        << "                         Default: ../config/channels.conf\n"
        << "  --channels N           Paksa jumlah channel relay (1-16), lewati\n"
        << "                         auto-detect. Opsional, dipakai sekali saat\n"
        << "                         start -- bukan file config yang perlu diedit.\n"
        << "  --help                 Tampilkan pesan ini\n\n"
        << "Catatan: default path di atas relatif terhadap folder tempat binary\n"
        << "dijalankan -- jalankan selalu dari dalam folder dist/ (mis.\n"
        << "\"cd dist && ./usbrelay-gui\") supaya \"../config/\" mengarah ke folder\n"
        << "config/ di root project, persis di sebelah folder dist/.\n\n"
        << "Contoh:\n"
        << "  " << prog << " --config /etc/usbrelay/rules.conf\n"
        << "\nFormat config device map:\n"
        << "  VID:PID  CHANNEL  ACTION  [LABEL]\n"
        << "  Contoh:\n"
        << "    046d:c52b  1  open_on_connect  Mouse Logitech\n"
        << "    0bda:8153  2  open_on_connect  USB LAN\n\n"
        << "Format config channel aktif:\n"
        << "  Satu nomor channel per baris (1-16), boleh koma atau range.\n"
        << "  Contoh: 1-4   atau   1,2,3,4\n";
}

int main(int argc, char *argv[])
{
    std::string configPath         = "../config/device_map.conf";
    std::string channelsConfigPath = "../config/channels.conf";
    int channelOverride = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
        {
            configPath = argv[++i];
        }
        else if (strcmp(argv[i], "--channels-config") == 0 && i + 1 < argc)
        {
            channelsConfigPath = argv[++i];
        }
        else if (strcmp(argv[i], "--channels") == 0 && i + 1 < argc)
        {
            int n = std::atoi(argv[++i]);
            if (n > 0 && n <= 16) channelOverride = n;
            else std::cerr << "[WARN] --channels harus 1-16, diabaikan.\n";
        }
    }

    AppCore core;
    if (!core.init(configPath))
    {
        std::cerr << "[ERROR] Gagal inisialisasi. Pastikan libhidapi dan libudev tersedia.\n";
#if defined(_WIN32)
        MessageBoxA(nullptr,
            "Gagal inisialisasi USB Relay Auto-Control.\n"
            "Pastikan DLL hidapi tersedia (jalankan dari folder dist\\ apa adanya).",
            "USB Relay Auto-Control", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    if (channelOverride > 0)
        core.setChannelCountOverride(channelOverride);

    run_gui(core, channelsConfigPath, argc, argv);

    core.shutdown();
    return 0;
}
