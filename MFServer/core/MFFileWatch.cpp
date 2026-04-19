#include "MFFileWatch.hpp"
#include "MFApplication.hpp"

static void watchCallback(dmon_watch_id watch_id, dmon_action action, const char* rootdir, const char* filepath, const char* oldfilepath, void* user) {
    MFFileWatchar* watch = static_cast<MFFileWatchar*>(user);
    watch->m_callback(action, std::string(rootdir), std::string(filepath));
}

MFFileWatchar::~MFFileWatchar() {
    dmon_deinit();
}

void MFFileWatchar::start(const std::string& path, WatchCallback callback) {
    m_callback = std::move(callback);
    dmon_init();
    auto result = dmon_watch(path.c_str(), watchCallback, DMON_WATCHFLAGS_RECURSIVE, this);
    if (result.id == 0) {
        MFApplication::getInstance()->logInfo("fileWatch start fail");
    }
}
