#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#include <string>
#include <functional>

#include "dmon/dmon.h"

using WatchCallback = std::function<void(dmon_action action, const std::string& rootDir, std::string filePath)>;

class MFFileWatchar {
public:
    ~MFFileWatchar();
public:
    void start(const std::string& path, WatchCallback callback);
public:
    WatchCallback   m_callback;
};
#endif // !FILE_WATCHER_H
