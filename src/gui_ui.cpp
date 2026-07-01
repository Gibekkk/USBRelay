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
#include <unistd.h>
// ---------------------------------------------------------------
// Widget globals
// ---------------------------------------------------------------
static GtkWidget*     g_window          = nullptr;
static GtkWidget*     g_lbl_status      = nullptr;
static GtkWidget*     g_lbl_devname     = nullptr;
static GtkWidget*     g_ch_row[8]       = { nullptr }; // baris channel (untuk show/hide)
static GtkWidget*     g_ch_indicators[8];
static GtkWidget*     g_ch_user_lbl[8];                // label NIM/nama pemakai channel
static GtkWidget*     g_usb_list        = nullptr;
static GtkTextBuffer* g_log_buf         = nullptr;
static GtkTextView*   g_log_view        = nullptr;
static AppCore*       g_core            = nullptr;
static int            g_num_channels    = 0;
static GtkWidget*     g_nim_entry       = nullptr;
static GtkWidget*     g_channel_selector = nullptr;      // grid berisi tombol channel besar, pilih channel sebelum scan
static GtkWidget*     g_channel_buttons[8] = { nullptr }; // satu tombol per channel (index 0 = CH1)
static int            g_selected_channel   = 0;           // channel yang sedang dipilih (0 = belum ada)

#include <sys/stat.h>

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
    mkdir(kLogDir.c_str(), 0755);

    std::string path = todayLogPath();
    bool isNew = access(path.c_str(), F_OK) != 0;

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
    ".indicator-on  { background-color: #2ecc71; color: white; border-radius:8px;"
    "                 padding:4px 12px; font-weight:bold; }"
    ".indicator-off { background-color: #e74c3c; color: white; border-radius:8px;"
    "                 padding:4px 12px; font-weight:bold; }"
    ".relay-connected    { color: #27ae60; font-weight: bold; }"
    ".relay-disconnected { color: #c0392b; font-weight: bold; }"
    ".relay-scanning     { color: #f39c12; font-weight: bold; }"
    ".channel-btn { min-width: 72px; min-height: 60px; font-size: 20px;"
    "               font-weight: bold; border-radius: 10px;"
    "               background-color: #ecf0f1; color: #2c3e50;"
    "               border: 2px solid #bdc3c7; }"
    ".channel-btn:hover { background-color: #dfe6e9; }"
    ".channel-btn-selected { background-color: #2980b9; color: #ffffff;"
    "                        border: 2px solid #1c5980; }"
    ".channel-btn-selected:hover { background-color: #2980b9; }";

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

static void setIndicator(int ch_idx, bool on) {
    GtkWidget* lbl = g_ch_indicators[ch_idx];
    gtk_label_set_text(GTK_LABEL(lbl), on ? "ON" : "OFF");
    GtkStyleContext* ctx = gtk_widget_get_style_context(lbl);
    if (on) {
        gtk_style_context_remove_class(ctx, "indicator-off");
        gtk_style_context_add_class(ctx, "indicator-on");
    } else {
        gtk_style_context_remove_class(ctx, "indicator-on");
        gtk_style_context_add_class(ctx, "indicator-off");
    }
}

static void updateRelayStatus(uint8_t status) {
    for (int i = 0; i < 8; i++) {
        bool active = (g_num_channels > 0) && (i < g_num_channels);
        bool on     = active && ((status >> i) & 1);
        setIndicator(i, on);

        if (g_ch_user_lbl[i]) {
            int ch = i + 1;
            auto it = g_usage.find(ch);
            std::string text = "-";
            if (active && it != g_usage.end())
                text = it->second.nim + " - " + it->second.nama;
            gtk_label_set_text(GTK_LABEL(g_ch_user_lbl[i]), text.c_str());
        }
    }
}

// Perbarui tampilan (warna) semua tombol channel sesuai channel yang
// sedang terpilih di g_selected_channel.
static void updateChannelButtonStyles() {
    for (int i = 0; i < 8; i++) {
        GtkWidget* btn = g_channel_buttons[i];
        if (!btn) continue;
        GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
        if (i + 1 == g_selected_channel)
            gtk_style_context_add_class(ctx, "channel-btn-selected");
        else
            gtk_style_context_remove_class(ctx, "channel-btn-selected");
    }
}

// Diklik saat user menekan salah satu tombol channel besar
static void onChannelBtnClicked(GtkButton*, gpointer user_data) {
    g_selected_channel = GPOINTER_TO_INT(user_data);
    updateChannelButtonStyles();
}

// Bangun ulang grid tombol channel sesuai jumlah channel hasil auto-scan
// relay. Menggantikan dropdown lama supaya lebih mudah ditekan (mis. di
// layar sentuh) -- satu tombol besar per channel, channel terpilih
// ditandai warna biru.
static void populateChannelSelector(int num_ch) {
    if (!g_channel_selector) return;

    GList* children = gtk_container_get_children(GTK_CONTAINER(g_channel_selector));
    for (GList* l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
    for (int i = 0; i < 8; i++) g_channel_buttons[i] = nullptr;

    g_selected_channel = (num_ch > 0) ? 1 : 0;

    const int kCols = 4; // maksimal 4 tombol per baris, cukup lega untuk ditekan
    for (int ch = 1; ch <= num_ch; ch++) {
        std::string label = "CH" + std::to_string(ch);
        GtkWidget* btn = gtk_button_new_with_label(label.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "channel-btn");
        g_signal_connect(btn, "clicked", G_CALLBACK(onChannelBtnClicked), GINT_TO_POINTER(ch));

        int idx = ch - 1;
        gtk_grid_attach(GTK_GRID(g_channel_selector), btn, idx % kCols, idx / kCols, 1, 1);
        g_channel_buttons[idx] = btn;
    }

    gtk_widget_set_sensitive(g_channel_selector, num_ch > 0);
    gtk_widget_show_all(g_channel_selector);
    updateChannelButtonStyles();
}

// Tampilkan hanya baris channel 1..num_ch (auto-scan jumlah channel relay),
// sembunyikan sisanya supaya UI mengikuti hardware yang benar-benar ada.
static void showChannelRows(int num_ch) {
    for (int i = 0; i < 8; i++) {
        bool visible = i < num_ch;
        if (g_ch_row[i])        gtk_widget_set_visible(g_ch_row[i],        visible);
        if (g_ch_indicators[i]) gtk_widget_set_visible(g_ch_indicators[i], visible);
        if (g_ch_user_lbl[i])   gtk_widget_set_visible(g_ch_user_lbl[i],   visible);
    }
}

static void setConnectedUI(const std::string& devname, int num_ch) {
    g_num_channels = num_ch;
    gtk_label_set_text(GTK_LABEL(g_lbl_status), "● Terhubung");
    gtk_label_set_text(GTK_LABEL(g_lbl_devname), devname.c_str());
    GtkStyleContext* ctx = gtk_widget_get_style_context(g_lbl_status);
    gtk_style_context_remove_class(ctx, "relay-disconnected");
    gtk_style_context_remove_class(ctx, "relay-scanning");
    gtk_style_context_add_class(ctx, "relay-connected");

    showChannelRows(num_ch);
    populateChannelSelector(num_ch);
    loadUsageCSV(num_ch);
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
    GtkWidget* vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    // Baris channel: grid tombol besar, dipilih SEBELUM scan NIM baru,
    // menentukan channel mana yang akan dinyalakan. Isinya diisi
    // otomatis sesuai jumlah channel hasil auto-scan relay (lihat
    // populateChannelSelector()).
    GtkWidget* hbox_ch = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lbl_ch  = gtk_label_new("Channel:");
    g_channel_selector = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g_channel_selector), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g_channel_selector), 6);
    gtk_widget_set_sensitive(g_channel_selector, FALSE);

    gtk_box_pack_start(GTK_BOX(hbox_ch), lbl_ch,             FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_ch), g_channel_selector, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_ch, FALSE, FALSE, 0);

    // Baris NIM
    GtkWidget* hbox_nim = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* lbl      = gtk_label_new("NIM / Scan:");
    g_nim_entry         = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_nim_entry), "Scan atau ketik NIM lalu Enter...");
    gtk_widget_set_hexpand(g_nim_entry, TRUE);

    g_signal_connect(g_nim_entry, "activate", G_CALLBACK(onNIMActivate), nullptr);

    gtk_box_pack_start(GTK_BOX(hbox_nim), lbl,         FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_nim), g_nim_entry, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_nim, FALSE, FALSE, 0);

    return frame;
}

static GtkWidget* buildRelayPanel() {
    GtkWidget* frame = gtk_frame_new("Kontrol Relay");
    GtkWidget* vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
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

    // Grid channel: tidak ada lagi tombol ON/OFF manual per-channel.
    // Kontrol ON/OFF sekarang lewat panel "Cari NIM" (pilih channel di
    // selector lalu scan NIM). Grid ini murni indikator status +
    // siapa yang sedang memakai channel tsb.
    // Baris channel yang ditampilkan otomatis menyesuaikan jumlah
    // channel hasil auto-scan relay (lihat showChannelRows()).
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("CH"),      0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Status"),  1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Pemakai"), 2, 0, 1, 1);

    for (int i = 0; i < 8; i++) {
        int ch = i + 1;
        GtkWidget* lbl_ch = gtk_label_new(("CH" + std::to_string(ch)).c_str());
        g_ch_indicators[i] = gtk_label_new("OFF");
        g_ch_user_lbl[i]   = gtk_label_new("-");
        gtk_label_set_xalign(GTK_LABEL(g_ch_user_lbl[i]), 0.0f);

        GtkStyleContext* ctx = gtk_widget_get_style_context(g_ch_indicators[i]);
        gtk_style_context_add_class(ctx, "indicator-off");

        gtk_grid_attach(GTK_GRID(grid), lbl_ch,             0, ch, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), g_ch_indicators[i], 1, ch, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), g_ch_user_lbl[i],   2, ch, 1, 1);

        // g_ch_row menandai baris mana yang harus di-show/hide sesuai
        // jumlah channel yang benar-benar terdeteksi di relay
        g_ch_row[i] = lbl_ch;
        gtk_widget_set_no_show_all(lbl_ch,             TRUE);
        gtk_widget_set_no_show_all(g_ch_indicators[i], TRUE);
        gtk_widget_set_no_show_all(g_ch_user_lbl[i],   TRUE);
        gtk_widget_hide(lbl_ch);
        gtk_widget_hide(g_ch_indicators[i]);
        gtk_widget_hide(g_ch_user_lbl[i]);
    }
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

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
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 200);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_usb_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_usb_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_usb_list);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    return frame;
}

static GtkWidget* buildLogPanel() {
    GtkWidget* frame    = gtk_frame_new("Log");
    GtkWidget* scroll   = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget* textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_size_request(textview, -1, 150);

    g_log_buf  = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    g_log_view = GTK_TEXT_VIEW(textview);

    gtk_container_add(GTK_CONTAINER(scroll), textview);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 150);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    return frame;
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

    GtkWidget* vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_main), 10);
    gtk_container_add(GTK_CONTAINER(g_window), vbox_main);

    GtkWidget* hbox_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hbox_top), buildRelayPanel(), TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(hbox_top), buildUSBPanel(),   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_main), hbox_top,         TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(vbox_main), buildNIMPanel(),  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_main), buildLogPanel(),  FALSE, FALSE, 0);

    gtk_widget_show_all(g_window);

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