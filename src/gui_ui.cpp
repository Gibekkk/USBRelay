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
#include <unistd.h>
// ---------------------------------------------------------------
// Widget globals
// ---------------------------------------------------------------
static GtkWidget*     g_window        = nullptr;
static GtkWidget*     g_lbl_status    = nullptr;
static GtkWidget*     g_lbl_devname   = nullptr;
static GtkWidget*     g_ch_buttons[8][2];
static GtkWidget*     g_ch_indicators[8];
static GtkWidget*     g_usb_list      = nullptr;
static GtkTextBuffer* g_log_buf       = nullptr;
static GtkTextView*   g_log_view      = nullptr;
static AppCore*       g_core          = nullptr;
static int            g_num_channels  = 0;
static GtkWidget*     g_nim_entry     = nullptr;

// ---------------------------------------------------------------
// Channel relay yang di-trigger saat scan NIM berhasil.
// Sekarang cuma CH1, tapi tinggal tambah angka di sini kalau
// nanti mau menyalakan lebih dari 1 channel sekaligus per scan,
// contoh: { 1, 2 }
// ---------------------------------------------------------------
static std::vector<int> g_scan_channels = { 1 };

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

static const char* CSS =
    ".indicator-on  { background-color: #2ecc71; color: white; border-radius:8px;"
    "                 padding:4px 12px; font-weight:bold; }"
    ".indicator-off { background-color: #e74c3c; color: white; border-radius:8px;"
    "                 padding:4px 12px; font-weight:bold; }"
    ".relay-connected    { color: #27ae60; font-weight: bold; }"
    ".relay-disconnected { color: #c0392b; font-weight: bold; }"
    ".relay-scanning     { color: #f39c12; font-weight: bold; }";

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
    // Tampilkan status semua 8 channel; channel di luar num_channels tetap OFF
    for (int i = 0; i < 8; i++) {
        bool active = (g_num_channels > 0) && (i < g_num_channels);
        bool on     = active && ((status >> i) & 1);
        setIndicator(i, on);
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
        setConnectedUI(name, info.num_channels);
        appendLog("[+] Relay terhubung: " + name +
                  "  ch=" + std::to_string(info.num_channels) +
                  "  path=" + info.path);

        // Scan semua metode baca, log hasilnya, update status UI
        auto scanLogs = g_core->scanRelayStatus();
        for (auto& l : scanLogs) appendLog(l);
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
static void onAllOnClicked(GtkButton*, gpointer) {
    if (!g_core->setAll(true))
        appendLog("[!] Relay tidak terhubung.");
    else
        appendLog("[Relay] Perintah: Semua ON");
}

static void onAllOffClicked(GtkButton*, gpointer) {
    if (!g_core->setAll(false))
        appendLog("[!] Relay tidak terhubung.");
    else
        appendLog("[Relay] Perintah: Semua OFF");
}

static void onChannelOn(GtkButton*, gpointer data) {
    int ch = GPOINTER_TO_INT(data);
    if (!g_core->setRelay(ch, true))
        appendLog("[!] Relay tidak terhubung.");
    else
        appendLog("[Relay] Perintah: CH" + std::to_string(ch) + " ON");
}

static void onChannelOff(GtkButton*, gpointer data) {
    int ch = GPOINTER_TO_INT(data);
    if (!g_core->setRelay(ch, false))
        appendLog("[!] Relay tidak terhubung.");
    else
        appendLog("[Relay] Perintah: CH" + std::to_string(ch) + " OFF");
}

// ---------------------------------------------------------------
// NIM search + toggle semua channel di g_scan_channels + log CSV
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

    uint8_t status = g_core->getRelayStatus();

    for (int ch : g_scan_channels) {
        bool ch_on     = (status & (1 << (ch - 1))) != 0;
        bool new_state = !ch_on;
        std::string label = "CH" + std::to_string(ch);

        if (!g_core->setRelay(ch, new_state)) {
            appendLog("[!] Gagal toggle " + label + ".");
            continue;
        }

        std::string scanStatus = new_state ? "IN" : "OUT";
        appendLog("[Relay] " + label + " toggle -> " + scanStatus);
        appendScanLog(nim, nama, scanStatus, ch);
    }
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

    GtkWidget* lbl = gtk_label_new("NIM / Scan:");
    g_nim_entry    = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_nim_entry), "Scan atau ketik NIM lalu Enter...");
    gtk_widget_set_hexpand(g_nim_entry, TRUE);

    g_signal_connect(g_nim_entry, "activate", G_CALLBACK(onNIMActivate), nullptr);

    gtk_box_pack_start(GTK_BOX(hbox), lbl,          FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), g_nim_entry,  TRUE,  TRUE,  0);
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

    // Grid channel
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("CH"),     0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Status"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("ON"),     2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("OFF"),    3, 0, 1, 1);

    for (int i = 0; i < 8; i++) {
        int ch = i + 1;
        GtkWidget* lbl_ch = gtk_label_new(("CH" + std::to_string(ch)).c_str());
        g_ch_indicators[i] = gtk_label_new("OFF");

        GtkStyleContext* ctx = gtk_widget_get_style_context(g_ch_indicators[i]);
        gtk_style_context_add_class(ctx, "indicator-off");

        g_ch_buttons[i][0] = gtk_button_new_with_label("ON");
        g_ch_buttons[i][1] = gtk_button_new_with_label("OFF");

        g_signal_connect(g_ch_buttons[i][0], "clicked", G_CALLBACK(onChannelOn),  GINT_TO_POINTER(ch));
        g_signal_connect(g_ch_buttons[i][1], "clicked", G_CALLBACK(onChannelOff), GINT_TO_POINTER(ch));

        gtk_grid_attach(GTK_GRID(grid), lbl_ch,             0, ch, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), g_ch_indicators[i], 1, ch, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), g_ch_buttons[i][0], 2, ch, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), g_ch_buttons[i][1], 3, ch, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    // All ON / All OFF
    GtkWidget* hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* btn_all_on  = gtk_button_new_with_label("Semua ON");
    GtkWidget* btn_all_off = gtk_button_new_with_label("Semua OFF");
    g_signal_connect(btn_all_on,  "clicked", G_CALLBACK(onAllOnClicked),  nullptr);
    g_signal_connect(btn_all_off, "clicked", G_CALLBACK(onAllOffClicked), nullptr);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_all_on,  FALSE, FALSE, 0);
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