#pragma once
// Wrapper lintas platform: mkdir, access, sleep_ms.
// Linux/Mac pakai POSIX asli; Windows pakai _mkdir/_access/Sleep.

#if defined(_WIN32)
    #include <direct.h>
    #include <io.h>
    #include <windows.h>
    #define PORTABLE_F_OK 0
    inline int portable_mkdir(const std::string& path) {
        return _mkdir(path.c_str());
    }
    inline bool portable_path_exists(const std::string& path) {
        return _access(path.c_str(), PORTABLE_F_OK) == 0;
    }
    inline void portable_sleep_ms(int ms) { Sleep(ms); }
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #define PORTABLE_F_OK F_OK
    inline int portable_mkdir(const std::string& path) {
        return mkdir(path.c_str(), 0755);
    }
    inline bool portable_path_exists(const std::string& path) {
        return access(path.c_str(), PORTABLE_F_OK) == 0;
    }
    inline void portable_sleep_ms(int ms) { usleep(ms * 1000); }
#endif
