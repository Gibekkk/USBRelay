CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRCDIR   := src
OBJDIR   := build
DISTDIR  := dist

COMMON_SRCS := \
    $(SRCDIR)/relay_controller.cpp \
    $(SRCDIR)/device_mapper.cpp \
    $(SRCDIR)/app_core.cpp

USB_MON_LINUX := $(SRCDIR)/usb_monitor_linux.cpp
USB_MON_MAC   := $(SRCDIR)/usb_monitor_mac.cpp
USB_MON_WIN   := $(SRCDIR)/usb_monitor_win.cpp

GUI_SRCS := $(SRCDIR)/gui_ui.cpp $(SRCDIR)/main.cpp

.PHONY: all linux mac windows clean deps-linux deps-mac deps-windows install install-udev

all: linux mac windows

# =================================================================
# LINUX  (jalankan di Linux, butuh: g++, hidapi-hidraw, libudev, gtk3)
# =================================================================
linux: CXX      := g++
linux: GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
linux: GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 2>/dev/null)
linux:
	@mkdir -p $(DISTDIR)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -DUSE_GUI -o $(DISTDIR)/gui-app-linux \
		$(COMMON_SRCS) $(USB_MON_LINUX) $(GUI_SRCS) \
		-lhidapi-hidraw -ludev $(GTK_LIBS)
	@echo "==> Build Linux selesai: $(DISTDIR)/gui-app-linux"

# =================================================================
# MACOS  (jalankan di macOS, butuh: brew install hidapi gtk+3 pkg-config)
# =================================================================
mac: CXX      := clang++
mac: GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
mac: GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 2>/dev/null)
mac: HID_CFLAGS := $(shell pkg-config --cflags hidapi 2>/dev/null)
mac: HID_LIBS   := $(shell pkg-config --libs   hidapi 2>/dev/null || echo -lhidapi)
mac:
	@mkdir -p $(DISTDIR)
	$(CXX) $(CXXFLAGS) $(HID_CFLAGS) $(GTK_CFLAGS) -DUSE_GUI -o $(DISTDIR)/gui-app-mac \
		$(COMMON_SRCS) $(USB_MON_MAC) $(GUI_SRCS) \
		$(HID_LIBS) $(GTK_LIBS)
	@echo "==> Build macOS selesai: $(DISTDIR)/gui-app-mac"

# =================================================================
# WINDOWS (jalankan di MSYS2/MinGW-w64, butuh: pacman -S hidapi gtk3)
# =================================================================
windows: CXX      := g++
windows: GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
windows: GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 2>/dev/null)
windows: HID_CFLAGS := $(shell pkg-config --cflags hidapi 2>/dev/null)
windows: HID_LIBS   := $(shell pkg-config --libs   hidapi 2>/dev/null || echo -lhidapi)
windows:
	@mkdir -p $(DISTDIR)
	$(CXX) $(CXXFLAGS) $(HID_CFLAGS) $(GTK_CFLAGS) -DUSE_GUI -mwindows \
		-o $(DISTDIR)/gui-app-win.exe \
		$(COMMON_SRCS) $(USB_MON_WIN) $(GUI_SRCS) \
		$(HID_LIBS) $(GTK_LIBS)
	@echo "==> Build Windows selesai: $(DISTDIR)/gui-app-win.exe"

# =================================================================
# Dependencies per-OS
# =================================================================
deps-linux:
	sudo apt-get install -y \
	    libhidapi-dev libhidapi-hidraw0 libudev-dev \
	    libgtk-3-dev pkg-config build-essential

deps-mac:
	brew install hidapi gtk+3 pkg-config

deps-windows:
	pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-hidapi \
	    mingw-w64-x86_64-gtk3 mingw-w64-x86_64-pkg-config

install-udev:
	@echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="16c0", ATTR{idProduct}=="05df", MODE="0666"' \
	    | sudo tee /etc/udev/rules.d/99-usbrelay.rules
	sudo udevadm control --reload-rules
	sudo udevadm trigger
	@echo "==> udev rule dipasang. Cabut & pasang kembali relay."

install: linux
	sudo cp $(DISTDIR)/gui-app-linux /usr/local/bin/
	sudo mkdir -p /etc/usbrelay
	@[ -f /etc/usbrelay/device_map.conf ] || \
	    sudo cp config/device_map.conf /etc/usbrelay/device_map.conf
	@echo "==> Installed ke /usr/local/bin/"

clean:
	rm -rf $(DISTDIR) $(OBJDIR)
