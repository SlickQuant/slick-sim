/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_MASSQUOTE517_H_
#define _SBE_MASSQUOTE517_H_

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


#include "MaturityMonthYear.h"
#include "Decimal64NULL.h"
#include "OrderTypeReq.h"
#include "BooleanNULL.h"
#include "AvgPxInd.h"
#include "OrderEventType.h"
#include "CmtaGiveUpCD.h"
#include "PRICENULL9.h"
#include "BooleanFlag.h"
#include "OrderType.h"
#include "QuoteCxlStatus.h"
#include "TimeInForce.h"
#include "PRICE9.h"
#include "MassStatusOrdTyp.h"
#include "ExecInst.h"
#include "Decimal32NULL.h"
#include "SplitMsg.h"
#include "SideReq.h"
#include "MassActionResponse.h"
#include "CustOrderCapacity.h"
#include "SideTimeInForce.h"
#include "SLEDS.h"
#include "ReqResult.h"
#include "ClearingAcctType.h"
#include "KeepAliveLapsed.h"
#include "ExecAckStatus.h"
#include "DATA.h"
#include "OFMOverrideReq.h"
#include "SideNULL.h"
#include "GroupSize.h"
#include "ListUpdAct.h"
#include "FTI.h"
#include "ExecReason.h"
#include "MassStatusReqTyp.h"
#include "DKReason.h"
#include "SecRspTyp.h"
#include "MassActionOrdTyp.h"
#include "QuoteAckStatus.h"
#include "ExpCycle.h"
#include "MassCancelTIF.h"
#include "OrderStatus.h"
#include "ShortSaleType.h"
#include "PartyDetailRole.h"
#include "MassActionScope.h"
#include "RFQSide.h"
#include "OrdStatusTrdCxl.h"
#include "TradeAddendum.h"
#include "MessageHeader.h"
#include "ManualOrdInd.h"
#include "QuoteCxlTyp.h"
#include "MassCxlReqTyp.h"
#include "ExecTypTrdCxl.h"
#include "ManualOrdIndReq.h"
#include "CustOrdHandlInst.h"
#include "OrdStatusTrd.h"
#include "MassStatusTIF.h"
#include "SMPI.h"
#include "ExecMode.h"
#include "QuoteTyp.h"

namespace sbe {

class MassQuote517
{
private:
    char *m_buffer = nullptr;
    std::uint64_t m_bufferLength = 0;
    std::uint64_t m_offset = 0;
    std::uint64_t m_position = 0;
    std::uint64_t m_actingVersion = 0;

    inline std::uint64_t *sbePositionPtr() SBE_NOEXCEPT
    {
        return &m_position;
    }

public:
    enum MetaAttribute
    {
        EPOCH, TIME_UNIT, SEMANTIC_TYPE, PRESENCE
    };

    union sbe_float_as_uint_u
    {
        float fp_value;
        std::uint32_t uint_value;
    };

    union sbe_double_as_uint_u
    {
        double fp_value;
        std::uint64_t uint_value;
    };

    MassQuote517() = default;

    MassQuote517(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        m_buffer(buffer),
        m_bufferLength(bufferLength),
        m_offset(offset),
        m_position(sbeCheckPosition(offset + actingBlockLength)),
        m_actingVersion(actingVersion)
    {
    }

    MassQuote517(char *buffer, const std::uint64_t bufferLength) :
        MassQuote517(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    MassQuote517(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        MassQuote517(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return (std::uint16_t)123;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return (std::uint16_t)517;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaId() SBE_NOEXCEPT
    {
        return (std::uint16_t)8;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaVersion() SBE_NOEXCEPT
    {
        return (std::uint16_t)8;
    }

    SBE_NODISCARD static SBE_CONSTEXPR const char * sbeSemanticType() SBE_NOEXCEPT
    {
        return "i";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    MassQuote517 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = MassQuote517(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    MassQuote517 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        MessageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = MassQuote517(
            buffer,
            offset + MessageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    MassQuote517 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = MassQuote517(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    SBE_NODISCARD std::uint64_t sbePosition() const SBE_NOEXCEPT
    {
        return m_position;
    }

    std::uint64_t sbeCheckPosition(const std::uint64_t position)
    {
        if (SBE_BOUNDS_CHECK_EXPECT((position > m_bufferLength), false))
        {
            throw std::runtime_error("buffer too short [E100]");
        }
        return position;
    }

    void sbePosition(const std::uint64_t position)
    {
        m_position = sbeCheckPosition(position);
    }

    SBE_NODISCARD std::uint64_t encodedLength() const SBE_NOEXCEPT
    {
        return sbePosition() - m_offset;
    }

    SBE_NODISCARD const char * buffer() const SBE_NOEXCEPT
    {
        return m_buffer;
    }

    SBE_NODISCARD char * buffer() SBE_NOEXCEPT
    {
        return m_buffer;
    }

    SBE_NODISCARD std::uint64_t bufferLength() const SBE_NOEXCEPT
    {
        return m_bufferLength;
    }

    SBE_NODISCARD std::uint64_t actingVersion() const SBE_NOEXCEPT
    {
        return m_actingVersion;
    }

    SBE_NODISCARD static const char * PartyDetailsListReqIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t partyDetailsListReqIDId() SBE_NOEXCEPT
    {
        return 1505;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool partyDetailsListReqIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= partyDetailsListReqIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyDetailsListReqIDEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t partyDetailsListReqIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t partyDetailsListReqID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    MassQuote517 &partyDetailsListReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * SendingTimeEpochMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t sendingTimeEpochId() SBE_NOEXCEPT
    {
        return 5297;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sendingTimeEpochSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool sendingTimeEpochInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= sendingTimeEpochSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sendingTimeEpochEncodingOffset() SBE_NOEXCEPT
    {
        return 8;
    }

    static SBE_CONSTEXPR std::uint64_t sendingTimeEpochNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t sendingTimeEpochMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t sendingTimeEpochMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t sendingTimeEpochEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t sendingTimeEpoch() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    MassQuote517 &sendingTimeEpoch(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * ManualOrderIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t manualOrderIndicatorId() SBE_NOEXCEPT
    {
        return 1028;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t manualOrderIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool manualOrderIndicatorInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= manualOrderIndicatorSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t manualOrderIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 16;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t manualOrderIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD ManualOrdIndReq::Value manualOrderIndicator() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 16, sizeof(std::uint8_t));
        return ManualOrdIndReq::get((val));
    }

    MassQuote517 &manualOrderIndicator(const ManualOrdIndReq::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 16, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * SeqNumMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t seqNumId() SBE_NOEXCEPT
    {
        return 9726;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t seqNumSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool seqNumInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= seqNumSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t seqNumEncodingOffset() SBE_NOEXCEPT
    {
        return 17;
    }

    static SBE_CONSTEXPR std::uint32_t seqNumNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t seqNumMinValue() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint32_t seqNumMaxValue() SBE_NOEXCEPT
    {
        return 4294967294;
    }

    static SBE_CONSTEXPR std::size_t seqNumEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t seqNum() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 17, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    MassQuote517 &seqNum(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 17, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char * SenderIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t senderIDId() SBE_NOEXCEPT
    {
        return 5392;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t senderIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool senderIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= senderIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t senderIDEncodingOffset() SBE_NOEXCEPT
    {
        return 21;
    }

    static SBE_CONSTEXPR char senderIDNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char senderIDMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char senderIDMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t senderIDEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t senderIDLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *senderID() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 21;
    }

    SBE_NODISCARD char *senderID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 21;
    }

    SBE_NODISCARD char senderID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 21 + (index * 1), sizeof(char));
        return (val);
    }

    MassQuote517 &senderID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 21 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSenderID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getSenderID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 21, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    MassQuote517 &putSenderID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 21, src, sizeof(char) * 20);
        return *this;
    }

    std::string getSenderIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 21;
        size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSenderIDAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSenderIDAsString();

        for (auto c = s.cbegin(); c != s.cend(); c++)
        {
            switch (*c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= *c && *c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(*c);
                    }
                    else
                    {
                        oss << *c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    std::string_view getSenderIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 21;
        size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MassQuote517 &putSenderID(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 21, str.data(), srcLength);
        for (size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 21 + start] = 0;
        }

        return *this;
    }
    #else
    MassQuote517 &putSenderID(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 21, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 21 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * QuoteReqIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t quoteReqIDId() SBE_NOEXCEPT
    {
        return 131;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t quoteReqIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool quoteReqIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= quoteReqIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteReqIDEncodingOffset() SBE_NOEXCEPT
    {
        return 41;
    }

    static SBE_CONSTEXPR std::uint64_t quoteReqIDNullValue() SBE_NOEXCEPT
    {
        return 0xffffffffffffffffL;
    }

    static SBE_CONSTEXPR std::uint64_t quoteReqIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t quoteReqIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t quoteReqIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t quoteReqID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 41, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    MassQuote517 &quoteReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 41, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * LocationMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t locationId() SBE_NOEXCEPT
    {
        return 9537;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t locationSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool locationInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= locationSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t locationEncodingOffset() SBE_NOEXCEPT
    {
        return 49;
    }

    static SBE_CONSTEXPR char locationNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char locationMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char locationMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t locationEncodingLength() SBE_NOEXCEPT
    {
        return 5;
    }

    static SBE_CONSTEXPR std::uint64_t locationLength() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD const char *location() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 49;
    }

    SBE_NODISCARD char *location() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 49;
    }

    SBE_NODISCARD char location(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 49 + (index * 1), sizeof(char));
        return (val);
    }

    MassQuote517 &location(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 49 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getLocation(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getLocation [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 49, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    MassQuote517 &putLocation(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 49, src, sizeof(char) * 5);
        return *this;
    }

    std::string getLocationAsString() const
    {
        const char *buffer = m_buffer + m_offset + 49;
        size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getLocationAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getLocationAsString();

        for (auto c = s.cbegin(); c != s.cend(); c++)
        {
            switch (*c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= *c && *c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(*c);
                    }
                    else
                    {
                        oss << *c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    std::string_view getLocationAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 49;
        size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MassQuote517 &putLocation(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 49, str.data(), srcLength);
        for (size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 49 + start] = 0;
        }

        return *this;
    }
    #else
    MassQuote517 &putLocation(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 49, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 49 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * QuoteIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t quoteIDId() SBE_NOEXCEPT
    {
        return 117;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t quoteIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool quoteIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= quoteIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteIDEncodingOffset() SBE_NOEXCEPT
    {
        return 54;
    }

    static SBE_CONSTEXPR std::uint32_t quoteIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t quoteIDMinValue() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint32_t quoteIDMaxValue() SBE_NOEXCEPT
    {
        return 4294967294;
    }

    static SBE_CONSTEXPR std::size_t quoteIDEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t quoteID() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 54, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    MassQuote517 &quoteID(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 54, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char * TotNoQuoteEntriesMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t totNoQuoteEntriesId() SBE_NOEXCEPT
    {
        return 304;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t totNoQuoteEntriesSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool totNoQuoteEntriesInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= totNoQuoteEntriesSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t totNoQuoteEntriesEncodingOffset() SBE_NOEXCEPT
    {
        return 58;
    }

    static SBE_CONSTEXPR std::uint8_t totNoQuoteEntriesNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT8;
    }

    static SBE_CONSTEXPR std::uint8_t totNoQuoteEntriesMinValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)0;
    }

    static SBE_CONSTEXPR std::uint8_t totNoQuoteEntriesMaxValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)254;
    }

    static SBE_CONSTEXPR std::size_t totNoQuoteEntriesEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t totNoQuoteEntries() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 58, sizeof(std::uint8_t));
        return (val);
    }

    MassQuote517 &totNoQuoteEntries(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 58, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * MMProtectionResetMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t mMProtectionResetId() SBE_NOEXCEPT
    {
        return 9773;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t mMProtectionResetSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool mMProtectionResetInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= mMProtectionResetSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mMProtectionResetEncodingOffset() SBE_NOEXCEPT
    {
        return 59;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mMProtectionResetEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD BooleanFlag::Value mMProtectionReset() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 59, sizeof(std::uint8_t));
        return BooleanFlag::get((val));
    }

    MassQuote517 &mMProtectionReset(const BooleanFlag::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 59, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * LiquidityFlagMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t liquidityFlagId() SBE_NOEXCEPT
    {
        return 9373;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t liquidityFlagSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool liquidityFlagInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= liquidityFlagSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t liquidityFlagEncodingOffset() SBE_NOEXCEPT
    {
        return 60;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t liquidityFlagEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD BooleanNULL::Value liquidityFlag() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 60, sizeof(std::uint8_t));
        return BooleanNULL::get((val));
    }

    MassQuote517 &liquidityFlag(const BooleanNULL::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 60, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * ShortSaleTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t shortSaleTypeId() SBE_NOEXCEPT
    {
        return 5409;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t shortSaleTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool shortSaleTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= shortSaleTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t shortSaleTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 61;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t shortSaleTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD ShortSaleType::Value shortSaleType() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 61, sizeof(std::uint8_t));
        return ShortSaleType::get((val));
    }

    MassQuote517 &shortSaleType(const ShortSaleType::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 61, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * ReservedMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t reservedId() SBE_NOEXCEPT
    {
        return 5187;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t reservedSinceVersion() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD bool reservedInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= reservedSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t reservedEncodingOffset() SBE_NOEXCEPT
    {
        return 62;
    }

    static SBE_CONSTEXPR char reservedNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char reservedMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char reservedMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t reservedEncodingLength() SBE_NOEXCEPT
    {
        return 30;
    }

    static SBE_CONSTEXPR std::uint64_t reservedLength() SBE_NOEXCEPT
    {
        return 30;
    }

    SBE_NODISCARD const char *reserved() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 5)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 62;
    }

    SBE_NODISCARD char *reserved() SBE_NOEXCEPT
    {
        if (m_actingVersion < 5)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 62;
    }

    SBE_NODISCARD char reserved(const std::uint64_t index) const
    {
        if (index >= 30)
        {
            throw std::runtime_error("index out of range for reserved [E104]");
        }

        if (m_actingVersion < 5)
        {
            return (char)0;
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 62 + (index * 1), sizeof(char));
        return (val);
    }

    MassQuote517 &reserved(const std::uint64_t index, const char value)
    {
        if (index >= 30)
        {
            throw std::runtime_error("index out of range for reserved [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 62 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getReserved(char *const dst, const std::uint64_t length) const
    {
        if (length > 30)
        {
            throw std::runtime_error("length too large for getReserved [E106]");
        }

        if (m_actingVersion < 5)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 62, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    MassQuote517 &putReserved(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 62, src, sizeof(char) * 30);
        return *this;
    }

    std::string getReservedAsString() const
    {
        const char *buffer = m_buffer + m_offset + 62;
        size_t length = 0;

        for (; length < 30 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getReservedAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getReservedAsString();

        for (auto c = s.cbegin(); c != s.cend(); c++)
        {
            switch (*c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= *c && *c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(*c);
                    }
                    else
                    {
                        oss << *c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    std::string_view getReservedAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 62;
        size_t length = 0;

        for (; length < 30 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MassQuote517 &putReserved(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 30)
        {
            throw std::runtime_error("string too large for putReserved [E106]");
        }

        std::memcpy(m_buffer + m_offset + 62, str.data(), srcLength);
        for (size_t start = srcLength; start < 30; ++start)
        {
            m_buffer[m_offset + 62 + start] = 0;
        }

        return *this;
    }
    #else
    MassQuote517 &putReserved(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 30)
        {
            throw std::runtime_error("string too large for putReserved [E106]");
        }

        std::memcpy(m_buffer + m_offset + 62, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 30; ++start)
        {
            m_buffer[m_offset + 62 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * Reserved1MetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t reserved1Id() SBE_NOEXCEPT
    {
        return 5239;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t reserved1SinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool reserved1InActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= reserved1SinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t reserved1EncodingOffset() SBE_NOEXCEPT
    {
        return 92;
    }

    static SBE_CONSTEXPR char reserved1NullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char reserved1MinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char reserved1MaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t reserved1EncodingLength() SBE_NOEXCEPT
    {
        return 30;
    }

    static SBE_CONSTEXPR std::uint64_t reserved1Length() SBE_NOEXCEPT
    {
        return 30;
    }

    SBE_NODISCARD const char *reserved1() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 92;
    }

    SBE_NODISCARD char *reserved1() SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 92;
    }

    SBE_NODISCARD char reserved1(const std::uint64_t index) const
    {
        if (index >= 30)
        {
            throw std::runtime_error("index out of range for reserved1 [E104]");
        }

        if (m_actingVersion < 8)
        {
            return (char)0;
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 92 + (index * 1), sizeof(char));
        return (val);
    }

    MassQuote517 &reserved1(const std::uint64_t index, const char value)
    {
        if (index >= 30)
        {
            throw std::runtime_error("index out of range for reserved1 [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 92 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getReserved1(char *const dst, const std::uint64_t length) const
    {
        if (length > 30)
        {
            throw std::runtime_error("length too large for getReserved1 [E106]");
        }

        if (m_actingVersion < 8)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 92, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    MassQuote517 &putReserved1(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 92, src, sizeof(char) * 30);
        return *this;
    }

    std::string getReserved1AsString() const
    {
        const char *buffer = m_buffer + m_offset + 92;
        size_t length = 0;

        for (; length < 30 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getReserved1AsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getReserved1AsString();

        for (auto c = s.cbegin(); c != s.cend(); c++)
        {
            switch (*c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= *c && *c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(*c);
                    }
                    else
                    {
                        oss << *c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    std::string_view getReserved1AsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 92;
        size_t length = 0;

        for (; length < 30 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MassQuote517 &putReserved1(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 30)
        {
            throw std::runtime_error("string too large for putReserved1 [E106]");
        }

        std::memcpy(m_buffer + m_offset + 92, str.data(), srcLength);
        for (size_t start = srcLength; start < 30; ++start)
        {
            m_buffer[m_offset + 92 + start] = 0;
        }

        return *this;
    }
    #else
    MassQuote517 &putReserved1(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 30)
        {
            throw std::runtime_error("string too large for putReserved1 [E106]");
        }

        std::memcpy(m_buffer + m_offset + 92, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 30; ++start)
        {
            m_buffer[m_offset + 92 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * QuoteEntryOpenMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t quoteEntryOpenId() SBE_NOEXCEPT
    {
        return 9182;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t quoteEntryOpenSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool quoteEntryOpenInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= quoteEntryOpenSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteEntryOpenEncodingOffset() SBE_NOEXCEPT
    {
        return 122;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteEntryOpenEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD BooleanNULL::Value quoteEntryOpen() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return BooleanNULL::NULL_VALUE;
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 122, sizeof(std::uint8_t));
        return BooleanNULL::get((val));
    }

    MassQuote517 &quoteEntryOpen(const BooleanNULL::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 122, &val, sizeof(std::uint8_t));
        return *this;
    }

    class NoQuoteEntries
    {
    private:
        char *m_buffer = nullptr;
        std::uint64_t m_bufferLength = 0;
        std::uint64_t m_initialPosition = 0;
        std::uint64_t *m_positionPtr = nullptr;
        std::uint64_t m_blockLength = 0;
        std::uint64_t m_count = 0;
        std::uint64_t m_index = 0;
        std::uint64_t m_offset = 0;
        std::uint64_t m_actingVersion = 0;

        SBE_NODISCARD std::uint64_t *sbePositionPtr() SBE_NOEXCEPT
        {
            return m_positionPtr;
        }

    public:
        inline void wrapForDecode(
            char *buffer,
            std::uint64_t *pos,
            const std::uint64_t actingVersion,
            const std::uint64_t bufferLength)
        {
            GroupSize dimensions(buffer, *pos, bufferLength, actingVersion);
            m_buffer = buffer;
            m_bufferLength = bufferLength;
            m_blockLength = dimensions.blockLength();
            m_count = dimensions.numInGroup();
            m_index = 0;
            m_actingVersion = actingVersion;
            m_initialPosition = *pos;
            m_positionPtr = pos;
            *m_positionPtr = *m_positionPtr + 3;
        }

        inline void wrapForEncode(
            char *buffer,
            const std::uint8_t count,
            std::uint64_t *pos,
            const std::uint64_t actingVersion,
            const std::uint64_t bufferLength)
        {
    #if defined(__GNUG__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    #endif
            if (count > 254)
            {
                throw std::runtime_error("count outside of allowed range [E110]");
            }
    #if defined(__GNUG__) && !defined(__clang__)
    #pragma GCC diagnostic pop
    #endif
            m_buffer = buffer;
            m_bufferLength = bufferLength;
            GroupSize dimensions(buffer, *pos, bufferLength, actingVersion);
            dimensions.blockLength((std::uint16_t)38);
            dimensions.numInGroup((std::uint8_t)count);
            m_index = 0;
            m_count = count;
            m_blockLength = 38;
            m_actingVersion = actingVersion;
            m_initialPosition = *pos;
            m_positionPtr = pos;
            *m_positionPtr = *m_positionPtr + 3;
        }

        static SBE_CONSTEXPR std::uint64_t sbeHeaderSize() SBE_NOEXCEPT
        {
            return 3;
        }

        static SBE_CONSTEXPR std::uint64_t sbeBlockLength() SBE_NOEXCEPT
        {
            return 38;
        }

        SBE_NODISCARD std::uint64_t sbePosition() const SBE_NOEXCEPT
        {
            return *m_positionPtr;
        }

        std::uint64_t sbeCheckPosition(const std::uint64_t position)
        {
            if (SBE_BOUNDS_CHECK_EXPECT((position > m_bufferLength), false))
            {
                throw std::runtime_error("buffer too short [E100]");
            }
            return position;
        }

        void sbePosition(const std::uint64_t position)
        {
            *m_positionPtr = sbeCheckPosition(position);
        }

        SBE_NODISCARD inline std::uint64_t count() const SBE_NOEXCEPT
        {
            return m_count;
        }

        SBE_NODISCARD inline bool hasNext() const SBE_NOEXCEPT
        {
            return m_index < m_count;
        }

        inline NoQuoteEntries &next()
        {
            if (m_index >= m_count)
            {
                throw std::runtime_error("index >= count [E108]");
            }
            m_offset = *m_positionPtr;
            if (SBE_BOUNDS_CHECK_EXPECT(((m_offset + m_blockLength) > m_bufferLength), false))
            {
                throw std::runtime_error("buffer too short for next group index [E108]");
            }
            *m_positionPtr = m_offset + m_blockLength;
            ++m_index;

            return *this;
        }

        inline std::uint64_t resetCountToIndex() SBE_NOEXCEPT
        {
            m_count = m_index;
            GroupSize dimensions(m_buffer, m_initialPosition, m_bufferLength, m_actingVersion);
            dimensions.numInGroup((std::uint8_t)m_count);
            return m_count;
        }

    #if __cplusplus < 201103L
        template<class Func> inline void forEach(Func& func)
        {
            while (hasNext())
            {
                next();
                func(*this);
            }
        }

    #else
        template<class Func> inline void forEach(Func&& func)
        {
            while (hasNext())
            {
                next();
                func(*this);
            }
        }

    #endif

        SBE_NODISCARD static const char * BidPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "Price";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t bidPxId() SBE_NOEXCEPT
        {
            return 132;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t bidPxSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool bidPxInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= bidPxSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t bidPxEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

private:
        PRICENULL9 m_bidPx;

public:
        SBE_NODISCARD PRICENULL9 &bidPx()
        {
            m_bidPx.wrap(m_buffer, m_offset + 0, m_actingVersion, m_bufferLength);
            return m_bidPx;
        }

        SBE_NODISCARD static const char * OfferPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "Price";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t offerPxId() SBE_NOEXCEPT
        {
            return 133;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t offerPxSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool offerPxInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= offerPxSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t offerPxEncodingOffset() SBE_NOEXCEPT
        {
            return 8;
        }

private:
        PRICENULL9 m_offerPx;

public:
        SBE_NODISCARD PRICENULL9 &offerPx()
        {
            m_offerPx.wrap(m_buffer, m_offset + 8, m_actingVersion, m_bufferLength);
            return m_offerPx;
        }

        SBE_NODISCARD static const char * QuoteEntryIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t quoteEntryIDId() SBE_NOEXCEPT
        {
            return 299;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t quoteEntryIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool quoteEntryIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= quoteEntryIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteEntryIDEncodingOffset() SBE_NOEXCEPT
        {
            return 16;
        }

        static SBE_CONSTEXPR std::uint32_t quoteEntryIDNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT32;
        }

        static SBE_CONSTEXPR std::uint32_t quoteEntryIDMinValue() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint32_t quoteEntryIDMaxValue() SBE_NOEXCEPT
        {
            return 4294967294;
        }

        static SBE_CONSTEXPR std::size_t quoteEntryIDEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::uint32_t quoteEntryID() const SBE_NOEXCEPT
        {
            std::uint32_t val;
            std::memcpy(&val, m_buffer + m_offset + 16, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteEntries &quoteEntryID(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 16, &val, sizeof(std::uint32_t));
            return *this;
        }

        SBE_NODISCARD static const char * SecurityIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t securityIDId() SBE_NOEXCEPT
        {
            return 48;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool securityIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= securityIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityIDEncodingOffset() SBE_NOEXCEPT
        {
            return 20;
        }

        static SBE_CONSTEXPR std::int32_t securityIDNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_INT32;
        }

        static SBE_CONSTEXPR std::int32_t securityIDMinValue() SBE_NOEXCEPT
        {
            return -2147483647;
        }

        static SBE_CONSTEXPR std::int32_t securityIDMaxValue() SBE_NOEXCEPT
        {
            return 2147483647;
        }

        static SBE_CONSTEXPR std::size_t securityIDEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::int32_t securityID() const SBE_NOEXCEPT
        {
            std::int32_t val;
            std::memcpy(&val, m_buffer + m_offset + 20, sizeof(std::int32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteEntries &securityID(const std::int32_t value) SBE_NOEXCEPT
        {
            std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 20, &val, sizeof(std::int32_t));
            return *this;
        }

        SBE_NODISCARD static const char * BidSizeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t bidSizeId() SBE_NOEXCEPT
        {
            return 134;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t bidSizeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool bidSizeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= bidSizeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t bidSizeEncodingOffset() SBE_NOEXCEPT
        {
            return 24;
        }

        static SBE_CONSTEXPR std::uint32_t bidSizeNullValue() SBE_NOEXCEPT
        {
            return 4294967295;
        }

        static SBE_CONSTEXPR std::uint32_t bidSizeMinValue() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint32_t bidSizeMaxValue() SBE_NOEXCEPT
        {
            return 4294967294;
        }

        static SBE_CONSTEXPR std::size_t bidSizeEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::uint32_t bidSize() const SBE_NOEXCEPT
        {
            std::uint32_t val;
            std::memcpy(&val, m_buffer + m_offset + 24, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteEntries &bidSize(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 24, &val, sizeof(std::uint32_t));
            return *this;
        }

        SBE_NODISCARD static const char * OfferSizeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t offerSizeId() SBE_NOEXCEPT
        {
            return 135;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t offerSizeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool offerSizeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= offerSizeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t offerSizeEncodingOffset() SBE_NOEXCEPT
        {
            return 28;
        }

        static SBE_CONSTEXPR std::uint32_t offerSizeNullValue() SBE_NOEXCEPT
        {
            return 4294967295;
        }

        static SBE_CONSTEXPR std::uint32_t offerSizeMinValue() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint32_t offerSizeMaxValue() SBE_NOEXCEPT
        {
            return 4294967294;
        }

        static SBE_CONSTEXPR std::size_t offerSizeEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::uint32_t offerSize() const SBE_NOEXCEPT
        {
            std::uint32_t val;
            std::memcpy(&val, m_buffer + m_offset + 28, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteEntries &offerSize(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 28, &val, sizeof(std::uint32_t));
            return *this;
        }

        SBE_NODISCARD static const char * UnderlyingSecurityIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t underlyingSecurityIDId() SBE_NOEXCEPT
        {
            return 309;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t underlyingSecurityIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool underlyingSecurityIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= underlyingSecurityIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t underlyingSecurityIDEncodingOffset() SBE_NOEXCEPT
        {
            return 32;
        }

        static SBE_CONSTEXPR std::int32_t underlyingSecurityIDNullValue() SBE_NOEXCEPT
        {
            return 2147483647;
        }

        static SBE_CONSTEXPR std::int32_t underlyingSecurityIDMinValue() SBE_NOEXCEPT
        {
            return -2147483647;
        }

        static SBE_CONSTEXPR std::int32_t underlyingSecurityIDMaxValue() SBE_NOEXCEPT
        {
            return 2147483647;
        }

        static SBE_CONSTEXPR std::size_t underlyingSecurityIDEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::int32_t underlyingSecurityID() const SBE_NOEXCEPT
        {
            std::int32_t val;
            std::memcpy(&val, m_buffer + m_offset + 32, sizeof(std::int32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteEntries &underlyingSecurityID(const std::int32_t value) SBE_NOEXCEPT
        {
            std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 32, &val, sizeof(std::int32_t));
            return *this;
        }

        SBE_NODISCARD static const char * QuoteSetIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t quoteSetIDId() SBE_NOEXCEPT
        {
            return 302;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t quoteSetIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool quoteSetIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= quoteSetIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteSetIDEncodingOffset() SBE_NOEXCEPT
        {
            return 36;
        }

        static SBE_CONSTEXPR std::uint16_t quoteSetIDNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT16;
        }

        static SBE_CONSTEXPR std::uint16_t quoteSetIDMinValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)0;
        }

        static SBE_CONSTEXPR std::uint16_t quoteSetIDMaxValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)65534;
        }

        static SBE_CONSTEXPR std::size_t quoteSetIDEncodingLength() SBE_NOEXCEPT
        {
            return 2;
        }

        SBE_NODISCARD std::uint16_t quoteSetID() const SBE_NOEXCEPT
        {
            std::uint16_t val;
            std::memcpy(&val, m_buffer + m_offset + 36, sizeof(std::uint16_t));
            return SBE_LITTLE_ENDIAN_ENCODE_16(val);
        }

        NoQuoteEntries &quoteSetID(const std::uint16_t value) SBE_NOEXCEPT
        {
            std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
            std::memcpy(m_buffer + m_offset + 36, &val, sizeof(std::uint16_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits>& operator<<(
            std::basic_ostream<CharT, Traits>& builder, NoQuoteEntries writer)
        {
            builder << '{';
            builder << R"("BidPx": )";
            builder << writer.bidPx();

            builder << ", ";
            builder << R"("OfferPx": )";
            builder << writer.offerPx();

            builder << ", ";
            builder << R"("QuoteEntryID": )";
            builder << +writer.quoteEntryID();

            builder << ", ";
            builder << R"("SecurityID": )";
            builder << +writer.securityID();

            builder << ", ";
            builder << R"("BidSize": )";
            builder << +writer.bidSize();

            builder << ", ";
            builder << R"("OfferSize": )";
            builder << +writer.offerSize();

            builder << ", ";
            builder << R"("UnderlyingSecurityID": )";
            builder << +writer.underlyingSecurityID();

            builder << ", ";
            builder << R"("QuoteSetID": )";
            builder << +writer.quoteSetID();

            builder << '}';

            return builder;
        }
    };

private:
    NoQuoteEntries m_noQuoteEntries;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoQuoteEntriesId() SBE_NOEXCEPT
    {
        return 295;
    }

    SBE_NODISCARD inline NoQuoteEntries &noQuoteEntries()
    {
        m_noQuoteEntries.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noQuoteEntries;
    }

    NoQuoteEntries &noQuoteEntriesCount(const std::uint8_t count)
    {
        m_noQuoteEntries.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noQuoteEntries;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noQuoteEntriesSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noQuoteEntriesInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noQuoteEntriesSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& builder, MassQuote517 _writer)
    {
        MassQuote517 writer(_writer.m_buffer, _writer.m_offset,
            _writer.m_bufferLength, _writer.sbeBlockLength(), _writer.m_actingVersion);
        builder << '{';
        builder << R"("Name": "MassQuote517", )";
        builder << R"("sbeTemplateId": )";
        builder << writer.sbeTemplateId();
        builder << ", ";

        builder << R"("PartyDetailsListReqID": )";
        builder << +writer.partyDetailsListReqID();

        builder << ", ";
        builder << R"("SendingTimeEpoch": )";
        builder << +writer.sendingTimeEpoch();

        builder << ", ";
        builder << R"("ManualOrderIndicator": )";
        builder << '"' << writer.manualOrderIndicator() << '"';

        builder << ", ";
        builder << R"("SeqNum": )";
        builder << +writer.seqNum();

        builder << ", ";
        builder << R"("SenderID": )";
        builder << '"' <<
            writer.getSenderIDAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("QuoteReqID": )";
        builder << +writer.quoteReqID();

        builder << ", ";
        builder << R"("Location": )";
        builder << '"' <<
            writer.getLocationAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("QuoteID": )";
        builder << +writer.quoteID();

        builder << ", ";
        builder << R"("TotNoQuoteEntries": )";
        builder << +writer.totNoQuoteEntries();

        builder << ", ";
        builder << R"("MMProtectionReset": )";
        builder << '"' << writer.mMProtectionReset() << '"';

        builder << ", ";
        builder << R"("LiquidityFlag": )";
        builder << '"' << writer.liquidityFlag() << '"';

        builder << ", ";
        builder << R"("ShortSaleType": )";
        builder << '"' << writer.shortSaleType() << '"';

        builder << ", ";
        builder << R"("Reserved": )";
        builder << '"' <<
            writer.getReservedAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("Reserved1": )";
        builder << '"' <<
            writer.getReserved1AsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("QuoteEntryOpen": )";
        builder << '"' << writer.quoteEntryOpen() << '"';

        builder << ", ";
        {
            bool atLeastOne = false;
            builder << R"("NoQuoteEntries": [)";
            writer.noQuoteEntries().forEach([&](NoQuoteEntries& noQuoteEntries)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noQuoteEntries;
            });
            builder << ']';
        }

        builder << '}';

        return builder;
    }
};
}
#endif
