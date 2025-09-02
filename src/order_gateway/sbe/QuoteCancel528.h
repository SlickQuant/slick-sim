/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_QUOTECANCEL528_H_
#define _SBE_QUOTECANCEL528_H_

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

class QuoteCancel528
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

    QuoteCancel528() = default;

    QuoteCancel528(
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

    QuoteCancel528(char *buffer, const std::uint64_t bufferLength) :
        QuoteCancel528(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    QuoteCancel528(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        QuoteCancel528(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return (std::uint16_t)61;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return (std::uint16_t)528;
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
        return "Z";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    QuoteCancel528 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = QuoteCancel528(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    QuoteCancel528 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        MessageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = QuoteCancel528(
            buffer,
            offset + MessageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    QuoteCancel528 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = QuoteCancel528(buffer, offset, bufferLength, actingBlockLength, actingVersion);
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

    QuoteCancel528 &partyDetailsListReqID(const std::uint64_t value) SBE_NOEXCEPT
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

    QuoteCancel528 &sendingTimeEpoch(const std::uint64_t value) SBE_NOEXCEPT
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

    QuoteCancel528 &manualOrderIndicator(const ManualOrdIndReq::Value value) SBE_NOEXCEPT
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

    QuoteCancel528 &seqNum(const std::uint32_t value) SBE_NOEXCEPT
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

    QuoteCancel528 &senderID(const std::uint64_t index, const char value)
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

    QuoteCancel528 &putSenderID(const char *const src) SBE_NOEXCEPT
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
    QuoteCancel528 &putSenderID(const std::string_view str)
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
    QuoteCancel528 &putSenderID(const std::string& str)
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
        return 41;
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
        return m_buffer + m_offset + 41;
    }

    SBE_NODISCARD char *location() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 41;
    }

    SBE_NODISCARD char location(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 41 + (index * 1), sizeof(char));
        return (val);
    }

    QuoteCancel528 &location(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 41 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getLocation(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getLocation [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 41, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    QuoteCancel528 &putLocation(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 41, src, sizeof(char) * 5);
        return *this;
    }

    std::string getLocationAsString() const
    {
        const char *buffer = m_buffer + m_offset + 41;
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
        const char *buffer = m_buffer + m_offset + 41;
        size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    QuoteCancel528 &putLocation(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 41, str.data(), srcLength);
        for (size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 41 + start] = 0;
        }

        return *this;
    }
    #else
    QuoteCancel528 &putLocation(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 41, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 41 + start] = 0;
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
        return 46;
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
        std::memcpy(&val, m_buffer + m_offset + 46, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    QuoteCancel528 &quoteID(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 46, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char * QuoteCancelTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t quoteCancelTypeId() SBE_NOEXCEPT
    {
        return 298;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t quoteCancelTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool quoteCancelTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= quoteCancelTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteCancelTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 50;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t quoteCancelTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD QuoteCxlTyp::Value quoteCancelType() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 50, sizeof(std::uint8_t));
        return QuoteCxlTyp::get((val));
    }

    QuoteCancel528 &quoteCancelType(const QuoteCxlTyp::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 50, &val, sizeof(std::uint8_t));
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
        return 51;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t liquidityFlagEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD BooleanNULL::Value liquidityFlag() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 51, sizeof(std::uint8_t));
        return BooleanNULL::get((val));
    }

    QuoteCancel528 &liquidityFlag(const BooleanNULL::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 51, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * OrigOrderUserMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t origOrderUserId() SBE_NOEXCEPT
    {
        return 9937;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t origOrderUserSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool origOrderUserInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= origOrderUserSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t origOrderUserEncodingOffset() SBE_NOEXCEPT
    {
        return 52;
    }

    static SBE_CONSTEXPR char origOrderUserNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char origOrderUserMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char origOrderUserMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t origOrderUserEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    static SBE_CONSTEXPR std::uint64_t origOrderUserLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD const char *origOrderUser() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 52;
    }

    SBE_NODISCARD char *origOrderUser() SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 52;
    }

    SBE_NODISCARD char origOrderUser(const std::uint64_t index) const
    {
        if (index >= 8)
        {
            throw std::runtime_error("index out of range for origOrderUser [E104]");
        }

        if (m_actingVersion < 8)
        {
            return (char)0;
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 52 + (index * 1), sizeof(char));
        return (val);
    }

    QuoteCancel528 &origOrderUser(const std::uint64_t index, const char value)
    {
        if (index >= 8)
        {
            throw std::runtime_error("index out of range for origOrderUser [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 52 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getOrigOrderUser(char *const dst, const std::uint64_t length) const
    {
        if (length > 8)
        {
            throw std::runtime_error("length too large for getOrigOrderUser [E106]");
        }

        if (m_actingVersion < 8)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 52, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    QuoteCancel528 &putOrigOrderUser(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 52, src, sizeof(char) * 8);
        return *this;
    }

    std::string getOrigOrderUserAsString() const
    {
        const char *buffer = m_buffer + m_offset + 52;
        size_t length = 0;

        for (; length < 8 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getOrigOrderUserAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getOrigOrderUserAsString();

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
    std::string_view getOrigOrderUserAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 52;
        size_t length = 0;

        for (; length < 8 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    QuoteCancel528 &putOrigOrderUser(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 8)
        {
            throw std::runtime_error("string too large for putOrigOrderUser [E106]");
        }

        std::memcpy(m_buffer + m_offset + 52, str.data(), srcLength);
        for (size_t start = srcLength; start < 8; ++start)
        {
            m_buffer[m_offset + 52 + start] = 0;
        }

        return *this;
    }
    #else
    QuoteCancel528 &putOrigOrderUser(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 8)
        {
            throw std::runtime_error("string too large for putOrigOrderUser [E106]");
        }

        std::memcpy(m_buffer + m_offset + 52, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 8; ++start)
        {
            m_buffer[m_offset + 52 + start] = 0;
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
        return 60;
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
        std::memcpy(&val, m_buffer + m_offset + 60, sizeof(std::uint8_t));
        return BooleanNULL::get((val));
    }

    QuoteCancel528 &quoteEntryOpen(const BooleanNULL::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 60, &val, sizeof(std::uint8_t));
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
            dimensions.blockLength((std::uint16_t)10);
            dimensions.numInGroup((std::uint8_t)count);
            m_index = 0;
            m_count = count;
            m_blockLength = 10;
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
            return 10;
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

        SBE_NODISCARD static const char * SecurityGroupMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "String";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t securityGroupId() SBE_NOEXCEPT
        {
            return 1151;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityGroupSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool securityGroupInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= securityGroupSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityGroupEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR char securityGroupNullValue() SBE_NOEXCEPT
        {
            return (char)0;
        }

        static SBE_CONSTEXPR char securityGroupMinValue() SBE_NOEXCEPT
        {
            return (char)32;
        }

        static SBE_CONSTEXPR char securityGroupMaxValue() SBE_NOEXCEPT
        {
            return (char)126;
        }

        static SBE_CONSTEXPR std::size_t securityGroupEncodingLength() SBE_NOEXCEPT
        {
            return 6;
        }

        static SBE_CONSTEXPR std::uint64_t securityGroupLength() SBE_NOEXCEPT
        {
            return 6;
        }

        SBE_NODISCARD const char *securityGroup() const SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 0;
        }

        SBE_NODISCARD char *securityGroup() SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 0;
        }

        SBE_NODISCARD char securityGroup(const std::uint64_t index) const
        {
            if (index >= 6)
            {
                throw std::runtime_error("index out of range for securityGroup [E104]");
            }

            char val;
            std::memcpy(&val, m_buffer + m_offset + 0 + (index * 1), sizeof(char));
            return (val);
        }

        NoQuoteEntries &securityGroup(const std::uint64_t index, const char value)
        {
            if (index >= 6)
            {
                throw std::runtime_error("index out of range for securityGroup [E105]");
            }

            char val = (value);
            std::memcpy(m_buffer + m_offset + 0 + (index * 1), &val, sizeof(char));
            return *this;
        }

        std::uint64_t getSecurityGroup(char *const dst, const std::uint64_t length) const
        {
            if (length > 6)
            {
                throw std::runtime_error("length too large for getSecurityGroup [E106]");
            }

            std::memcpy(dst, m_buffer + m_offset + 0, sizeof(char) * static_cast<size_t>(length));
            return length;
        }

        NoQuoteEntries &putSecurityGroup(const char *const src) SBE_NOEXCEPT
        {
            std::memcpy(m_buffer + m_offset + 0, src, sizeof(char) * 6);
            return *this;
        }

        std::string getSecurityGroupAsString() const
        {
            const char *buffer = m_buffer + m_offset + 0;
            size_t length = 0;

            for (; length < 6 && *(buffer + length) != '\0'; ++length);
            std::string result(buffer, length);

            return result;
        }

        std::string getSecurityGroupAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getSecurityGroupAsString();

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
        std::string_view getSecurityGroupAsStringView() const SBE_NOEXCEPT
        {
            const char *buffer = m_buffer + m_offset + 0;
            size_t length = 0;

            for (; length < 6 && *(buffer + length) != '\0'; ++length);
            std::string_view result(buffer, length);

            return result;
        }
        #endif

        #if __cplusplus >= 201703L
        NoQuoteEntries &putSecurityGroup(const std::string_view str)
        {
            const size_t srcLength = str.length();
            if (srcLength > 6)
            {
                throw std::runtime_error("string too large for putSecurityGroup [E106]");
            }

            std::memcpy(m_buffer + m_offset + 0, str.data(), srcLength);
            for (size_t start = srcLength; start < 6; ++start)
            {
                m_buffer[m_offset + 0 + start] = 0;
            }

            return *this;
        }
        #else
        NoQuoteEntries &putSecurityGroup(const std::string& str)
        {
            const size_t srcLength = str.length();
            if (srcLength > 6)
            {
                throw std::runtime_error("string too large for putSecurityGroup [E106]");
            }

            std::memcpy(m_buffer + m_offset + 0, str.c_str(), srcLength);
            for (size_t start = srcLength; start < 6; ++start)
            {
                m_buffer[m_offset + 0 + start] = 0;
            }

            return *this;
        }
        #endif

        SBE_NODISCARD static const char * SecurityIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
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
            return 6;
        }

        static SBE_CONSTEXPR std::int32_t securityIDNullValue() SBE_NOEXCEPT
        {
            return 2147483647;
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
            std::memcpy(&val, m_buffer + m_offset + 6, sizeof(std::int32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteEntries &securityID(const std::int32_t value) SBE_NOEXCEPT
        {
            std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 6, &val, sizeof(std::int32_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits>& operator<<(
            std::basic_ostream<CharT, Traits>& builder, NoQuoteEntries writer)
        {
            builder << '{';
            builder << R"("SecurityGroup": )";
            builder << '"' <<
                writer.getSecurityGroupAsJsonEscapedString().c_str() << '"';

            builder << ", ";
            builder << R"("SecurityID": )";
            builder << +writer.securityID();

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

    class NoQuoteSets
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
            dimensions.blockLength((std::uint16_t)10);
            dimensions.numInGroup((std::uint8_t)count);
            m_index = 0;
            m_count = count;
            m_blockLength = 10;
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
            return 10;
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

        inline NoQuoteSets &next()
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
            return 0;
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
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteSets &bidSize(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint32_t));
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
            return 4;
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
            std::memcpy(&val, m_buffer + m_offset + 4, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoQuoteSets &offerSize(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 4, &val, sizeof(std::uint32_t));
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
            return 8;
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
            std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint16_t));
            return SBE_LITTLE_ENDIAN_ENCODE_16(val);
        }

        NoQuoteSets &quoteSetID(const std::uint16_t value) SBE_NOEXCEPT
        {
            std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
            std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint16_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits>& operator<<(
            std::basic_ostream<CharT, Traits>& builder, NoQuoteSets writer)
        {
            builder << '{';
            builder << R"("BidSize": )";
            builder << +writer.bidSize();

            builder << ", ";
            builder << R"("OfferSize": )";
            builder << +writer.offerSize();

            builder << ", ";
            builder << R"("QuoteSetID": )";
            builder << +writer.quoteSetID();

            builder << '}';

            return builder;
        }
    };

private:
    NoQuoteSets m_noQuoteSets;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoQuoteSetsId() SBE_NOEXCEPT
    {
        return 296;
    }

    SBE_NODISCARD inline NoQuoteSets &noQuoteSets()
    {
        m_noQuoteSets.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noQuoteSets;
    }

    NoQuoteSets &noQuoteSetsCount(const std::uint8_t count)
    {
        m_noQuoteSets.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noQuoteSets;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noQuoteSetsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noQuoteSetsInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noQuoteSetsSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& builder, QuoteCancel528 _writer)
    {
        QuoteCancel528 writer(_writer.m_buffer, _writer.m_offset,
            _writer.m_bufferLength, _writer.sbeBlockLength(), _writer.m_actingVersion);
        builder << '{';
        builder << R"("Name": "QuoteCancel528", )";
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
        builder << R"("Location": )";
        builder << '"' <<
            writer.getLocationAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("QuoteID": )";
        builder << +writer.quoteID();

        builder << ", ";
        builder << R"("QuoteCancelType": )";
        builder << '"' << writer.quoteCancelType() << '"';

        builder << ", ";
        builder << R"("LiquidityFlag": )";
        builder << '"' << writer.liquidityFlag() << '"';

        builder << ", ";
        builder << R"("OrigOrderUser": )";
        builder << '"' <<
            writer.getOrigOrderUserAsJsonEscapedString().c_str() << '"';

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

        builder << ", ";
        {
            bool atLeastOne = false;
            builder << R"("NoQuoteSets": [)";
            writer.noQuoteSets().forEach([&](NoQuoteSets& noQuoteSets)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noQuoteSets;
            });
            builder << ']';
        }

        builder << '}';

        return builder;
    }
};
}
#endif
