#include "app_core.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32) && defined(USE_GUI)
// usbrelay-gui.exe dibuild dengan -mwindows (tanpa console), jadi
// std::cerr tidak terlihat kalau gagal dobel-klik -- tampilkan MessageBox
// juga supaya error tidak "hilang" begitu saja.
#include <windows.h>
#endif

// Deklarasi dari cli_ui.cpp dan gui_ui.cpp
#ifdef USE_GUI
// Hapus guard, tinggal:
void run_gui(AppCore& core, int argc, char* argv[]);
#else
void run_cli(AppCore &core, const std::string &configPath);
#endif

static void printUsage(const char *prog)
{
    std::cout
        << "Penggunaan:\n"
        << "  " << prog << " [OPSI]\n\n"
        << "OPSI:\n"
        << "  --cli              Jalankan mode terminal (ncurses) [default]\n"
        << "  --gui              Jalankan mode GUI (GTK3)\n"
        << "  --config FILE      Path file konfigurasi device map\n"
        << "                     Default: ./config/device_map.conf\n"
        << "  --channels N       Paksa jumlah channel relay (1-8), lewati\n"
        << "                     auto-detect. Opsional, dipakai sekali saat\n"
        << "                     start -- bukan file config yang perlu diedit.\n"
        << "  --help             Tampilkan pesan ini\n\n"
        << "Contoh:\n"
        << "  " << prog << " --cli --config /etc/usbrelay/rules.conf\n"
        << "  " << prog << " --gui\n"
        << "\nFormat config:\n"
        << "  VID:PID  CHANNEL  ACTION  [LABEL]\n"
        << "  Contoh:\n"
        << "    046d:c52b  1  open_on_connect  Mouse Logitech\n"
        << "    0bda:8153  2  open_on_connect  USB LAN\n";
}

int main(int argc, char *argv[])
{
    bool useGUI = false;
    std::string configPath = "config/device_map.conf";
    int channelOverride = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--gui") == 0)
            useGUI = true;
        else if (strcmp(argv[i], "--cli") == 0)
            useGUI = false;
        else if (strcmp(argv[i], "--help") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
        {
            configPath = argv[++i];
        }
        else if (strcmp(argv[i], "--channels") == 0 && i + 1 < argc)
        {
            int n = std::atoi(argv[++i]);
            if (n > 0 && n <= 8) channelOverride = n;
            else std::cerr << "[WARN] --channels harus 1-8, diabaikan.\n";
        }
    }

    AppCore core;
    if (!core.init(configPath))
    {
        std::cerr << "[ERROR] Gagal inisialisasi. Pastikan libhidapi dan libudev tersedia.\n";
#if defined(_WIN32) && defined(USE_GUI)
        MessageBoxA(nullptr,
            "Gagal inisialisasi USB Relay Auto-Control.\n"
            "Pastikan DLL hidapi tersedia (jalankan dari folder dist\\ apa adanya).",
            "USB Relay Auto-Control", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    if (channelOverride > 0)
        core.setChannelCountOverride(channelOverride);

    if (useGUI)
    {
#ifdef USE_GUI
        run_gui(core, argc, argv);
#else
        std::cerr << "[ERROR] Binary ini tidak mendukung GUI. Gunakan usbrelay-gui.\n";
        return 1;
#endif
    }
    else
    {
#ifdef USE_GUI
        std::cerr << "[ERROR] Binary ini tidak mendukung CLI. Gunakan usbrelay-cli.\n";
        return 1;
#else
        run_cli(core, configPath);
#endif
    }

    core.shutdown();
    return 0;
}