#include "MFJsonConfig.hpp"
#include "MFApplication.hpp"
#include "MFUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <sstream>

MFJsonConfig& MFJsonConfig::instance() {
    static MFJsonConfig s;
    return s;
}

bool MFJsonConfig::ingestFile(const std::string& fullPath, const std::string& configName, std::string& errOut) {
    if (!MFUtil::checkFileExists(fullPath)) {
        errOut = "file not found: " + fullPath;
        return false;
    }
    std::string content = MFUtil::readFile(fullPath);
    if (content.empty()) {
        errOut = "empty or unreadable file: " + fullPath;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    JSONCPP_STRING errs;
    std::istringstream iss(content);
    if (!Json::parseFromStream(builder, iss, &root, &errs)) {
        errOut = errs.empty() ? "json parse error" : errs;
        return false;
    }
    MFApplication::getInstance()->logInfo("load config success = {}", configName);
    {
        std::unique_lock lock(m_mutex);
        m_roots[configName] = std::make_shared<Json::Value>(std::move(root));
    }
    return true;
}

bool MFJsonConfig::load(const std::string& fullPath, std::string& errOut) {
    const std::string& configName = MFUtil::getFileName(fullPath);
    return ingestFile(fullPath, configName, errOut);
}

static bool pathEndsWithJsonIcase(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".json";
}

static bool isDotHidden(const std::filesystem::path& filename) {
    std::string fn = filename.string();
    return !fn.empty() && fn[0] == '.';
}

bool MFJsonConfig::loadAllFromDirectory(const std::string& rootPath, bool recursive, std::string& errOut) {
    errOut.clear();
    std::filesystem::path base = std::filesystem::path(rootPath);
    std::error_code ec;
    if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) {
        errOut = "not a directory: " + base.string();
        return false;
    }

    std::ostringstream failures;
    size_t failCount = 0;

    auto processFile = [&](const std::filesystem::path& filePath) {
        if (!pathEndsWithJsonIcase(filePath)) {
            return;
        }
        if (isDotHidden(filePath.filename())) {
            return;
        }
        const std::string configName = filePath.stem().string();
        if (configName.empty()) {
            return;
        }
        std::string oneErr;
        if (!ingestFile(filePath.string(), configName, oneErr)) {
            ++failCount;
            failures << "[" << configName << "] " << oneErr << "; ";
        }
    };

    try {
        if (recursive) {
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(base)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (isDotHidden(entry.path().filename())) {
                    continue;
                }
                processFile(entry.path());
            }
        } else {
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(base)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                processFile(entry.path());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        errOut = std::string("filesystem: ") + e.what();
        return false;
    }

    if (failCount > 0) {
        errOut = failures.str();
        return false;
    }
    return true;
}

void MFJsonConfig::remove(const std::string& name) {
    std::unique_lock lock(m_mutex);
    m_roots.erase(name);
}

bool MFJsonConfig::has(const std::string& name) const {
    std::shared_lock lock(m_mutex);
    return m_roots.find(name) != m_roots.end();
}

bool MFJsonConfig::cppNavigateOne(const Json::Value*& cur, const char* key) {
    if (key == nullptr || !cur->isObject() || !cur->isMember(key)) {
        return false;
    }
    cur = &(*cur)[key];
    return true;
}

bool MFJsonConfig::cppNavigateOne(const Json::Value*& cur, const std::string& key) {
    if (!cur->isObject() || !cur->isMember(key)) {
        return false;
    }
    cur = &(*cur)[key];
    return true;
}

bool MFJsonConfig::cppNavigateOne(const Json::Value*& cur, std::string_view key) {
    return cppNavigateOne(cur, std::string(key));
}

