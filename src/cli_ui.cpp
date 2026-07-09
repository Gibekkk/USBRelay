#include "app_core.h"
#if defined(_WIN32)
  #include <curses.h>   // PDCurses -- API sama dengan ncurses
#else
  #include <ncurses.h>
#endif
#include <vector>
#include <string>
#include <deque>
#include <chrono>
#include <thread>
#include <algorithm>

// ---------------------------------------------------------------
// Layout:
//  +------------------------+----------------------------+
//  | RELAY STATUS           | USB DEVICES                |
//  | [CH1] ON  [CH2] OFF    | 046d:c52b Logitech Mouse   |
//  | ...                    | ...                        |
//  +--------------------------------------------------+--+
//  | LOG                                                  |
//  | [10:23:01] Device terhubung: Mouse [046d:c52b]       |
//  +------------------------------------------------------+
//  | Q:Keluar  R:Scan  A:All-ON  Z:All-OFF  1-8:Toggle   |
// ---------------------------------------------------------------

static const int PANEL_HEIGHT_RELAY = 12;
static const int LOG_LINES          = 10;

static WINDOW* win_relay = nullptr;
static WINDOW* win_usb   = nullptr;
static WINDOW* win_log   = nullptr;
static WINDOW* win_help  = nullptr;

static std::deque<std::string> g_logs;
static AppCore*                g_core = nullptr;

// Warna
enum Colors { C_TITLE=1, C_ON=2, C_OFF=3, C_USB=4, C_LOG=5, C_HELP=6 };

static void initColors() {
    start_color();
    init_pair(C_TITLE, COLOR_CYAN,   COLOR_BLUE);
    init_pair(C_ON,    COLOR_BLACK,  COLOR_GREEN);
    init_pair(C_OFF,   COLOR_BLACK,  COLOR_RED);
    init_pair(C_USB,   COLOR_YELLOW, COLOR_BLACK);
    init_pair(C_LOG,   COLOR_WHITE,  COLOR_BLACK);
    init_pair(C_HELP,  COLOR_BLACK,  COLOR_WHITE);
}

static void createWindows() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int half = cols / 2;

    win_relay = newwin(PANEL_HEIGHT_RELAY, half,       0,    0);
    win_usb   = newwin(PANEL_HEIGHT_RELAY, cols-half,  0,    half);
    win_log   = newwin(LOG_LINES,          cols,       PANEL_HEIGHT_RELAY, 0);
    win_help  = newwin(1,                  cols, PANEL_HEIGHT_RELAY + LOG_LINES, 0);
}

static void destroyWindows() {
    if (win_relay) { delwin(win_relay); win_relay = nullptr; }
    if (win_usb)   { delwin(win_usb);   win_usb   = nullptr; }
    if (win_log)   { delwin(win_log);   win_log   = nullptr; }
    if (win_help)  { delwin(win_help);  win_help  = nullptr; }
}

static void addLog(const std::string& msg) {
    g_logs.push_back(msg);
    if (g_logs.size() > 200) g_logs.pop_front();
}

static void drawRelayPanel(uint8_t status, const RelayInfo& info) {
    werase(win_relay);
    wbkgd(win_relay, COLOR_PAIR(C_LOG));
    wattron(win_relay, COLOR_PAIR(C_TITLE) | A_BOLD);
    mvwprintw(win_relay, 0, 0, " RELAY STATUS ");
    wattroff(win_relay, COLOR_PAIR(C_TITLE) | A_BOLD);

    if (info.serial.empty()) {
        mvwprintw(win_relay, 2, 2, "Tidak ada relay terhubung");
        mvwprintw(win_relay, 3, 2, "Tekan 'R' untuk scan");
    } else {
        mvwprintw(win_relay, 1, 2, "Serial: %s (%d ch)", info.serial.c_str(), info.num_channels);
        int channels = std::max(info.num_channels, 1);
        for (int ch = 1; ch <= channels; ch++) {
            bool on = (status >> (ch - 1)) & 1;
            mvwprintw(win_relay, 2 + ch, 2, "CH%d: ", ch);
            if (on) {
                wattron(win_relay, COLOR_PAIR(C_ON) | A_BOLD);
                wprintw(win_relay, "[ ON  ]");
                wattroff(win_relay, COLOR_PAIR(C_ON) | A_BOLD);
            } else {
                wattron(win_relay, COLOR_PAIR(C_OFF));
                wprintw(win_relay, "[ OFF ]");
                wattroff(win_relay, COLOR_PAIR(C_OFF));
            }
            wprintw(win_relay, "  Tekan '%d' toggle", ch);
        }
    }
    box(win_relay, 0, 0);
    wrefresh(win_relay);
}

static void drawUSBPanel(const std::vector<USBDevice>& devs) {
    werase(win_usb);
    wbkgd(win_usb, COLOR_PAIR(C_LOG));
    wattron(win_usb, COLOR_PAIR(C_TITLE) | A_BOLD);
    mvwprintw(win_usb, 0, 0, " USB DEVICES (%d) ", (int)devs.size());
    wattroff(win_usb, COLOR_PAIR(C_TITLE) | A_BOLD);

    int rows, cols;
    getmaxyx(win_usb, rows, cols);
    int maxShow = rows - 3;

    wattron(win_usb, COLOR_PAIR(C_USB));
    for (int i = 0; i < (int)devs.size() && i < maxShow; i++) {
        auto& d = devs[i];
        std::string label = d.product.empty() ? (d.vid + ":" + d.pid) : d.product;
        if ((int)label.size() > cols - 4) label = label.substr(0, cols - 7) + "...";
        mvwprintw(win_usb, 2 + i, 2, "%s:%s %s",
                  d.vid.c_str(), d.pid.c_str(), label.c_str());
    }
    wattroff(win_usb, COLOR_PAIR(C_USB));
    box(win_usb, 0, 0);
    wrefresh(win_usb);
}

static void drawLogPanel() {
    werase(win_log);
    wbkgd(win_log, COLOR_PAIR(C_LOG));
    wattron(win_log, COLOR_PAIR(C_TITLE) | A_BOLD);
    mvwprintw(win_log, 0, 0, " LOG ");
    wattroff(win_log, COLOR_PAIR(C_TITLE) | A_BOLD);

    int rows, cols;
    getmaxyx(win_log, rows, cols);
    int startIdx = (int)g_logs.size() - (rows - 2);
    if (startIdx < 0) startIdx = 0;

    wattron(win_log, COLOR_PAIR(C_LOG));
    for (int r = 1; r < rows - 1 && startIdx < (int)g_logs.size(); r++, startIdx++) {
        std::string line = g_logs[startIdx];
        if ((int)line.size() > cols - 3) line = line.substr(0, cols - 3);
        mvwprintw(win_log, r, 2, "%s", line.c_str());
    }
    wattroff(win_log, COLOR_PAIR(C_LOG));
    box(win_log, 0, 0);
    wrefresh(win_log);
}

static void drawHelpBar() {
    werase(win_help);
    wbkgd(win_help, COLOR_PAIR(C_HELP));
    wattron(win_help, COLOR_PAIR(C_HELP) | A_BOLD);
    mvwprintw(win_help, 0, 0,
              " Q:Keluar  R:Scan  C:Connect  D:Disconnect  A:All-ON  Z:All-OFF  1-8:Toggle ");
    wattroff(win_help, COLOR_PAIR(C_HELP) | A_BOLD);
    wrefresh(win_help);
}

static void refreshAll() {
    uint8_t status = g_core->getRelayStatus();
    RelayInfo info  = g_core->getRelayInfo();
    auto usbDevs    = g_core->getUSBDevices();
    drawRelayPanel(status, info);
    drawUSBPanel(usbDevs);
    drawLogPanel();
    drawHelpBar();
}

// Scan dan tampilkan relay devices untuk dipilih user
static void doScanAndConnect() {
    auto relays = g_core->getRelayDevices();
    if (relays.empty()) {
        addLog("[!] Tidak ada relay ditemukan. Pastikan device terpasang.");
        return;
    }
    if (relays.size() == 1) {
        if (g_core->connectRelay(relays[0].path))
            addLog("[+] Relay terhubung otomatis: " + relays[0].serial);
        else
            addLog("[!] Gagal terhubung ke relay: " + relays[0].path);
        return;
    }
    // Lebih dari 1 relay - tampilkan menu pilihan sederhana
    addLog("[?] Ditemukan " + std::to_string(relays.size()) + " relay:");
    for (int i = 0; i < (int)relays.size(); i++)
        addLog("    " + std::to_string(i+1) + ". " + relays[i].serial + " (" + relays[i].path + ")");
    addLog("[?] Otomatis pilih relay pertama: " + relays[0].serial);
    g_core->connectRelay(relays[0].path);
}

void run_cli(AppCore& core, const std::string& configPath) {
    g_core = &core;

    // Init ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);  // non-blocking getch
    curs_set(0);
    initColors();
    createWindows();

    addLog("[*] USB Relay Auto-Control dimulai");
    addLog("[*] Config: " + (configPath.empty() ? "(tidak ada)" : configPath));

    // Auto-scan relay saat start
    doScanAndConnect();
    refreshAll();

    bool running = true;
    auto lastRefresh = std::chrono::steady_clock::now();

    while (running) {
        // Poll events dari AppCore
        AppEvent ev;
        while (core.pollEvent(ev)) {
            addLog(ev.message);
        }

        // Handle keyboard
        int ch = getch();
        switch (ch) {
        case 'q': case 'Q':
            running = false;
            break;
        case 'r': case 'R':
            addLog("[*] Scan relay...");
            doScanAndConnect();
            break;
        case 'c': case 'C':
            doScanAndConnect();
            break;
        case 'd': case 'D':
            core.disconnectRelay();
            addLog("[*] Relay diputus.");
            break;
        case 'a': case 'A':
            if (core.setAll(true))  addLog("[+] Semua relay ON");
            else                    addLog("[!] Gagal: relay tidak terhubung");
            break;
        case 'z': case 'Z':
            if (core.setAll(false)) addLog("[+] Semua relay OFF");
            else                    addLog("[!] Gagal: relay tidak terhubung");
            break;
        case KEY_RESIZE:
            destroyWindows();
            createWindows();
            break;
        default:
            if (ch >= '1' && ch <= '8') {
                int idx = ch - '0';
                uint8_t status = core.getRelayStatus();
                bool    curOn  = (status >> (idx - 1)) & 1;
                if (core.setRelay(idx, !curOn))
                    addLog("[+] CH" + std::to_string(idx) + (curOn ? " OFF" : " ON"));
                else
                    addLog("[!] Gagal: relay tidak terhubung");
            }
            break;
        }

        // Refresh UI setiap 300ms
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh).count() > 300) {
            refreshAll();
            lastRefresh = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    destroyWindows();
    endwin();
}
