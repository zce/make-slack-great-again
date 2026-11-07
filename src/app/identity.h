// Application identity and platform-specific directories.
//
//                    Linux                    macOS                  Windows
//   app id           msga (app_id, WM_CLASS,  com.nisdos.msga        com.nisdos.msga
//                    msga.desktop)           (bundle id)            (AppUserModelID)
//   URL scheme       msga://                  msga://                msga://
//   settings store   ~/.config/msga/msga.conf com.msga.msga plist    HKCU\Software\msga\msga
//                    (legacy settings; credentials remain in the existing store)
//   configDir        ~/.config/msga           <dataDir>              <dataDir>
//   dataDir          ~/.local/share/msga      ~/Library/Application  %APPDATA%\msga\MSGA
//                                             Support/msga/MSGA
//   cacheDir         ~/.cache/msga            ~/Library/Caches/      %LOCALAPPDATA%\msga\MSGA\cache
//                                             msga/MSGA
//
// Linux uses one application component beneath XDG_CONFIG_HOME, XDG_DATA_HOME
// and XDG_CACHE_HOME. The previous msga/MSGA data and cache layout is not
// automatically migrated; users upgrading on Linux must move their files.
// Other platforms retain their historical paths and application identity.
#pragma once

#include <string>

namespace plat {
class App;
}

namespace identity {

inline constexpr const char *kName      = "MSGA"; // the application name
inline constexpr const char *kUrlScheme = "msga";
// Linux: the desktop entry / Wayland app_id / WM_CLASS instance; elsewhere
// the bundle id / AUMID.
const char                  *appId();
// The single-instance channel's name (plat::App::claimSingleInstance).
const char                  *instanceKey();

// An earlier version, if it is running, takes this launch the way it took its
// own second launches (a local socket, or a named pipe on Windows,
// "msga-<home folder name>", carrying one length-prefixed UTF-16BE string —
// the msga:// URL, or nothing): it shows its window and gets the URL. True when it did; this
// process then exits, so the two never run on one store at once.
bool handOffToEarlierVersion(const std::string &url);

// Absolute, no trailing slash; "" when the OS gives no such directory.
std::string configDir(plat::App &app);
std::string dataDir(plat::App &app);
std::string cacheDir(plat::App &app);
// <dataDir>/crash.log: where the old crash handler appended its reports.
std::string crashLogPath(plat::App &app);

} // namespace identity
