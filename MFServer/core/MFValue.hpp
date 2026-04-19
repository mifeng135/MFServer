#ifndef MFValue_h
#define MFValue_h

#include <cstdint>
#include <string>


class MFValue {
public:
    enum Type
    {
        VNULL,      ///< Null value
        UINT64,     ///< Unsigned integer
        INT64,      ///< Signed integer
        FLOAT,      ///< Float number
        DOUBLE,     ///< Double number
        BOOL,       ///< Boolean
        STRING,     ///< String (utf8)
        USTRING,    ///< Wide string (utf16)
        RAW,        ///< Raw bytes
        EXPR,       ///< String to be interpreted as an expression
        JSON,       ///< JSON string
    };
public:
    explicit MFValue();
    explicit MFValue(const std::string& str);
    explicit MFValue(std::string&& str);
    explicit MFValue(int64_t value);
    explicit MFValue(uint64_t value);
    explicit MFValue(float value);
    explicit MFValue(double value);
    explicit MFValue(bool value);
    explicit MFValue(const unsigned char* ptr, size_t len);

public:
    bool isNull() const;
    bool getBool() const;
    uint64_t getUInt() const;
    int64_t getSint() const;
    float getFloat() const;
    double getDouble() const;
    Type getType() const;
    const std::string& getString() const;
protected:
    Type            m_type;
    std::string     m_str;
    union {
        double   v_double = 0.0;
        float    v_float;
        int64_t  v_sint;
        uint64_t v_uint;
        bool     v_bool;
    } m_val;
};

#endif /* MFValue_h */
