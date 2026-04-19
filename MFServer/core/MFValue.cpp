#include "MFValue.hpp"

#include <stdexcept>


MFValue::MFValue()
: m_type(VNULL) {
}

MFValue::MFValue(const std::string &str)
: m_type(STRING)
, m_str(str) {
    m_val.v_bool = false;
}

MFValue::MFValue(std::string&& str)
: m_type(STRING)
, m_str(std::move(str)) {
    m_val.v_bool = false;
}

MFValue::MFValue(int64_t value)
: m_type(INT64) {
    m_val.v_sint = value;
}

MFValue::MFValue(uint64_t value)
: m_type(UINT64) {
    m_val.v_uint = value;
}

MFValue::MFValue(float value)
: m_type(FLOAT) {
    m_val.v_float = value;
}

MFValue::MFValue(double value)
: m_type(DOUBLE) {
    m_val.v_double = value;
}

MFValue::MFValue(bool value)
: m_type(BOOL) {
    m_val.v_bool = value;
}

MFValue::MFValue(const unsigned char *ptr, size_t len)
: m_type(RAW) {
    m_str.assign(reinterpret_cast<const char *>(ptr), len);
}

bool MFValue::isNull() const {
    return VNULL == m_type;
}

bool MFValue::getBool() const {
    switch (m_type) {
        case BOOL:
            return m_val.v_bool;
        case UINT64:
            return m_val.v_uint != 0;
        case INT64:
            return m_val.v_sint != 0;
        default:
            throw std::runtime_error("Can not convert to Boolean value");
    }
}

uint64_t MFValue::getUInt() const {
    switch (m_type) {
        case UINT64:
            return m_val.v_uint;
        case INT64:
            return m_val.v_sint;
        default:
            throw std::runtime_error("Can not convert to Integer value");
    }
}

int64_t MFValue::getSint() const {
    if (INT64 == m_type) {
        return m_val.v_sint;
    }
    return getUInt();
}

float MFValue::getFloat() const {
    switch (m_type) {
        case INT64:
            return 1.0F*m_val.v_sint;
        case UINT64:
            return 1.0F*m_val.v_uint;
        case FLOAT:
            return m_val.v_float;
        default:
            throw std::runtime_error("Value cannot be converted to float number");
    }
}

double MFValue::getDouble() const {
    switch (m_type) {
        case INT64:
            return 1.0 * m_val.v_sint;
        case UINT64:
            return 1.0 * m_val.v_uint;
        case FLOAT:
            return m_val.v_float;
        case DOUBLE:
            return m_val.v_double;
        default:
            throw std::runtime_error("Value can not be converted to double number");
    }
}

MFValue::Type MFValue::getType() const {
    return m_type;
}

const std::string& MFValue::getString() const {
    return m_str;
}


