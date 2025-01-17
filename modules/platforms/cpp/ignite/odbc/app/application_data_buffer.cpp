﻿/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ignite/odbc/log.h>
#include <ignite/odbc/utility.h>
#include <ignite/odbc/app/application_data_buffer.h>
#include <ignite/odbc/system/odbc_constants.h>

#include <ignite/common/bits.h>

#include <algorithm>
#include <string>
#include <sstream>

namespace
{

// Just copy bytes currently.
// Only works for the ASCII character set.
ignite::conversion_result string_to_wstring(const char* str, std::int64_t str_len, SQLWCHAR* wstr,
    std::int64_t wstr_len)
{
    using namespace ignite;

    if (wstr_len <= 0)
        return conversion_result::AI_VARLEN_DATA_TRUNCATED;

    std::int64_t to_copy = std::min(str_len, wstr_len - 1);

    if (to_copy <= 0) {
        wstr[0] = 0;
        return conversion_result::AI_VARLEN_DATA_TRUNCATED;
    }

    for (int64_t i = 0; i < to_copy; ++i)
        wstr[i] = str[i];

    wstr[to_copy] = 0;

    if (to_copy < str_len)
        return conversion_result::AI_VARLEN_DATA_TRUNCATED;

    return conversion_result::AI_SUCCESS;
}

} // anonymous namespace

namespace ignite
{

application_data_buffer::application_data_buffer(odbc_native_type type, void* buffer, SQLLEN buf_len, SQLLEN* res_len)
    : m_type(type)
    , m_buffer(buffer)
    , m_buffer_len(buf_len)
    , m_result_len(res_len)
    , m_byte_offset(0)
    , m_element_offset(0) { }

template<typename T>
conversion_result application_data_buffer::put_num(T value)
{
    LOG_MSG("value: " << value);

    SQLLEN* res_len_ptr = get_result_len();
    void* data_ptr = get_data();

    switch (m_type)
    {
        case odbc_native_type::AI_SIGNED_TINYINT:
        {
            return put_num_to_num_buffer<signed char>(value);
        }

        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        {
            return put_num_to_num_buffer<unsigned char>(value);
        }

        case odbc_native_type::AI_SIGNED_SHORT:
        {
            return put_num_to_num_buffer<SQLSMALLINT>(value);
        }

        case odbc_native_type::AI_UNSIGNED_SHORT:
        {
            return put_num_to_num_buffer<SQLUSMALLINT>(value);
        }

        case odbc_native_type::AI_SIGNED_LONG:
        {
            return put_num_to_num_buffer<SQLINTEGER>(value);
        }

        case odbc_native_type::AI_UNSIGNED_LONG:
        {
            return put_num_to_num_buffer<SQLUINTEGER>(value);
        }

        case odbc_native_type::AI_SIGNED_BIGINT:
        {
            return put_num_to_num_buffer<SQLBIGINT>(value);
        }

        case odbc_native_type::AI_UNSIGNED_BIGINT:
        {
            return put_num_to_num_buffer<SQLUBIGINT>(value);
        }

        case odbc_native_type::AI_FLOAT:
        {
            return put_num_to_num_buffer<SQLREAL>(value);
        }

        case odbc_native_type::AI_DOUBLE:
        {
            return put_num_to_num_buffer<SQLDOUBLE>(value);
        }

        case odbc_native_type::AI_CHAR:
        {
            return put_value_to_string_buffer<char>(value);
        }

        case odbc_native_type::AI_WCHAR:
        {
            return put_value_to_string_buffer<wchar_t>(value);
        }

        case odbc_native_type::AI_NUMERIC:
        {
            if (data_ptr) {
                auto* out = reinterpret_cast<SQL_NUMERIC_STRUCT*>(data_ptr);
                auto u_val = static_cast<std::uint64_t>(value < 0 ? -value : value);

                out->precision = digit_length(u_val);
                out->scale = 0;
                out->sign = value < 0 ? 0 : 1;

                memset(out->val, 0, SQL_MAX_NUMERIC_LEN);

                memcpy(out->val, &u_val, std::min<int>(SQL_MAX_NUMERIC_LEN, sizeof(u_val)));
            }

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        {
            if (data_ptr)
                memcpy(data_ptr, &value, std::min(sizeof(value), static_cast<std::size_t>(m_buffer_len)));

            if (res_len_ptr)
                *res_len_ptr = sizeof(value);

            return static_cast<std::size_t>(m_buffer_len) < sizeof(value) ?
                conversion_result::AI_VARLEN_DATA_TRUNCATED : conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TDATE:
        case odbc_native_type::AI_TTIMESTAMP:
        case odbc_native_type::AI_TTIME:
        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

template<typename TBuf, typename TIn>
conversion_result application_data_buffer::put_num_to_num_buffer(TIn value)
{
    void* data_ptr = get_data();
    SQLLEN* res_len_ptr = get_result_len();

    if (data_ptr) {
        TBuf* out = reinterpret_cast<TBuf*>(data_ptr);
        *out = static_cast<TBuf>(value);
    }

    if (res_len_ptr)
        *res_len_ptr = static_cast<SQLLEN>(sizeof(TBuf));

    return conversion_result::AI_SUCCESS;
}

template<typename CharT, typename Tin>
conversion_result application_data_buffer::put_value_to_string_buffer(const Tin& value)
{
    std::basic_stringstream<CharT> converter;
    converter << value;

    std::int32_t written = 0;

    return put_string_to_string_buffer<CharT>(converter.str(), written);
}

template<typename CharT>
conversion_result application_data_buffer::put_value_to_string_buffer(const std::int8_t& value)
{
    std::basic_stringstream<CharT> converter;
    converter << static_cast<int>(value);

    std::int32_t written = 0;

    return put_string_to_string_buffer<CharT>(converter.str(), written);
}

template<typename OutCharT, typename InCharT>
conversion_result application_data_buffer::put_string_to_string_buffer(const std::basic_string<InCharT>& value,
    std::int32_t& written)
{
    written = 0;
    auto char_size = static_cast<SQLLEN>(sizeof(OutCharT));

    SQLLEN* res_len_ptr = get_result_len();
    void* data_ptr = get_data();

    if (res_len_ptr)
        *res_len_ptr = static_cast<SQLLEN>(value.size());

    if (!data_ptr)
        return conversion_result::AI_SUCCESS;

    if (m_buffer_len < char_size)
        return conversion_result::AI_VARLEN_DATA_TRUNCATED;

    auto* out = reinterpret_cast<OutCharT*>(data_ptr);

    SQLLEN outLen = (m_buffer_len / char_size) - 1;

    SQLLEN to_copy = std::min<SQLLEN>(outLen, value.size());

    for (SQLLEN i = 0; i < to_copy; ++i)
        out[i] = value[i];

    out[to_copy] = 0;

    written = static_cast<std::int32_t>(to_copy);

    if (to_copy < static_cast<SQLLEN>(value.size()))
        return conversion_result::AI_VARLEN_DATA_TRUNCATED;

    return conversion_result::AI_SUCCESS;
}

conversion_result application_data_buffer::put_raw_data_to_buffer(void *data, std::size_t len, std::int32_t& written)
{
    auto s_len = static_cast<SQLLEN>(len);

    SQLLEN* res_len_ptr = get_result_len();
    void* data_ptr = get_data();

    if (res_len_ptr)
        *res_len_ptr = s_len;

    SQLLEN to_copy = std::min(m_buffer_len, s_len);

    if (data_ptr != nullptr && to_copy > 0)
        memcpy(data_ptr, data, static_cast<std::size_t>(to_copy));

    written = static_cast<std::int32_t>(to_copy);

    return to_copy < s_len ? conversion_result::AI_VARLEN_DATA_TRUNCATED : conversion_result::AI_SUCCESS;
}

conversion_result application_data_buffer::put_int8(int8_t value)
{
    return put_num(value);
}

conversion_result application_data_buffer::put_int16(int16_t value)
{
    return put_num(value);
}

conversion_result application_data_buffer::put_int32(std::int32_t value)
{
    return put_num(value);
}

conversion_result application_data_buffer::put_int64(int64_t value)
{
    return put_num(value);
}

conversion_result application_data_buffer::put_float(float value)
{
    return put_num(value);
}

conversion_result application_data_buffer::put_double(double value)
{
    return put_num(value);
}

conversion_result application_data_buffer::put_string(const std::string & value)
{
    std::int32_t written = 0;

    return put_string(value, written);
}

conversion_result application_data_buffer::put_string(const std::string& value, std::int32_t& written)
{
    LOG_MSG("value: " << value);

    switch (m_type)
    {
        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        case odbc_native_type::AI_NUMERIC:
        {
            std::stringstream converter;

            converter << value;

            int64_t numValue;

            converter >> numValue;

            written = static_cast<std::int32_t>(value.size());

            return put_num(numValue);
        }

        case odbc_native_type::AI_FLOAT:
        case odbc_native_type::AI_DOUBLE:
        {
            std::stringstream converter;

            converter << value;

            double numValue;

            converter >> numValue;

            written = static_cast<std::int32_t>(value.size());

            return put_num(numValue);
        }

        case odbc_native_type::AI_CHAR:
        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        {
            return put_string_to_string_buffer<char>(value, written);
        }

        case odbc_native_type::AI_WCHAR:
        {
            return put_string_to_string_buffer<wchar_t>(value, written);
        }

        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

conversion_result application_data_buffer::put_uuid(const uuid& value)
{
    LOG_MSG("Value: " << value);

    SQLLEN* res_len_ptr = get_result_len();

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        {
            return put_value_to_string_buffer<char>(value);
        }

        case odbc_native_type::AI_WCHAR:
        {
            return put_value_to_string_buffer<wchar_t>(value);
        }

        case odbc_native_type::AI_GUID:
        {
            auto* guid = reinterpret_cast<SQLGUID*>(get_data());

            guid->Data1 = static_cast<uint32_t>(value.get_most_significant_bits() >> 32);
            guid->Data2 = static_cast<uint16_t>(value.get_most_significant_bits() >> 16);
            guid->Data3 = static_cast<uint16_t>(value.get_most_significant_bits());

            std::uint64_t lsb = value.get_least_significant_bits();
            for (std::size_t i = 0; i < sizeof(guid->Data4); ++i)
                guid->Data4[i] = (lsb >> (sizeof(guid->Data4) - i - 1) * 8) & 0xFF;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQLGUID));

            return conversion_result::AI_SUCCESS;
        }

        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

conversion_result application_data_buffer::put_binary_data(void *data, std::size_t len, std::int32_t& written)
{
    switch (m_type)
    {
        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        {
            return put_raw_data_to_buffer(data, len, written);
        }

        case odbc_native_type::AI_CHAR:
        {
            std::stringstream converter;

            for (std::size_t i = 0; i < len; ++i)
            {
                auto *dataBytes = reinterpret_cast<std::uint8_t*>(data);

                converter << std::hex
                          << std::setfill('0')
                          << std::setw(2)
                          << static_cast<unsigned>(dataBytes[i]);
            }

            return put_string_to_string_buffer<char>(converter.str(), written);
        }

        case odbc_native_type::AI_WCHAR:
        {
            std::wstringstream converter;

            for (std::size_t i = 0; i < len; ++i)
            {
                auto *dataBytes = reinterpret_cast<std::uint8_t*>(data);

                converter << std::hex
                          << std::setfill(L'0')
                          << std::setw(2)
                          << static_cast<unsigned>(dataBytes[i]);
            }

            return put_string_to_string_buffer<wchar_t>(converter.str(), written);
        }

        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

conversion_result application_data_buffer::put_null()
{
    SQLLEN* res_len_ptr = get_result_len();

    if (!res_len_ptr)
        return conversion_result::AI_INDICATOR_NEEDED;

    *res_len_ptr = SQL_NULL_DATA;

    return conversion_result::AI_SUCCESS;
}

conversion_result application_data_buffer::put_decimal(const big_decimal& value)
{
    SQLLEN* res_len_ptr = get_result_len();

    switch (m_type)
    {
        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        {
            put_num<int64_t>(value.to_int64());

            return conversion_result::AI_FRACTIONAL_TRUNCATED;
        }

        case odbc_native_type::AI_FLOAT:
        case odbc_native_type::AI_DOUBLE:
        {
            put_num<double>(value.ToDouble());

            return conversion_result::AI_FRACTIONAL_TRUNCATED;
        }

        case odbc_native_type::AI_CHAR:
        case odbc_native_type::AI_WCHAR:
        {
            std::stringstream converter;

            converter << value;

            std::int32_t dummy = 0;

            return put_string(converter.str(), dummy);
        }

        case odbc_native_type::AI_NUMERIC:
        {
            auto* numeric = reinterpret_cast<SQL_NUMERIC_STRUCT*>(get_data());

            big_decimal zero_scaled;
            value.set_scale(0, zero_scaled);

            const big_integer& unscaled = zero_scaled.get_unscaled_value();
            std::vector<std::byte> bytes_buffer = unscaled.to_bytes();

            for (std::int32_t i = 0; i < SQL_MAX_NUMERIC_LEN; ++i)
            {
                std::ptrdiff_t buf_idx = std::ptrdiff_t(bytes_buffer.size()) - 1 - i;
                if (buf_idx >= 0)
                    numeric->val[i] = SQLCHAR(bytes_buffer[buf_idx]);
                else
                    numeric->val[i] = 0;
            }

            numeric->scale = 0;
            numeric->sign = unscaled.get_sign() < 0 ? 0 : 1;
            numeric->precision = unscaled.get_precision();

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

            if (bytes_buffer.size() > SQL_MAX_NUMERIC_LEN)
                return conversion_result::AI_FRACTIONAL_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_DEFAULT:
        case odbc_native_type::AI_BINARY:
        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

conversion_result application_data_buffer::put_date(const ignite_date& value)
{
    tm tm_time{};

    // TODO: IGNITE-19205 Implement date-time type conversion.

    UNUSED_VALUE(value);
    //DateToCTm(value, tm_time);

    SQLLEN* res_len_ptr = get_result_len();
    void* data_ptr = get_data();

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            char* buffer = reinterpret_cast<char*>(data_ptr);
            const std::size_t valLen = sizeof("HHHH-MM-DD") - 1;

            if (res_len_ptr)
                *res_len_ptr = valLen;

            if (buffer)
                strftime(buffer, get_size(), "%Y-%m-%d", &tm_time);

            if (static_cast<SQLLEN>(valLen) + 1 > get_size())
                return conversion_result::AI_VARLEN_DATA_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_WCHAR:
        {
            auto* buffer = reinterpret_cast<SQLWCHAR*>(data_ptr);
            const std::size_t valLen = sizeof("HHHH-MM-DD") - 1;

            if (res_len_ptr)
                *res_len_ptr = valLen;

            if (buffer)
            {
                std::string tmp(valLen + 1, 0);

                strftime(&tmp[0], tmp.size(), "%Y-%m-%d", &tm_time);

                string_to_wstring(&tmp[0], std::int64_t(tmp.size()), buffer, get_size());
            }

            if (static_cast<SQLLEN>(valLen) + 1 > get_size())
                return conversion_result::AI_VARLEN_DATA_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TDATE:
        {
            auto* buffer = reinterpret_cast<SQL_DATE_STRUCT*>(data_ptr);

            buffer->year = SQLSMALLINT(tm_time.tm_year + 1900);
            buffer->month = tm_time.tm_mon + 1;
            buffer->day = tm_time.tm_mday;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TTIME:
        {
            auto* buffer = reinterpret_cast<SQL_TIME_STRUCT*>(data_ptr);

            buffer->hour = tm_time.tm_hour;
            buffer->minute = tm_time.tm_min;
            buffer->second = tm_time.tm_sec;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TTIMESTAMP:
        {
            auto* buffer = reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(data_ptr);

            buffer->year = SQLSMALLINT(tm_time.tm_year + 1900);
            buffer->month = tm_time.tm_mon + 1;
            buffer->day = tm_time.tm_mday;
            buffer->hour = tm_time.tm_hour;
            buffer->minute = tm_time.tm_min;
            buffer->second = tm_time.tm_sec;
            buffer->fraction = 0;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        case odbc_native_type::AI_FLOAT:
        case odbc_native_type::AI_DOUBLE:
        case odbc_native_type::AI_NUMERIC:
        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

conversion_result application_data_buffer::put_timestamp(const ignite_timestamp& value)
{
    tm tm_time{};

    // TODO: IGNITE-19205 Implement date-time type conversion.

    UNUSED_VALUE(value);
    //TimestampToCTm(value, tm_time);

    SQLLEN* res_len_ptr = get_result_len();
    void* data_ptr = get_data();

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            const std::size_t valLen = sizeof("HHHH-MM-DD HH:MM:SS") - 1;

            if (res_len_ptr)
                *res_len_ptr = valLen;

            char* buffer = reinterpret_cast<char*>(data_ptr);

            if (buffer)
                strftime(buffer, get_size(), "%Y-%m-%d %H:%M:%S", &tm_time);

            if (static_cast<SQLLEN>(valLen) + 1 > get_size())
                return conversion_result::AI_VARLEN_DATA_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_WCHAR:
        {
            const std::size_t valLen = sizeof("HHHH-MM-DD HH:MM:SS") - 1;

            if (res_len_ptr)
                *res_len_ptr = valLen;

            auto* buffer = reinterpret_cast<SQLWCHAR*>(data_ptr);

            if (buffer)
            {
                std::string tmp(get_size(), 0);

                strftime(&tmp[0], get_size(), "%Y-%m-%d %H:%M:%S", &tm_time);

                string_to_wstring(&tmp[0], std::int64_t(tmp.size()), buffer, get_size());
            }

            if (static_cast<SQLLEN>(valLen) + 1 > get_size())
                return conversion_result::AI_VARLEN_DATA_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TDATE:
        {
            auto* buffer = reinterpret_cast<SQL_DATE_STRUCT*>(data_ptr);

            buffer->year = SQLSMALLINT(tm_time.tm_year + 1900);
            buffer->month = tm_time.tm_mon + 1;
            buffer->day = tm_time.tm_mday;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

            return conversion_result::AI_FRACTIONAL_TRUNCATED;
        }

        case odbc_native_type::AI_TTIME:
        {
            auto* buffer = reinterpret_cast<SQL_TIME_STRUCT*>(data_ptr);

            buffer->hour = tm_time.tm_hour;
            buffer->minute = tm_time.tm_min;
            buffer->second = tm_time.tm_sec;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

            return conversion_result::AI_FRACTIONAL_TRUNCATED;
        }

        case odbc_native_type::AI_TTIMESTAMP:
        {
            auto* buffer = reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(data_ptr);

            buffer->year = SQLSMALLINT(tm_time.tm_year + 1900);
            buffer->month = tm_time.tm_mon + 1;
            buffer->day = tm_time.tm_mday;
            buffer->hour = tm_time.tm_hour;
            buffer->minute = tm_time.tm_min;
            buffer->second = tm_time.tm_sec;
            buffer->fraction = value.get_nano();

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        case odbc_native_type::AI_FLOAT:
        case odbc_native_type::AI_DOUBLE:
        case odbc_native_type::AI_NUMERIC:
        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

conversion_result application_data_buffer::put_time(const ignite_time& value)
{
    tm tm_time{};

    // TODO: IGNITE-19205 Implement date-time type conversion.

    UNUSED_VALUE(value);
    //TimeToCTm(value, tm_time);

    SQLLEN* res_len_ptr = get_result_len();
    void* data_ptr = get_data();

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            const std::size_t valLen = sizeof("HH:MM:SS") - 1;

            if (res_len_ptr)
                *res_len_ptr = sizeof("HH:MM:SS") - 1;

            char* buffer = reinterpret_cast<char*>(data_ptr);

            if (buffer)
                strftime(buffer, get_size(), "%H:%M:%S", &tm_time);

            if (static_cast<SQLLEN>(valLen) + 1 > get_size())
                return conversion_result::AI_VARLEN_DATA_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_WCHAR:
        {
            const std::size_t valLen = sizeof("HH:MM:SS") - 1;

            if (res_len_ptr)
                *res_len_ptr = sizeof("HH:MM:SS") - 1;

            auto* buffer = reinterpret_cast<SQLWCHAR*>(data_ptr);

            if (buffer)
            {
                std::string tmp(get_size(), 0);

                strftime(&tmp[0], get_size(), "%H:%M:%S", &tm_time);

                string_to_wstring(&tmp[0], std::int64_t(tmp.size()), buffer, get_size());
            }

            if (static_cast<SQLLEN>(valLen) + 1 > get_size())
                return conversion_result::AI_VARLEN_DATA_TRUNCATED;

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TTIME:
        {
            auto* buffer = reinterpret_cast<SQL_TIME_STRUCT*>(data_ptr);

            buffer->hour = tm_time.tm_hour;
            buffer->minute = tm_time.tm_min;
            buffer->second = tm_time.tm_sec;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_TTIMESTAMP:
        {
            auto* buffer = reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(data_ptr);

            buffer->year = SQLSMALLINT(tm_time.tm_year + 1900);
            buffer->month = tm_time.tm_mon + 1;
            buffer->day = tm_time.tm_mday;
            buffer->hour = tm_time.tm_hour;
            buffer->minute = tm_time.tm_min;
            buffer->second = tm_time.tm_sec;
            buffer->fraction = 0;

            if (res_len_ptr)
                *res_len_ptr = static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

            return conversion_result::AI_SUCCESS;
        }

        case odbc_native_type::AI_BINARY:
        case odbc_native_type::AI_DEFAULT:
        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        case odbc_native_type::AI_FLOAT:
        case odbc_native_type::AI_DOUBLE:
        case odbc_native_type::AI_NUMERIC:
        case odbc_native_type::AI_TDATE:
        default:
            break;
    }

    return conversion_result::AI_UNSUPPORTED_CONVERSION;
}

std::string application_data_buffer::get_string(std::size_t maxLen) const
{
    std::string res;

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            std::size_t param_len = get_input_size();

            if (!param_len)
                break;

            res = sql_string_to_string(
                reinterpret_cast<const unsigned char*>(get_data()), static_cast<std::int32_t>(param_len));

            if (res.size() > maxLen)
                res.resize(maxLen);

            break;
        }

        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        {
            std::stringstream converter;
            converter << get_num<int64_t>();

            res = converter.str();

            break;
        }

        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        {
            std::stringstream converter;

            converter << get_num<std::uint64_t>();

            res = converter.str();

            break;
        }

        case odbc_native_type::AI_FLOAT:
        {
            std::stringstream converter;

            converter << get_num<float>();

            res = converter.str();

            break;
        }

        case odbc_native_type::AI_NUMERIC:
        case odbc_native_type::AI_DOUBLE:
        {
            std::stringstream converter;

            converter << get_num<double>();

            res = converter.str();

            break;
        }

        default:
            break;
    }

    return res;
}

int8_t application_data_buffer::get_int8() const
{
    return get_num<int8_t>();
}

int16_t application_data_buffer::get_int16() const
{
    return get_num<int16_t>();
}

std::int32_t application_data_buffer::get_int32() const
{
    return get_num<std::int32_t>();
}

int64_t application_data_buffer::get_int64() const
{
    return get_num<int64_t>();
}

float application_data_buffer::get_float() const
{
    return get_num<float>();
}

double application_data_buffer::get_double() const
{
    return get_num<double>();
}

uuid application_data_buffer::get_uuid() const
{
    uuid res;

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            SQLLEN param_len = get_input_size();

            if (!param_len)
                break;

            std::string str = sql_string_to_string(
                reinterpret_cast<const unsigned char*>(get_data()), static_cast<std::int32_t>(param_len));

            std::stringstream converter;

            converter << str;

            converter >> res;

            break;
        }

        case odbc_native_type::AI_GUID:
        {
            const auto* guid = reinterpret_cast<const SQLGUID*>(get_data());

            std::uint64_t msb = static_cast<std::uint64_t>(guid->Data1) << 32 |
                                static_cast<std::uint64_t>(guid->Data2) << 16 |
                                static_cast<std::uint64_t>(guid->Data3);

            std::uint64_t lsb = 0;

            for (std::size_t i = 0; i < sizeof(guid->Data4); ++i)
                lsb = guid->Data4[i] << (sizeof(guid->Data4) - i - 1) * 8;

            res = uuid(std::int64_t(msb), std::int64_t(lsb));

            break;
        }

        default:
            break;
    }

    return res;
}

const void* application_data_buffer::get_data() const
{
    return apply_offset(m_buffer, get_element_size());
}

const SQLLEN* application_data_buffer::get_result_len() const
{
    return apply_offset(m_result_len, sizeof(*m_result_len));
}

void* application_data_buffer::get_data()
{
    return apply_offset(m_buffer, get_element_size());
}

SQLLEN* application_data_buffer::get_result_len()
{
    return apply_offset(m_result_len, sizeof(*m_result_len));
}


template<typename T, typename V>
inline V LoadPrimitive(const void* data) {
    T res = T();
    std::memcpy(&res, data, sizeof(res));
    return static_cast<V>(res);
}

template<typename T>
T application_data_buffer::get_num() const
{
    T res = T();

    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            SQLLEN param_len = get_input_size();

            if (!param_len)
                break;

            std::string str = get_string(param_len);

            std::stringstream converter;

            converter << str;

            // Workaround for char types which are recognised as
            // symbolyc types and not numeric types.
            if (sizeof(T) == 1)
            {
                short tmp;

                converter >> tmp;

                res = static_cast<T>(tmp);
            }
            else
                converter >> res;

            break;
        }

        case odbc_native_type::AI_SIGNED_TINYINT:
        {
            res = LoadPrimitive<int8_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
        {
            res = LoadPrimitive<std::uint8_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_SIGNED_SHORT:
        {
            res = LoadPrimitive<int16_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_UNSIGNED_SHORT:
        {
            res = LoadPrimitive<uint16_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_SIGNED_LONG:
        {
            res = LoadPrimitive<std::int32_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_UNSIGNED_LONG:
        {
            res = LoadPrimitive<uint32_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_SIGNED_BIGINT:
        {
            res = LoadPrimitive<int64_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_UNSIGNED_BIGINT:
        {
            res = LoadPrimitive<std::uint64_t, T>(get_data());
            break;
        }

        case odbc_native_type::AI_FLOAT:
        {
            res = LoadPrimitive<float, T>(get_data());
            break;
        }

        case odbc_native_type::AI_DOUBLE:
        {
            res = LoadPrimitive<double, T>(get_data());
            break;
        }

        case odbc_native_type::AI_NUMERIC:
        {
            const auto* numeric = reinterpret_cast<const SQL_NUMERIC_STRUCT*>(get_data());

            big_decimal dec(reinterpret_cast<const int8_t*>(numeric->val),
                SQL_MAX_NUMERIC_LEN, numeric->scale, numeric->sign ? 1 : -1, false);

            res = static_cast<T>(dec.to_int64());

            break;
        }

        default:
            break;
    }

    return res;
}

ignite_date application_data_buffer::get_date() const
{
    tm tm_time{};

    std::memset(&tm_time, 0, sizeof(tm_time));

    switch (m_type)
    {
        case odbc_native_type::AI_TDATE:
        {
            const auto* buffer = reinterpret_cast<const SQL_DATE_STRUCT*>(get_data());

            tm_time.tm_year = buffer->year - 1900;
            tm_time.tm_mon = buffer->month - 1;
            tm_time.tm_mday = buffer->day;

            break;
        }

        case odbc_native_type::AI_TTIME:
        {
            const auto* buffer = reinterpret_cast<const SQL_TIME_STRUCT*>(get_data());

            tm_time.tm_year = 70;
            tm_time.tm_mday = 1;
            tm_time.tm_hour = buffer->hour;
            tm_time.tm_min = buffer->minute;
            tm_time.tm_sec = buffer->second;

            break;
        }

        case odbc_native_type::AI_TTIMESTAMP:
        {
            const auto* buffer = reinterpret_cast<const SQL_TIMESTAMP_STRUCT*>(get_data());

            tm_time.tm_year = buffer->year - 1900;
            tm_time.tm_mon = buffer->month - 1;
            tm_time.tm_mday = buffer->day;
            tm_time.tm_hour = buffer->hour;
            tm_time.tm_min = buffer->minute;
            tm_time.tm_sec = buffer->second;

            break;
        }

        case odbc_native_type::AI_CHAR:
        {
            SQLLEN param_len = get_input_size();

            if (!param_len)
                break;

            std::string str = sql_string_to_string(
                reinterpret_cast<const unsigned char*>(get_data()), static_cast<std::int32_t>(param_len));

            sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &tm_time.tm_year, &tm_time.tm_mon, // NOLINT(cert-err34-c)
                &tm_time.tm_mday, &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec);

            tm_time.tm_year = tm_time.tm_year - 1900;
            tm_time.tm_mon = tm_time.tm_mon - 1;

            break;
        }

        default:
            break;
    }

    // TODO: IGNITE-19205 Implement date-time type conversion.
    //return CTmToDate(tm_time);
    UNUSED_VALUE(tm_time);
    return {};
}

ignite_timestamp application_data_buffer::get_timestamp() const
{
    tm tm_time{};

    std::memset(&tm_time, 0, sizeof(tm_time));
    std::int32_t nanos = 0;

    switch (m_type)
    {
        case odbc_native_type::AI_TDATE:
        {
            const auto* buffer = reinterpret_cast<const SQL_DATE_STRUCT*>(get_data());

            tm_time.tm_year = buffer->year - 1900;
            tm_time.tm_mon = buffer->month - 1;
            tm_time.tm_mday = buffer->day;

            break;
        }

        case odbc_native_type::AI_TTIME:
        {
            const auto* buffer = reinterpret_cast<const SQL_TIME_STRUCT*>(get_data());

            tm_time.tm_year = 70;
            tm_time.tm_mday = 1;
            tm_time.tm_hour = buffer->hour;
            tm_time.tm_min = buffer->minute;
            tm_time.tm_sec = buffer->second;

            break;
        }

        case odbc_native_type::AI_TTIMESTAMP:
        {
            const auto* buffer = reinterpret_cast<const SQL_TIMESTAMP_STRUCT*>(get_data());

            tm_time.tm_year = buffer->year - 1900;
            tm_time.tm_mon = buffer->month - 1;
            tm_time.tm_mday = buffer->day;
            tm_time.tm_hour = buffer->hour;
            tm_time.tm_min = buffer->minute;
            tm_time.tm_sec = buffer->second;

            nanos = std::int32_t(buffer->fraction);

            break;
        }

        case odbc_native_type::AI_CHAR:
        {
            SQLLEN param_len = get_input_size();

            if (!param_len)
                break;

            std::string str = sql_string_to_string(
                reinterpret_cast<const unsigned char*>(get_data()), static_cast<std::int32_t>(param_len));

            sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &tm_time.tm_year, &tm_time.tm_mon, // NOLINT(cert-err34-c)
                &tm_time.tm_mday, &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec);

            tm_time.tm_year = tm_time.tm_year - 1900;
            tm_time.tm_mon = tm_time.tm_mon - 1;

            break;
        }

        default:
            break;
    }

    // TODO: IGNITE-19205 Implement date-time type conversion.
    UNUSED_VALUE(nanos);
    //return CTmToTimestamp(tm_time, nanos);
    return {};
}

ignite_time application_data_buffer::get_time() const
{
    tm tm_time{};

    std::memset(&tm_time, 0, sizeof(tm_time));

    tm_time.tm_year = 70;
    tm_time.tm_mon = 0;
    tm_time.tm_mday = 1;

    switch (m_type)
    {
        case odbc_native_type::AI_TTIME:
        {
            const auto* buffer = reinterpret_cast<const SQL_TIME_STRUCT*>(get_data());

            tm_time.tm_hour = buffer->hour;
            tm_time.tm_min = buffer->minute;
            tm_time.tm_sec = buffer->second;

            break;
        }

        case odbc_native_type::AI_TTIMESTAMP:
        {
            const auto* buffer = reinterpret_cast<const SQL_TIMESTAMP_STRUCT*>(get_data());

            tm_time.tm_hour = buffer->hour;
            tm_time.tm_min = buffer->minute;
            tm_time.tm_sec = buffer->second;

            break;
        }

        case odbc_native_type::AI_CHAR:
        {
            SQLLEN param_len = get_input_size();

            if (!param_len)
                break;

            std::string str = sql_string_to_string(
                reinterpret_cast<const unsigned char*>(get_data()), static_cast<std::int32_t>(param_len));

            sscanf(str.c_str(), "%d:%d:%d", &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec); // NOLINT(cert-err34-c)

            break;
        }

        default:
            break;
    }

    // TODO: IGNITE-19205 Implement date-time type conversion.
    //return CTmToTime(tm_time);
    return {};
}

void application_data_buffer::get_decimal(big_decimal& val) const
{
    switch (m_type)
    {
        case odbc_native_type::AI_CHAR:
        {
            SQLLEN param_len = get_input_size();

            if (!param_len)
                break;

            std::string str = get_string(param_len);

            std::stringstream converter;

            converter << str;

            converter >> val;

            break;
        }

        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_SIGNED_BIGINT:
        {
            val.assign_int64(get_num<int64_t>());

            break;
        }

        case odbc_native_type::AI_UNSIGNED_TINYINT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
        {
            val.assign_uint64(get_num<std::uint64_t>());

            break;
        }

        case odbc_native_type::AI_FLOAT:
        case odbc_native_type::AI_DOUBLE:
        {
            val.assign_double(get_num<double>());

            break;
        }

        case odbc_native_type::AI_NUMERIC:
        {
            const auto* numeric = reinterpret_cast<const SQL_NUMERIC_STRUCT*>(get_data());

            big_decimal dec(reinterpret_cast<const int8_t*>(numeric->val),
                SQL_MAX_NUMERIC_LEN, numeric->scale, numeric->sign ? 1 : -1, false);

            val.swap(dec);

            break;
        }

        default:
        {
            val.assign_int64(0);
            break;
        }
    }
}

template<typename T>
T* application_data_buffer::apply_offset(T* ptr, std::size_t elemSize) const
{
    if (!ptr)
        return ptr;

    return get_pointer_with_offset(ptr, m_byte_offset + elemSize * m_element_offset);
}

bool application_data_buffer::is_data_at_exec() const
{
    const SQLLEN* res_len_ptr = get_result_len();
    if (!res_len_ptr)
        return false;

    auto s_len = static_cast<std::int32_t>(*res_len_ptr);
    return s_len <= SQL_LEN_DATA_AT_EXEC_OFFSET || s_len == SQL_DATA_AT_EXEC;
}

SQLLEN application_data_buffer::get_data_at_exec_size() const
{
    switch (m_type)
    {
        case odbc_native_type::AI_WCHAR:
        case odbc_native_type::AI_CHAR:
        case odbc_native_type::AI_BINARY:
        {
            const SQLLEN* res_len_ptr = get_result_len();

            if (!res_len_ptr)
                return 0;

            auto s_len = static_cast<std::int32_t>(*res_len_ptr);

            if (s_len <= SQL_LEN_DATA_AT_EXEC_OFFSET)
                s_len = SQL_LEN_DATA_AT_EXEC(s_len);
            else
                s_len = 0;

            if (m_type == odbc_native_type::AI_WCHAR)
                s_len *= 2;

            return s_len;
        }

        case odbc_native_type::AI_SIGNED_SHORT:
        case odbc_native_type::AI_UNSIGNED_SHORT:
            return static_cast<SQLLEN>(sizeof(short));

        case odbc_native_type::AI_SIGNED_LONG:
        case odbc_native_type::AI_UNSIGNED_LONG:
            return static_cast<SQLLEN>(sizeof(long));

        case odbc_native_type::AI_FLOAT:
            return static_cast<SQLLEN>(sizeof(float));

        case odbc_native_type::AI_DOUBLE:
            return static_cast<SQLLEN>(sizeof(double));

        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_SIGNED_TINYINT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
            return static_cast<SQLLEN>(sizeof(char));

        case odbc_native_type::AI_SIGNED_BIGINT:
        case odbc_native_type::AI_UNSIGNED_BIGINT:
            return static_cast<SQLLEN>(sizeof(SQLBIGINT));

        case odbc_native_type::AI_TDATE:
            return static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

        case odbc_native_type::AI_TTIME:
            return static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

        case odbc_native_type::AI_TTIMESTAMP:
            return static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

        case odbc_native_type::AI_NUMERIC:
            return static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

        case odbc_native_type::AI_GUID:
            return static_cast<SQLLEN>(sizeof(SQLGUID));

        case odbc_native_type::AI_DEFAULT:
        case odbc_native_type::AI_UNSUPPORTED:
        default:
            break;
    }

    return 0;
}

SQLLEN application_data_buffer::get_element_size() const
{
    switch (m_type)
    {
        case odbc_native_type::AI_WCHAR:
        case odbc_native_type::AI_CHAR:
        case odbc_native_type::AI_BINARY:
            return m_buffer_len;

        case odbc_native_type::AI_SIGNED_SHORT:
            return static_cast<SQLLEN>(sizeof(SQLSMALLINT));

        case odbc_native_type::AI_UNSIGNED_SHORT:
            return static_cast<SQLLEN>(sizeof(SQLUSMALLINT));

        case odbc_native_type::AI_SIGNED_LONG:
            return static_cast<SQLLEN>(sizeof(SQLUINTEGER));

        case odbc_native_type::AI_UNSIGNED_LONG:
            return static_cast<SQLLEN>(sizeof(SQLINTEGER));

        case odbc_native_type::AI_FLOAT:
            return static_cast<SQLLEN>(sizeof(SQLREAL));

        case odbc_native_type::AI_DOUBLE:
            return static_cast<SQLLEN>(sizeof(SQLDOUBLE));

        case odbc_native_type::AI_SIGNED_TINYINT:
            return static_cast<SQLLEN>(sizeof(SQLSCHAR));

        case odbc_native_type::AI_BIT:
        case odbc_native_type::AI_UNSIGNED_TINYINT:
            return static_cast<SQLLEN>(sizeof(SQLCHAR));

        case odbc_native_type::AI_SIGNED_BIGINT:
            return static_cast<SQLLEN>(sizeof(SQLBIGINT));

        case odbc_native_type::AI_UNSIGNED_BIGINT:
            return static_cast<SQLLEN>(sizeof(SQLUBIGINT));

        case odbc_native_type::AI_TDATE:
            return static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

        case odbc_native_type::AI_TTIME:
            return static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

        case odbc_native_type::AI_TTIMESTAMP:
            return static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

        case odbc_native_type::AI_NUMERIC:
            return static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

        case odbc_native_type::AI_GUID:
            return static_cast<SQLLEN>(sizeof(SQLGUID));

        case odbc_native_type::AI_DEFAULT:
        case odbc_native_type::AI_UNSUPPORTED:
        default:
            break;
    }

    return 0;
}

SQLLEN application_data_buffer::get_input_size() const
{
    if (!is_data_at_exec()) {
        const SQLLEN *len = get_result_len();
        return len ? *len : SQL_NTS;
    }

    return get_data_at_exec_size();
}

} // namespace ignite
