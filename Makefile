# ---------------------------------------------------------------
# Makefile untuk LINUX (GUI-only).
#   - macOS   -> pakai Makefile.macos  (make -f Makefile.macos all)
#   - Windows -> pakai build_windows.bat (tidak perlu "make" sama sekali)
#
# Hasil build (binary) masuk ke folder dist/. Tidak ada langkah
# "install" -- jalankan langsung dari dist/ ("cd dist && ./usbrelay-gui").
#
# Config (config/device_map.conf, config/channels.conf) TIDAK lagi
# disalin ke dist/ saat build -- binary membacanya langsung dari
# ../config/ (relatif terhadap dist/, yaitu folder config/ di root
# project ini). data.csv juga TIDAK disalin -- filenya sudah permanen
# ada di dalam dist/ (lihat dist/data.csv), supaya folder dist/ gampang
# di-porting/copy ke tempat lain sebagai satu paket lengkap (binary +
# data.csv) tanpa perlu file config terpisah.
# ---------------------------------------------------------------

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRCDIR   := src
OBJDIR   := build
DISTDIR  := dist

# Deteksi pkg-config untuk GTK3
GTK_CFLAGS  := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS    := $(shell pkg-config --libs   gtk+-3.0 2>/dev/null)

COMMON_SRCS := \
    $(SRCDIR)/relay_controller.cpp \
    $(SRCDIR)/usb_monitor.cpp \
    $(SRCDIR)/device_mapper.cpp \
    $(SRCDIR)/app_core.cpp

GUI_SRCS := $(COMMON_SRCS) $(SRCDIR)/gui_ui.cpp $(SRCDIR)/main.cpp
GUI_LIBS := -lhidapi-hidraw -ludev $(GTK_LIBS)

TARGET_GUI := usbrelay-gui

.PHONY: all gui clean deps install-udev dist-assets

all: gui

# Pastikan folder build/ dan dist/ ada. TIDAK menyalin apa pun ke
# dalamnya lagi (lihat catatan di atas) -- cuma memastikan foldernya ada.
dist-assets:
	@mkdir -p $(OBJDIR) $(DISTDIR)

gui: dist-assets $(GUI_SRCS)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -DUSE_GUI \
		-o $(DISTDIR)/$(TARGET_GUI) $(GUI_SRCS) $(GUI_LIBS)
	@echo "==> Build GUI selesai: $(DISTDIR)/$(TARGET_GUI)"
	@echo "==> Config dibaca dari ../config/ (relatif dist/); data.csv sudah ada di dist/."

# Install dependencies (Debian/Ubuntu)
deps:
	sudo apt-get install -y \
	    libhidapi-dev \
	    libhidapi-hidraw0 \
	    libudev-dev \
	    libgtk-3-dev \
	    pkg-config \
	    build-essential

# Pasang udev rule agar tidak perlu sudo
install-udev:
	@echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="16c0", ATTR{idProduct}=="05df", MODE="0666"' \
	    | sudo tee /etc/udev/rules.d/99-usbrelay.rules
	sudo udevadm control --reload-rules
	sudo udevadm trigger
	@echo "==> udev rule dipasang. Cabut & pasang kembali relay."

# Cuma hapus hasil kompilasi (build/ dan binary-nya) -- TIDAK menghapus
# seluruh dist/ lagi, karena dist/ sekarang juga menyimpan data.csv,
# usage.csv, dan logs/ yang bukan hasil build (jangan sampai hilang
# gara-gara "make clean").
clean:
	rm -rf $(OBJDIR)
	rm -f $(DISTDIR)/$(TARGET_GUI)
