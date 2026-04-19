#ifndef MFAnnotationHandle_hpp
#define MFAnnotationHandle_hpp

#include "MFMacro.h"
#include "sol/sol.hpp"

class MFAnnotationHandle {
public:
    MFAnnotationHandle();
public:
    static MFAnnotationHandle* getInstance();
    static void destroyInstance();
public:
    void scanRouter(const std::string& dir, const sol::this_state& lua);
    sol::object getModulePath(const std::string& key, const sol::this_state& lua);
private:
    std::string isAnnotation(const std::string& line, const std::string& annotationName);
    std::string getModulePath(const std::string& parentPath, const std::string& fullPath);
    void parseParam(const std::string& paramStr, MFFastMap<std::string, std::string>& outParams);
private:
    MFFastMap<std::string, std::string>     m_moduleMap;
private:
    static MFAnnotationHandle*              m_instance;
};
#endif /* MFAnnotationHandle_hpp */
