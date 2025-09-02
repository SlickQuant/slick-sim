/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_EXECREASON_H_
#define _SBE_EXECREASON_H_

#if defined(SBE_HAVE_CMATH)
/* cmath needed for std::numeric_limits<double>::quiet_NaN() */
#  include <cmath>
#  define SBE_FLOAT_NAN std::numeric_limits<float>::quiet_NaN()
#  define SBE_DOUBLE_NAN std::numeric_limits<double>::quiet_NaN()
#else
/* math.h needed for NAN */
#  include <math.h>
#  define SBE_FLOAT_NAN NAN
#  define SBE_DOUBLE_NAN NAN
#endif

#if __cplusplus >= 201103L
#  include <cstdint>
#  include <string>
#  include <cstring>
#endif

#if __cplusplus >= 201103L
#  define SBE_CONSTEXPR constexpr
#  define SBE_NOEXCEPT noexcept
#else
#  define SBE_CONSTEXPR
#  define SBE_NOEXCEPT
#endif

#if __cplusplus >= 201703L
#  define SBE_NODISCARD [[nodiscard]]
#else
#  define SBE_NODISCARD
#endif

#if !defined(__STDC_LIMIT_MACROS)
#  define __STDC_LIMIT_MACROS 1
#endif
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <ostream>
#include <sstream>
#include <iomanip>

#if defined(WIN32) || defined(_WIN32)
#  define SBE_BIG_ENDIAN_ENCODE_16(v) _byteswap_ushort(v)
#  define SBE_BIG_ENDIAN_ENCODE_32(v) _byteswap_ulong(v)
#  define SBE_BIG_ENDIAN_ENCODE_64(v) _byteswap_uint64(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_16(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_32(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_64(v) (v)
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define SBE_BIG_ENDIAN_ENCODE_16(v) __builtin_bswap16(v)
#  define SBE_BIG_ENDIAN_ENCODE_32(v) __builtin_bswap32(v)
#  define SBE_BIG_ENDIAN_ENCODE_64(v) __builtin_bswap64(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_16(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_32(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_64(v) (v)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define SBE_LITTLE_ENDIAN_ENCODE_16(v) __builtin_bswap16(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_32(v) __builtin_bswap32(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_64(v) __builtin_bswap64(v)
#  define SBE_BIG_ENDIAN_ENCODE_16(v) (v)
#  define SBE_BIG_ENDIAN_ENCODE_32(v) (v)
#  define SBE_BIG_ENDIAN_ENCODE_64(v) (v)
#else
#  error "Byte Ordering of platform not determined. Set __BYTE_ORDER__ manually before including this file."
#endif

#if defined(SBE_NO_BOUNDS_CHECK)
#  define SBE_BOUNDS_CHECK_EXPECT(exp,c) (false)
#elif defined(_MSC_VER)
#  define SBE_BOUNDS_CHECK_EXPECT(exp,c) (exp)
#else
#  define SBE_BOUNDS_CHECK_EXPECT(exp,c) (__builtin_expect(exp,c))
#endif

#define SBE_NULLVALUE_INT8 (std::numeric_limits<std::int8_t>::min)()
#define SBE_NULLVALUE_INT16 (std::numeric_limits<std::int16_t>::min)()
#define SBE_NULLVALUE_INT32 (std::numeric_limits<std::int32_t>::min)()
#define SBE_NULLVALUE_INT64 (std::numeric_limits<std::int64_t>::min)()
#define SBE_NULLVALUE_UINT8 (std::numeric_limits<std::uint8_t>::max)()
#define SBE_NULLVALUE_UINT16 (std::numeric_limits<std::uint16_t>::max)()
#define SBE_NULLVALUE_UINT32 (std::numeric_limits<std::uint32_t>::max)()
#define SBE_NULLVALUE_UINT64 (std::numeric_limits<std::uint64_t>::max)()


namespace sbe {

class ExecReason
{
public:
    enum Value
    {
        MarketExchangeOption = (std::uint8_t)8,
        CancelledNotBest = (std::uint8_t)9,
        CancelOnDisconnect = (std::uint8_t)100,
        SelfMatchPreventionOldestOrderCancelled = (std::uint8_t)103,
        CancelOnGlobexCreditControlsViolation = (std::uint8_t)104,
        CancelFromFirmsoft = (std::uint8_t)105,
        CancelFromRiskManagementAPI = (std::uint8_t)106,
        SelfMatchPreventionNewestOrderCancelled = (std::uint8_t)107,
        Cancelduetovolquotedoptionorderrestedqtylessthanminordersize = (std::uint8_t)108,
        CancelRFCOrder = (std::uint8_t)109,
        CancelUponContractExpiration = (std::uint8_t)110,
        SystemCancel = (std::uint8_t)111,
        NULL_VALUE = (std::uint8_t)255
    };

    static ExecReason::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case 8: return MarketExchangeOption;
            case 9: return CancelledNotBest;
            case 100: return CancelOnDisconnect;
            case 103: return SelfMatchPreventionOldestOrderCancelled;
            case 104: return CancelOnGlobexCreditControlsViolation;
            case 105: return CancelFromFirmsoft;
            case 106: return CancelFromRiskManagementAPI;
            case 107: return SelfMatchPreventionNewestOrderCancelled;
            case 108: return Cancelduetovolquotedoptionorderrestedqtylessthanminordersize;
            case 109: return CancelRFCOrder;
            case 110: return CancelUponContractExpiration;
            case 111: return SystemCancel;
            case 255: return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum ExecReason [E103]");
    }

    static const char* c_str(const ExecReason::Value value)
    {
        switch (value)
        {
            case MarketExchangeOption: return "MarketExchangeOption";
            case CancelledNotBest: return "CancelledNotBest";
            case CancelOnDisconnect: return "CancelOnDisconnect";
            case SelfMatchPreventionOldestOrderCancelled: return "SelfMatchPreventionOldestOrderCancelled";
            case CancelOnGlobexCreditControlsViolation: return "CancelOnGlobexCreditControlsViolation";
            case CancelFromFirmsoft: return "CancelFromFirmsoft";
            case CancelFromRiskManagementAPI: return "CancelFromRiskManagementAPI";
            case SelfMatchPreventionNewestOrderCancelled: return "SelfMatchPreventionNewestOrderCancelled";
            case Cancelduetovolquotedoptionorderrestedqtylessthanminordersize: return "Cancelduetovolquotedoptionorderrestedqtylessthanminordersize";
            case CancelRFCOrder: return "CancelRFCOrder";
            case CancelUponContractExpiration: return "CancelUponContractExpiration";
            case SystemCancel: return "SystemCancel";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum ExecReason [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& os, ExecReason::Value m)
    {
        return os << ExecReason::c_str(m);
    }
};

}

#endif
