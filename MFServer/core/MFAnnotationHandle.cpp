#include "MFAnnotationHandle.hpp"

#include <regex>
#include "MFApplication.hpp"
#include "MFUtil.hpp"

static const std::vector<std::string> annotationNameList = { "MFRouterHandle" };


MFAnnotationHandle* MFAnnotationHandle::m_instance = nullptr;

MFAnnotationHandle::MFAnnotationHandle() {

}

MFAnnotationHandle * MFAnnotationHandle::getInstance() {
    if (!m_instance) {
        m_instance = new MFAnnotationHandle();
    }
    return m_instance;
}

void MFAnnotationHandle::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

void MFAnnotationHandle::scanRouter(const std::string &dir, const sol::this_state& lua) {
    const std::string& scriptRoot = MFApplication::getInstance()->getScriptRoot();

    std::vector<std::string> files;
    MFUtil::getFileRecursive(dir, files);

    for (const std::string& fullPath : files) {
        std::string content = MFUtil::readFile(fullPath);
        const std::vector<std::string>& lines = MFUtil::splitString(content, "\n");

        std::string curParamString;
        bool found = false;

        for (const std::string& lineContent : lines) {
            if (MFUtil::trim(lineContent).empty()) {
                continue;
            }
            for (const std::string& annotationName : annotationNameList) {
                curParamString = isAnnotation(lineContent, annotationName);
                if (!curParamString.empty()) {
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }

        if (!found || curParamString.empty()) {
            continue;
        }

        MFFastMap<std::string, std::string> annotationInfo;
        parseParam(curParamString, annotationInfo);

        auto itRouter = annotationInfo.find("router");
        if (itRouter == annotationInfo.end()) {
            continue;
        }

        std::string modulePath = getModulePath(scriptRoot, fullPath);

        const std::string& routerString = itRouter->second;
        if (routerString.find(".") == std::string::npos) {
            continue;
        }

        const std::vector<std::string>& routerList = MFUtil::splitString(routerString, "\\.");
        if (routerList.size() < 2) {
            continue;
        }
        const std::string& v1 = MFUtil::trim(routerList[0]);
        const std::string& v2 = MFUtil::trim(routerList[1]);
        std::string router = "";
        auto luaValue = sol::state_view(lua)[v1][v2];
        if (luaValue.get_type() == sol::type::number) {
            router = std::to_string(luaValue.get<uint32_t>());
        } else if (luaValue.get_type() == sol::type::string) {
            router = luaValue.get<std::string>();
        }
        if (m_moduleMap.count(router) > 0) {
            MFApplication::getInstance()->logInfo("MFAnnotation: has same router = <{}> value = <{}>", routerString);
            continue;
        }
        m_moduleMap[router] = modulePath;
        MFApplication::getInstance()->logInfo("add router = <{}>, modulePath = <{}>", routerString, modulePath);
    }
}

sol::object MFAnnotationHandle::getModulePath(const std::string &key, const sol::this_state& lua) {
    auto it = m_moduleMap.find(key);
    if (it == m_moduleMap.end()) {
        return sol::lua_nil;
    }
    return sol::make_object(lua, it->second);
}


std::string MFAnnotationHandle::isAnnotation(const std::string& line, const std::string& annotationName) {
    std::string pattern1 = annotationName + R"(\s*\(([^)]*)\))";
    std::regex re1(pattern1);
    std::smatch m1;
    if (!std::regex_search(line, m1, re1)) {
        return "";
    }

    std::string content = m1[1].str();
    std::regex re2(R"(^\s*\{\s*(.*?)\s*\}\s*$)");
    std::smatch m2;
    if (!std::regex_match(content, m2, re2)) {
        return "";
    }
    return m2[1].str();
}

std::string MFAnnotationHandle::getModulePath(const std::string& parentPath, const std::string& fullPath) {
    if (parentPath.empty() || fullPath.size() < parentPath.size()) {
        return fullPath;
    }
    if (fullPath.compare(0, parentPath.size(), parentPath) != 0) {
        return fullPath;
    }
    if (fullPath.size() > parentPath.size() && (fullPath[parentPath.size()] != '/' && fullPath[parentPath.size()] != '\\')) {
        return fullPath;
    }
    size_t start = parentPath.size() + 1;
    std::string path = fullPath.substr(start);
    for (char& c : path) {
        if (c == '\\') {
            c = '.';
        } else if (c == '/') {
            c = '.';
        }
    }
    const std::string suffix = ".lua";
    if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
        path.resize(path.size() - suffix.size());
    }
    return path;
}

void MFAnnotationHandle::parseParam(const std::string& paramStr, MFFastMap<std::string, std::string>& outParams) {
    std::vector<std::string> parts = MFUtil::splitString(paramStr, ",");
    for (const std::string &part: parts) {
        std::string trimmed = MFUtil::trim(part);
        if (trimmed.empty()) {
            continue;
        }
        std::vector<std::string> kv = MFUtil::splitString(trimmed, "=");
        if (kv.size() < 2) {
            continue;
        }
        std::string attrName = MFUtil::trim(kv[0]);
        std::string attrValue = MFUtil::trim(kv[1]);
        outParams[attrName] = attrValue;
    }
}