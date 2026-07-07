#include "app_core.h"
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <ctime>
#include <iomanip>
#include <cctype>
#include <algorithm>
#include "platform_compat.h"
// ---------------------------------------------------------------
// Widget globals
// ---------------------------------------------------------------
static GtkWidget*     g_window          = nullptr;
static GtkWidget*     g_lbl_status      = nullptr;
static GtkWidget*     g_lbl_devname     = nullptr;
static GtkWidget*     g_usb_list        = nullptr;
static GtkTextBuffer* g_log_buf         = nullptr;
static GtkTextView*   g_log_view        = nullptr;
static AppCore*       g_core            = nullptr;
static int            g_num_channels    = 0;
static GtkWidget*     g_nim_entry       = nullptr;

// ---------------------------------------------------------------
// Grid tombol channel besar, responsif (GtkFlowBox), dipakai SEKALIGUS
// sebagai (1) indikator status tiap channel via warna, dan (2) selector
// channel yang akan dinyalakan untuk scan NIM berikutnya:
//   - abu-abu = channel OFF (belum dipakai) -> boleh dipilih (tap)
//   - biru    = channel OFF & sedang DIPILIH untuk scan NIM berikutnya
//   - hijau   = channel ON (aktif dipakai) -> tidak bisa dipilih
// ---------------------------------------------------------------
static GtkWidget*     g_channel_selector = nullptr;        // GtkFlowBox, wadah tombol channel
static GtkWidget*     g_channel_buttons[8] = { nullptr };  // tombol per channel (index 0 = CH1)
static GtkWidget*     g_channel_lbl[8]     = { nullptr };  // label teks di dalam tiap tombol
static int            g_selected_channel   = 0;            // channel OFF yang sedang dipilih (0 = belum ada)
static uint8_t        g_last_status_cache  = 0;            // cache status terakhir, dipakai saat refresh setelah klik

// Path file log hasil scan (nim;nama;status;timestamp;silent_box_id)
static const std::string kLogDir = "logs";

static std::string todayLogPath() {
    auto t  = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%d%m%y");
    return kLogDir + "/" + ss.str() + ".csv";
}

// CSV NIM data: nim -> nama
static std::unordered_map<std::string, std::string> g_nim_map;

static void loadCSV(const std::string& path) {
    g_nim_map.clear();
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    std::getline(f, line); // skip header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto pos = line.find(';');
        std::string nim  = line.substr(0, pos);
        std::string nama = (pos == std::string::npos) ? "" : line.substr(pos + 1);
        while (!nama.empty() && (nama.back() == '\r' || nama.back() == '\n'))
            nama.pop_back();
        if (!nim.empty()) g_nim_map[nim] = nama;
    }
}

static bool findNIM(const std::string& nim, std::string& outNama) {
    auto it = g_nim_map.find(nim);
    if (it == g_nim_map.end()) return false;
    outNama = it->second;
    return true;
}

static std::string nowTimestamp() {
    auto t  = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Tulis satu baris hasil scan ke logs/ddmmyy.csv (append).
// File baru per hari, dibuat otomatis + header kalau belum ada.
static void appendScanLog(const std::string& nim,
                           const std::string& nama,
                           const std::string& status,   // "IN" atau "OUT"
                           int silent_box_id) {
    portable_mkdir(kLogDir);

    std::string path = todayLogPath();
    bool isNew = !portable_path_exists(path);

    std::ofstream f(path, std::ios::app);
    if (!f.is_open()) return;

    if (isNew)
        f << "nim;nama;status;timestamp;silent_box_id\n";

    f << nim << ';' << nama << ';' << status << ';'
      << nowTimestamp() << ';' << silent_box_id << '\n';
}

// ---------------------------------------------------------------
// usage.csv: tabel channel yang SEDANG dipakai (real-time), hanya
// berisi baris untuk channel yang ON. Begitu channel dimatikan,
// barisnya langsung dihapus dari file -- bukan ditandai OFF.
// Beda dengan logs/ddmmyy.csv yang berupa riwayat (history) lengkap
// IN/OUT -- usage.csv murni snapshot kondisi TERKINI.
//
// Format: channel;nim;nama;waktu_on
//
// Alur:
//  - NIM baru scan  -> channel yang dipilih user di selector
//                       dinyalakan -> baris ditambahkan ke usage.csv.
//  - NIM yg sama scan lagi -> channel yang tercatat jadi miliknya
//                       dimatikan -> barisnya DIHAPUS dari usage.csv.
// ---------------------------------------------------------------
struct UsageEntry {
    std::string nim;
    std::string nama;
    std::string waktu_on;
};

static const std::string kUsagePath = "usage.csv";
static std::unordered_map<int, UsageEntry> g_usage; // key = channel, hanya yang ON

static void saveUsageCSV() {
    std::ofstream f(kUsagePath, std::ios::trunc);
    if (!f.is_open()) return;
    f << "channel;nim;nama;waktu_on\n";
    std::vector<int> chans;
    for (auto& kv : g_usage) chans.push_back(kv.first);
    std::sort(chans.begin(), chans.end());
    for (int ch : chans) {
        auto& u = g_usage[ch];
        f << ch << ';' << u.nim << ';' << u.nama << ';' << u.waktu_on << '\n';
    }
}

// Muat usage.csv yang ada (kalau ada). Baris yang channel-nya di luar
// jangkauan num_channels saat ini (mis. relay diganti) ikut dibuang.
static void loadUsageCSV(int num_channels) {
    g_usage.clear();
    std::ifstream f(kUsagePath);
    if (f.is_open()) {
        std::string line;
        std::getline(f, line); // skip header
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::vector<std::string> col;
            std::stringstream ss(line);
            std::string field;
            while (std::getline(ss, field, ';')) col.push_back(field);
            while (!col.empty() && (col.back() == "\r")) col.pop_back();
            if (col.size() < 2) continue;
            int ch = std::atoi(col[0].c_str());
            if (ch <= 0 || ch > num_channels) continue;
            UsageEntry u;
            u.nim      = col.size() > 1 ? col[1] : "";
            u.nama     = col.size() > 2 ? col[2] : "";
            u.waktu_on = col.size() > 3 ? col[3] : "";
            if (u.nim.empty()) continue;
            g_usage[ch] = u;
        }
    }
    saveUsageCSV();
}

// Cari channel yang sedang aktif dipakai oleh NIM tertentu.
// Return -1 kalau NIM tsb tidak sedang memakai channel manapun.
static int findActiveChannelForNIM(const std::string& nim) {
    for (auto& kv : g_usage) {
        if (kv.second.nim == nim)
            return kv.first;
    }
    return -1;
}

static void setUsageOn(int ch, const std::string& nim, const std::string& nama) {
    UsageEntry u;
    u.nim       = nim;
    u.nama      = nama;
    u.waktu_on  = nowTimestamp();
    g_usage[ch] = u;
    saveUsageCSV();
}

// Channel dimatikan -> baris dihapus total dari usage.csv
static void setUsageOff(int ch) {
    g_usage.erase(ch);
    saveUsageCSV();
}

// ---------------------------------------------------------------
// Rekonsiliasi status channel setelah (re)connect relay:
// Board relay HID clone (16c0:05df) tidak selalu bisa dipercaya saat
// dibaca status-nya dari hardware (lihat catatan di relay_controller.cpp).
// Karena itu, status yang jadi ACUAN adalah catatan kita sendiri di
// usage.csv: channel yang tercatat ON di sana kita nyalakan ulang secara
// eksplisit, sisanya kita pastikan mati. Ini sekaligus membuat kondisi
// fisik relay selalu konsisten dengan data di usage.csv setiap kali
// aplikasi konek/reconnect ke relay.
// ---------------------------------------------------------------
static void reconcileRelayFromUsage(int num_channels) {
    for (int ch = 1; ch <= num_channels; ch++) {
        bool shouldBeOn = g_usage.find(ch) != g_usage.end();
        g_core->setRelay(ch, shouldBeOn);
    }
}

static const char* CSS =
    ".relay-connected    { color: #27ae60; font-weight: bold; }"
    ".relay-disconnected { color: #c0392b; font-weight: bold; }"
    ".relay-scanning     { color: #f39c12; font-weight: bold; }"
    ".channel-btn { min-width: 120px; min-height: 110px; font-size: 20px;"
    "               font-weight: bold; border-radius: 10px; padding: 6px;"
    "               background-image: linear-gradient(rgba(149,165,166,0.78), rgba(149,165,166,0.78)), url(\"silentbox.png\");"
    "               background-size: cover; background-position: center; background-repeat: no-repeat;"
    "               color: #2c3e50; border: 2px solid #7f8c8d; }"
    ".channel-btn:hover { background-image: linear-gradient(rgba(169,179,180,0.78), rgba(169,179,180,0.78)), url(\"silentbox.png\"); }"
    ".channel-selected { background-image: linear-gradient(rgba(41,128,185,0.80), rgba(41,128,185,0.80)), url(\"silentbox.png\");"
    "                    color: #ffffff; border: 2px solid #1c5980; }"
    ".channel-selected:hover { background-image: linear-gradient(rgba(52,145,206,0.80), rgba(52,145,206,0.80)), url(\"silentbox.png\"); }"
    ".channel-active { background-image: linear-gradient(rgba(39,174,96,0.80), rgba(39,174,96,0.80)), url(\"silentbox.png\");"
    "                  color: #ffffff; border: 2px solid #1e8449; }"
    ".channel-btn:disabled { opacity: 1; }";

static void applyCSS() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------
static void appendLog(const std::string& msg) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g_log_buf, &end);
    gtk_text_buffer_insert(g_log_buf, &end, (msg + "\n").c_str(), -1);
    gtk_text_buffer_get_end_iter(g_log_buf, &end);
    GtkTextMark* mark = gtk_text_buffer_create_mark(g_log_buf, nullptr, &end, FALSE);
    gtk_text_view_scroll_mark_onscreen(g_log_view, mark);
    gtk_text_buffer_delete_mark(g_log_buf, mark);
}

// Perbarui warna + label semua tombol channel sesuai status ON/OFF
// terkini DAN channel mana yang sedang dipilih (g_selected_channel):
//   - ON  -> hijau, tidak bisa dipilih (insensitive)
//   - OFF & terpilih -> biru
//   - OFF & tidak terpilih -> abu-abu (default)
static void updateRelayStatus(uint8_t status) {
    g_last_status_cache = status;

    for (int i = 0; i < 8; i++) {
        GtkWidget* btn = g_channel_buttons[i];
        if (!btn) continue;
        int  ch     = i + 1;
        bool inRange = i < g_num_channels;
        bool on      = inRange && ((status >> i) & 1);

        GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
        gtk_style_context_remove_class(ctx, "channel-active");
        gtk_style_context_remove_class(ctx, "channel-selected");

        if (on) {
            gtk_style_context_add_class(ctx, "channel-active");
            gtk_widget_set_sensitive(btn, FALSE); // aktif -> tidak bisa diselect
            if (g_selected_channel == ch) g_selected_channel = 0;
        } else {
            gtk_widget_set_sensitive(btn, inRange);
            if (ch == g_selected_channel)
                gtk_style_context_add_class(ctx, "channel-selected");
        }

        if (g_channel_lbl[i]) {
            auto it = g_usage.find(ch);
            std::string text = "CH" + std::to_string(ch);
            if (inRange && it != g_usage.end())
                text += "\n" + it->second.nim;
            else
                text += "\n-";
            gtk_label_set_text(GTK_LABEL(g_channel_lbl[i]), text.c_str());

            std::string tip = on
                ? ("CH" + std::to_string(ch) + " AKTIF" +
                   (it != g_usage.end() ? (" - " + it->second.nim + " (" + it->second.nama + ")") : ""))
                : ("CH" + std::to_string(ch) + " kosong, ketuk untuk pilih");
            gtk_widget_set_tooltip_text(btn, tip.c_str());
        }
    }
}

// Diklik saat user menekan salah satu tombol channel (hanya channel OFF
// yang sensitif/bisa diklik -- channel ON otomatis insensitive).
static void onChannelBtnClicked(GtkButton*, gpointer user_data) {
    g_selected_channel = GPOINTER_TO_INT(user_data);
    updateRelayStatus(g_last_status_cache);
}

// Bangun ulang grid tombol channel sesuai jumlah channel hasil auto-scan
// relay. GtkFlowBox otomatis reflow responsif sesuai lebar window --
// diset maksimal 4 tombol per baris supaya di layar lebar/fullscreen
// langsung terlihat 4 kolom sekaligus.
static void populateChannelSelector(int num_ch) {
    if (!g_channel_selector) return;

    GList* children = gtk_container_get_children(GTK_CONTAINER(g_channel_selector));
    for (GList* l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
    for (int i = 0; i < 8; i++) { g_channel_buttons[i] = nullptr; g_channel_lbl[i] = nullptr; }

    g_selected_channel = (num_ch > 0) ? 1 : 0;

    for (int ch = 1; ch <= num_ch; ch++) {
        GtkWidget* btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "channel-btn");

        GtkWidget* lbl = gtk_label_new(("CH" + std::to_string(ch)).c_str());
        gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
        gtk_container_add(GTK_CONTAINER(btn), lbl);

        g_signal_connect(btn, "clicked", G_CALLBACK(onChannelBtnClicked), GINT_TO_POINTER(ch));

        gtk_flow_box_insert(GTK_FLOW_BOX(g_channel_selector), btn, -1);
        g_channel_buttons[ch - 1] = btn;
        g_channel_lbl[ch - 1]     = lbl;
    }

    gtk_widget_set_sensitive(g_channel_selector, num_ch > 0);
    gtk_widget_show_all(g_channel_selector);
    updateRelayStatus(g_last_status_cache);
}

static void setConnectedUI(const std::string& devname, int num_ch) {
    g_num_channels = num_ch;
    gtk_label_set_text(GTK_LABEL(g_lbl_status), "● Terhubung");
    gtk_label_set_text(GTK_LABEL(g_lbl_devname), devname.c_str());
    GtkStyleContext* ctx = gtk_widget_get_style_context(g_lbl_status);
    gtk_style_context_remove_class(ctx, "relay-disconnected");
    gtk_style_context_remove_class(ctx, "relay-scanning");
    gtk_style_context_add_class(ctx, "relay-connected");

    loadUsageCSV(num_ch);
    populateChannelSelector(num_ch);
}

static void setDisconnectedUI() {
    g_num_channels = 0;
    gtk_label_set_text(GTK_LABEL(g_lbl_status), "⟳ Mencari relay...");
    gtk_label_set_text(GTK_LABEL(g_lbl_devname), "");
    GtkStyleContext* ctx = gtk_widget_get_style_context(g_lbl_status);
    gtk_style_context_remove_class(ctx, "relay-connected");
    gtk_style_context_remove_class(ctx, "relay-disconnected");
    gtk_style_context_add_class(ctx, "relay-scanning");
    updateRelayStatus(0);
    populateChannelSelector(0);
}

static void updateUSBList() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(g_usb_list));
    for (GList* l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    auto devs = g_core->getUSBDevices();
    for (auto& d : devs) {
        std::string label = d.product.empty()
                            ? (d.vid + ":" + d.pid)
                            : (d.vid + ":" + d.pid + "  " + d.product);
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* lbl = gtk_label_new(label.c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_container_add(GTK_CONTAINER(row), lbl);
        gtk_list_box_insert(GTK_LIST_BOX(g_usb_list), row, -1);
    }
    gtk_widget_show_all(g_usb_list);
}

// Forward declaration
static void startScanLoop();
static void setDisconnectedUI();

// ---------------------------------------------------------------
// Polling status + health-check tiap 1 detik
// Jika relay tidak merespons 3x berturut, anggap terputus
// ---------------------------------------------------------------
static guint g_status_timer  = 0;
static int   g_fail_count    = 0;
static const int MAX_FAILS   = 3;

static gboolean onStatusPoll(gpointer) {
    if (!g_core->isRelayConnected()) return G_SOURCE_REMOVE;

    uint8_t status = g_core->getRelayStatus();

    // getRelayStatus mengembalikan 0 saat read timeout/gagal
    // Tapi 0 juga valid (semua relay OFF), jadi kita cek lewat hid_write
    // Cara terbaik: panggil disconnectRelay dari AppCore jika gagal tulis
    // Di sini cukup update status jika relay masih open
    if (!g_core->isRelayConnected()) {
        // AppCore sudah deteksi disconnect via setRelay/setAll yang gagal
        // atau via udev; update UI
        setDisconnectedUI();
        startScanLoop();
        return G_SOURCE_REMOVE;
    }

    g_fail_count = 0;
    updateRelayStatus(status);
    return G_SOURCE_CONTINUE;
}

static void startStatusPoll() {
    if (g_status_timer != 0) return;
    g_fail_count  = 0;
    g_status_timer = g_timeout_add(1000, onStatusPoll, nullptr);
}

static void stopStatusPoll() {
    if (g_status_timer != 0) {
        g_source_remove(g_status_timer);
        g_status_timer = 0;
    }
}

// ---------------------------------------------------------------
// Auto-connect + scan loop
// ---------------------------------------------------------------
static guint g_scan_timer = 0;

static void tryAutoConnect() {
    if (g_core->isRelayConnected()) return;
    auto relays = g_core->getRelayDevices();
    if (relays.empty()) {
        appendLog("[~] Tidak ada relay, scan ulang...");
        return;
    }
    appendLog("[*] Relay ditemukan: " + relays[0].path +
              "  serial=" + relays[0].serial +
              "  ch=" + std::to_string(relays[0].num_channels));
    if (g_core->connectRelay(relays[0].path))
        appendLog("[+] Auto-connect berhasil: " + relays[0].serial);
    else
        appendLog("[!] Gagal connect: " + relays[0].path);
}

static gboolean onScanTick(gpointer) {
    if (!g_core->isRelayConnected()) {
        tryAutoConnect();
        return G_SOURCE_CONTINUE;
    }
    g_scan_timer = 0;
    return G_SOURCE_REMOVE;
}

static void startScanLoop() {
    setDisconnectedUI();
    if (g_scan_timer != 0) return;
    g_scan_timer = g_timeout_add(5000, onScanTick, nullptr);
}

static void stopScanLoop() {
    if (g_scan_timer != 0) {
        g_source_remove(g_scan_timer);
        g_scan_timer = 0;
    }
}

// ---------------------------------------------------------------
// Event callback dari AppCore
// ---------------------------------------------------------------
struct EventData { AppEvent ev; };

static gboolean onEventIdle(gpointer user_data) {
    auto* data = static_cast<EventData*>(user_data);
    auto& ev   = data->ev;

    switch (ev.type) {
    case AppEvent::Type::USB_ADDED:
        updateUSBList();
        appendLog("[USB] " + ev.message);
        if (!g_core->isRelayConnected())
            tryAutoConnect();
        break;

    case AppEvent::Type::USB_REMOVED:
        updateUSBList();
        appendLog("[USB] " + ev.message);
        break;

    case AppEvent::Type::RELAY_CHANGED:
        // Update status langsung dari event (bukan tunggu polling)
        updateRelayStatus(ev.relay_status);
        appendLog("[Relay] " + ev.message +
                  "  (status=0x" + [&]{
                      char buf[8]; snprintf(buf,8,"%02X",ev.relay_status);
                      return std::string(buf);
                  }() + ")");
        break;

    case AppEvent::Type::RELAY_CONNECTED: {
        stopScanLoop();
        RelayInfo info = g_core->getRelayInfo();
        std::string name = info.serial.empty() ? info.path : info.serial;
        setConnectedUI(name, info.num_channels); // ini juga memanggil loadUsageCSV()
        appendLog("[+] Relay terhubung: " + name +
                  "  ch=" + std::to_string(info.num_channels) +
                  "  path=" + info.path);

        // Diagnostik saja (log metode baca yg direspons device untuk
        // deteksi disconnect) -- TIDAK dipakai untuk menentukan status
        // channel, karena tidak reliable di semua board (lihat catatan
        // di relay_controller.cpp).
        auto scanLogs = g_core->scanRelayStatus();
        for (auto& l : scanLogs) appendLog(l);

        // Status channel yang benar-benar dipakai berasal dari usage.csv:
        // nyalakan ulang channel yang tercatat ON, pastikan sisanya OFF.
        reconcileRelayFromUsage(info.num_channels);
        appendLog("[*] Status channel direkonsiliasi dari usage.csv (" +
                  std::to_string(g_usage.size()) + " channel aktif).");
        updateRelayStatus(g_core->getRelayStatus());

        startStatusPoll();
        break;
    }

    case AppEvent::Type::RELAY_DISCONNECTED:
        stopStatusPoll();
        setDisconnectedUI();           // <-- perbaikan: update label sekarang
        appendLog("[-] Relay terputus. Memulai scan ulang...");
        startScanLoop();
        break;

    case AppEvent::Type::LOG:
        appendLog(ev.message);
        break;
    }

    delete data;
    return G_SOURCE_REMOVE;
}

static void onAppEvent(const AppEvent& ev) {
    auto* data = new EventData{ ev };
    g_idle_add(onEventIdle, data);
}

// ---------------------------------------------------------------
// Signal handlers channel
// ---------------------------------------------------------------
// Tombol darurat: matikan semua channel + tandai OFF semua baris usage.csv
static void onAllOffClicked(GtkButton*, gpointer) {
    if (!g_core->setAll(false)) {
        appendLog("[!] Relay tidak terhubung.");
        return;
    }
    for (int ch = 1; ch <= g_num_channels; ch++)
        setUsageOff(ch);
    updateRelayStatus(g_core->getRelayStatus());
    appendLog("[Relay] Perintah darurat: Semua OFF");
}

// Ambil nomor channel yang sedang dipilih di grid tombol (1-based).
// Return -1 kalau tidak ada channel terpilih / relay belum konek.
static int getSelectedChannel() {
    return (g_selected_channel > 0) ? g_selected_channel : -1;
}

// ---------------------------------------------------------------
// NIM search + alur ON/OFF otomatis berbasis usage.csv + log CSV
//
//  - Kalau NIM ini SEDANG memakai sebuah channel (tercatat ON di
//    usage.csv) -> scan ulang berarti "selesai pakai": channel
//    dimatikan & baris usage.csv untuk channel itu di-set OFF.
//  - Kalau NIM ini BELUM memakai channel manapun -> scan berarti
//    "mulai pakai": channel yang dipilih di selector dinyalakan &
//    dicatat ON di usage.csv (channel yang sedang dipakai NIM lain
//    tidak akan ditimpa).
// ---------------------------------------------------------------
static void processScanInput(const std::string& raw) {
    // Split by '+', ambil index 0 (barcode/RFID sering nambah suffix)
    std::string nim = raw.substr(0, raw.find('+'));
    while (!nim.empty() && (nim.front() == ' ' || nim.front() == '\r'))
        nim.erase(nim.begin());
    while (!nim.empty() && (nim.back() == ' ' || nim.back() == '\r'))
        nim.pop_back();

    if (nim.empty()) {
        appendLog("[NIM] Input kosong.");
        return;
    }

    appendLog("[NIM] Cari: " + nim);

    std::string nama;
    if (!findNIM(nim, nama)) {
        appendLog("[NIM] Tidak ditemukan: " + nim);
        return;
    }

    appendLog("[NIM] Ditemukan: " + nim + "  (" + nama + ")");

    if (!g_core->isRelayConnected()) {
        appendLog("[!] Relay tidak terhubung.");
        return;
    }

    int activeCh = findActiveChannelForNIM(nim);

    if (activeCh > 0) {
        // NIM ini sedang pakai channel -> matikan (scan kedua = keluar)
        std::string label = "CH" + std::to_string(activeCh);
        if (!g_core->setRelay(activeCh, false)) {
            appendLog("[!] Gagal mematikan " + label + ".");
            return;
        }
        setUsageOff(activeCh);
        updateRelayStatus(g_core->getRelayStatus());
        appendLog("[Relay] " + label + " -> OFF (selesai, " + nim + ")");
        appendScanLog(nim, nama, "OUT", activeCh);
        return;
    }

    // NIM belum pakai channel manapun -> nyalakan channel dari selector
    int ch = getSelectedChannel();
    if (ch <= 0) {
        appendLog("[!] Pilih channel dulu di selector sebelum scan.");
        return;
    }

    auto it = g_usage.find(ch);
    if (it != g_usage.end() && it->second.nim != nim) {
        appendLog("[!] CH" + std::to_string(ch) + " sedang dipakai NIM " +
                   it->second.nim + " (" + it->second.nama + "). Pilih channel lain.");
        return;
    }

    std::string label = "CH" + std::to_string(ch);
    if (!g_core->setRelay(ch, true)) {
        appendLog("[!] Gagal menyalakan " + label + ".");
        return;
    }
    setUsageOn(ch, nim, nama);
    updateRelayStatus(g_core->getRelayStatus());
    appendLog("[Relay] " + label + " -> ON (" + nim + " - " + nama + ")");
    appendScanLog(nim, nama, "IN", ch);
}

static void onNIMActivate(GtkEntry* entry, gpointer) {
    const gchar* raw = gtk_entry_get_text(entry);
    processScanInput(std::string(raw));
    gtk_entry_set_text(entry, "");
}

// ---------------------------------------------------------------
// Capture keystroke scanner USB (HID keyboard) di level window,
// supaya scan tetap kebaca walau fokus sedang bukan di textbox
// NIM. Ini TIDAK menangkap ketikan di luar aplikasi ini (bukan
// keylogger sistem) — hanya event keyboard yang ditujukan ke
// window aplikasi ini sendiri.
// ---------------------------------------------------------------
static std::string g_scan_buffer;

static gboolean onWindowKeyPress(GtkWidget*, GdkEventKey* event, gpointer) {
    // Kalau textbox NIM memang sedang fokus, biarkan GTK yang
    // menangani seperti biasa (termasuk signal "activate").
    if (gtk_widget_has_focus(g_nim_entry))
        return FALSE;

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        std::string text = g_scan_buffer;
        g_scan_buffer.clear();
        gtk_entry_set_text(GTK_ENTRY(g_nim_entry), "");
        processScanInput(text);
        return TRUE;
    }

    guint32 uc = gdk_keyval_to_unicode(event->keyval);
    if (uc >= 0x20 && uc < 0x7F) { // karakter cetak ASCII saja
        g_scan_buffer += static_cast<char>(uc);
        if (g_scan_buffer.size() > 128) // jaga-jaga input nyasar
            g_scan_buffer.erase(0, g_scan_buffer.size() - 128);
        gtk_entry_set_text(GTK_ENTRY(g_nim_entry), g_scan_buffer.c_str());
        gtk_editable_set_position(GTK_EDITABLE(g_nim_entry), -1);
        return TRUE;
    }

    return FALSE;
}

// ---------------------------------------------------------------
// Bangun UI
// ---------------------------------------------------------------
static GtkWidget* buildNIMPanel() {
    GtkWidget* frame = gtk_frame_new("Cari NIM");
    GtkWidget* hbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);
    gtk_container_add(GTK_CONTAINER(frame), hbox);

    // Channel yang akan dinyalakan sekarang dipilih langsung di grid
    // tombol pada panel "Kontrol Relay" di atas (tap tombol channel yang
    // abu-abu/kosong -> jadi biru/terpilih), bukan di sini lagi.
    GtkWidget* lbl = gtk_label_new("NIM / Scan:");
    g_nim_entry    = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_nim_entry), "Scan atau ketik NIM lalu Enter...");
    gtk_widget_set_hexpand(g_nim_entry, TRUE);

    g_signal_connect(g_nim_entry, "activate", G_CALLBACK(onNIMActivate), nullptr);

    gtk_box_pack_start(GTK_BOX(hbox), lbl,         FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), g_nim_entry, TRUE,  TRUE,  0);
    return frame;
}

static GtkWidget* buildRelayPanel() {
    GtkWidget* frame = gtk_frame_new("Kontrol Relay");
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, TRUE);
    GtkWidget* vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_vexpand(vbox, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    // Status bar
    GtkWidget* hstatus = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_lbl_status  = gtk_label_new("⟳ Mencari relay...");
    g_lbl_devname = gtk_label_new("");
    {
        GtkStyleContext* ctx = gtk_widget_get_style_context(g_lbl_status);
        gtk_style_context_add_class(ctx, "relay-scanning");
    }
    gtk_box_pack_start(GTK_BOX(hstatus), g_lbl_status,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hstatus), g_lbl_devname, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(vbox), hstatus, FALSE, FALSE, 0);

    // Grid tombol channel besar & responsif (GtkFlowBox):
    //   - dobel fungsi sebagai indikator status (warna) dan sebagai
    //     selector channel untuk scan NIM berikutnya (tap untuk pilih).
    //   - hijau = ON/aktif (tidak bisa dipilih), biru = terpilih,
    //     abu-abu = OFF/kosong.
    //   - jumlah tombol yang muncul otomatis menyesuaikan jumlah channel
    //     hasil auto-scan relay (lihat populateChannelSelector()).
    //   - homogeneous + max 4 per baris supaya di layar lebar/fullscreen
    //     langsung terlihat 4 kolom, dan tetap reflow rapi di layar sempit.
    g_channel_selector = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_channel_selector), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(g_channel_selector), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_channel_selector), 4);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(g_channel_selector), 1);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(g_channel_selector), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(g_channel_selector), 8);
    gtk_widget_set_sensitive(g_channel_selector, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), g_channel_selector, TRUE, TRUE, 0);

    // Tombol darurat: tetap disediakan untuk mematikan semua relay
    // manual (mis. kondisi macet/darurat), BUKAN untuk kontrol normal.
    GtkWidget* hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* btn_all_off = gtk_button_new_with_label("Darurat: Matikan Semua");
    g_signal_connect(btn_all_off, "clicked", G_CALLBACK(onAllOffClicked), nullptr);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_all_off, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbtn, FALSE, FALSE, 4);

    return frame;
}

static GtkWidget* buildUSBPanel() {
    GtkWidget* frame  = gtk_frame_new("USB Devices Terdeteksi");
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 120);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    g_usb_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_usb_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_usb_list);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, TRUE);
    return frame;
}

static GtkWidget* buildLogPanel() {
    GtkWidget* frame    = gtk_frame_new("Log");
    GtkWidget* scroll   = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);

    g_log_buf  = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    g_log_view = GTK_TEXT_VIEW(textview);

    gtk_container_add(GTK_CONTAINER(scroll), textview);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 100);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, TRUE);
    return frame;
}

// Panel bawah: Log (kiri) dipisah dari USB Devices Terdeteksi (kanan)
// pakai GtkPaned horizontal -- user bisa drag-resize sendiri pembagian
// lebarnya, dan keduanya auto-scale (hexpand/vexpand TRUE) mengikuti
// ukuran window, jadi tetap responsive baik saat window dilebarkan/
// dipersempit maupun saat di-maximize/fullscreen.
static GtkWidget* buildBottomPanel() {
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);

    GtkWidget* logPanel = buildLogPanel();
    GtkWidget* usbPanel = buildUSBPanel();

    // resize=TRUE -> ikut membesar/mengecil saat paned di-resize
    // shrink=TRUE -> boleh mengecil di bawah ukuran natural saat window
    //                dipersempit, supaya tidak memaksa window minimum
    //                jadi terlalu lebar/tinggi (tetap responsive)
    gtk_paned_pack1(GTK_PANED(paned), logPanel, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(paned), usbPanel, TRUE, TRUE);

    return paned;
}

void run_gui(AppCore& core, int argc, char* argv[]) {
    g_core = &core;

    gtk_init(&argc, &argv);
    applyCSS();

    core.setEventCallback(onAppEvent);

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), "USB Relay Auto-Control");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 800, 600);
    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    // Tangkap keystroke scanner USB walau fokus bukan di textbox NIM
    g_signal_connect(g_window, "key-press-event", G_CALLBACK(onWindowKeyPress), nullptr);

    // Layout utama dibagi proporsional 50/50 secara vertikal pakai
    // GtkPaned: panel atas (Kontrol Relay / grid tombol channel)
    // mendapat 50% tinggi window, panel bawah (NIM + Log + USB Devices)
    // mendapat 50% sisanya -- supaya tombol channel tidak lagi kekecilan
    // (dulu cuma natural-size) dan tetap bisa di-resize manual oleh user
    // via drag splitter kalau mau.
    GtkWidget* main_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_container_set_border_width(GTK_CONTAINER(main_paned), 10);
    gtk_widget_set_hexpand(main_paned, TRUE);
    gtk_widget_set_vexpand(main_paned, TRUE);
    gtk_container_add(GTK_CONTAINER(g_window), main_paned);

    // Panel atas: grid tombol channel, vexpand TRUE supaya benar-benar
    // mengisi porsi 50% tinggi yang dialokasikan (bukan cuma natural-size).
    GtkWidget* relayPanel = buildRelayPanel();

    // Panel bawah: NIM (natural-size, tetap di atas) + Log|USB Devices
    // (mengambil semua sisa ruang di dalam panel bawah).
    GtkWidget* bottom_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_vexpand(bottom_vbox, TRUE);
    gtk_box_pack_start(GTK_BOX(bottom_vbox), buildNIMPanel(), FALSE, FALSE, 0);

    // Log (kiri) <-> USB Devices Terdeteksi (kanan), dipisah GtkPaned
    // horizontal (lihat buildBottomPanel()) -- user bisa drag-resize
    // sendiri pembagian lebarnya.
    GtkWidget* bottomPanel = buildBottomPanel();
    gtk_box_pack_start(GTK_BOX(bottom_vbox), bottomPanel, TRUE, TRUE, 0);

    // resize=TRUE -> kedua panel ikut membesar/mengecil saat window
    //                di-resize, menjaga proporsi 50/50.
    // shrink=TRUE -> boleh mengecil di bawah ukuran natural saat window
    //                dipersempit, supaya tidak memaksa window minimum
    //                jadi terlalu tinggi (tetap responsive).
    gtk_paned_pack1(GTK_PANED(main_paned), relayPanel, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(main_paned), bottom_vbox, TRUE, TRUE);

    gtk_widget_show_all(g_window);

    // Posisi awal splitter: 50% tinggi window untuk panel atas (channel),
    // dan 50% lebar window untuk splitter Log|USB di panel bawah --
    // dihitung dari ukuran window aktual biar rasionya konsisten di
    // berbagai ukuran layar.
    {
        int win_w = 0, win_h = 0;
        gtk_window_get_size(GTK_WINDOW(g_window), &win_w, &win_h);
        if (win_w <= 1) win_w = 800;
        if (win_h <= 1) win_h = 600;
        gtk_paned_set_position(GTK_PANED(main_paned), (int)(win_h * 0.5));
        gtk_paned_set_position(GTK_PANED(bottomPanel), (int)(win_w * 0.5));
    }

    updateUSBList();
    appendLog("[*] USB Relay Auto-Control dimulai.");
    loadCSV("data.csv");
    appendLog("[*] CSV dimuat: " + std::to_string(g_nim_map.size()) + " NIM.");

    // Coba langsung, kalau gagal mulai scan loop 5 detik
    tryAutoConnect();
    if (!core.isRelayConnected())
        startScanLoop();

    gtk_main();

    stopScanLoop();
    stopStatusPoll();
}