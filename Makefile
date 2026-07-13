# ---------------------------------------------------------------
# Makefile untuk LINUX.
#   - macOS   -> pakai Makefile.macos  (make -f Makefile.macos all)
#   - Windows -> pakai build_windows.bat (tidak perlu "make" sama sekali)
#
# Semua hasil build (binary + config) masuk ke folder dist/.
# Tidak ada target "install" -- jalankan langsung dari dist/.
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

CLI_SRCS := $(COMMON_SRCS) $(SRCDIR)/cli_ui.cpp $(SRCDIR)/main.cpp
GUI_SRCS := $(COMMON_SRCS) $(SRCDIR)/gui_ui.cpp $(SRCDIR)/main.cpp

CLI_LIBS := -lhidapi-hidraw -ludev -lncurses
GUI_LIBS := -lhidapi-hidraw -ludev $(GTK_LIBS)

TARGET_CLI := usbrelay-cli
TARGET_GUI := usbrelay-gui

.PHONY: all cli gui clean deps install-udev dist-assets

all: cli gui

# Siapkan folder dist/ + salin config supaya dist/ bisa langsung dipakai
# tanpa perlu file lain dari repo ini.
dist-assets:
	@mkdir -p $(OBJDIR) $(DISTDIR)
	@cp -r config $(DISTDIR)/ 2>/dev/null || true

cli: dist-assets $(CLI_SRCS)
	$(CXX) $(CXXFLAGS) \
		-o $(DISTDIR)/$(TARGET_CLI) $(CLI_SRCS) $(CLI_LIBS)
	@echo "==> Build CLI selesai: $(DISTDIR)/$(TARGET_CLI)"

gui: dist-assets $(GUI_SRCS)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -DUSE_GUI \
		-o $(DISTDIR)/$(TARGET_GUI) $(GUI_SRCS) $(GUI_LIBS)
	@echo "==> Build GUI selesai: $(DISTDIR)/$(TARGET_GUI)"

# Install dependencies (Debian/Ubuntu)
deps:
	sudo apt-get install -y \
	    libhidapi-dev \
	    libhidapi-hidraw0 \
	    libudev-dev \
	    libncurses-dev \
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

clean:
	rm -rf $(OBJDIR) $(DISTDIR)