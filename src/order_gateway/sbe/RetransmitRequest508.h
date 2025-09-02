/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_RETRANSMITREQUEST508_H_
#define _SBE_RETRANSMITREQUEST508_H_

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

class RetransmitRequest508
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

    RetransmitRequest508() = default;

    RetransmitRequest508(
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

    RetransmitRequest508(char *buffer, const std::uint64_t bufferLength) :
        RetransmitRequest508(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    RetransmitRequest508(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        RetransmitRequest508(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return (std::uint16_t)30;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return (std::uint16_t)508;
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
        return "RetransmitRequest";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    RetransmitRequest508 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = RetransmitRequest508(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    RetransmitRequest508 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        MessageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = RetransmitRequest508(
            buffer,
            offset + MessageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    RetransmitRequest508 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = RetransmitRequest508(buffer, offset, bufferLength, actingBlockLength, actingVersion);
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

    SBE_NODISCARD static const char * UUIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t uUIDId() SBE_NOEXCEPT
    {
        return 39001;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t uUIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool uUIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= uUIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t uUIDEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t uUIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t uUIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t uUIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t uUIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t uUID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    RetransmitRequest508 &uUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * LastUUIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t lastUUIDId() SBE_NOEXCEPT
    {
        return 39017;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t lastUUIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool lastUUIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= lastUUIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t lastUUIDEncodingOffset() SBE_NOEXCEPT
    {
        return 8;
    }

    static SBE_CONSTEXPR std::uint64_t lastUUIDNullValue() SBE_NOEXCEPT
    {
        return 0xffffffffffffffffL;
    }

    static SBE_CONSTEXPR std::uint64_t lastUUIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t lastUUIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t lastUUIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t lastUUID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    RetransmitRequest508 &lastUUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * RequestTimestampMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t requestTimestampId() SBE_NOEXCEPT
    {
        return 39002;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t requestTimestampSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool requestTimestampInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= requestTimestampSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t requestTimestampEncodingOffset() SBE_NOEXCEPT
    {
        return 16;
    }

    static SBE_CONSTEXPR std::uint64_t requestTimestampNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t requestTimestampMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t requestTimestampMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t requestTimestampEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t requestTimestamp() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 16, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    RetransmitRequest508 &requestTimestamp(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 16, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * FromSeqNoMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t fromSeqNoId() SBE_NOEXCEPT
    {
        return 39018;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t fromSeqNoSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool fromSeqNoInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= fromSeqNoSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t fromSeqNoEncodingOffset() SBE_NOEXCEPT
    {
        return 24;
    }

    static SBE_CONSTEXPR std::uint32_t fromSeqNoNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t fromSeqNoMinValue() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint32_t fromSeqNoMaxValue() SBE_NOEXCEPT
    {
        return 4294967294;
    }

    static SBE_CONSTEXPR std::size_t fromSeqNoEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t fromSeqNo() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 24, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    RetransmitRequest508 &fromSeqNo(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 24, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char * MsgCountMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t msgCountId() SBE_NOEXCEPT
    {
        return 39019;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t msgCountSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool msgCountInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= msgCountSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t msgCountEncodingOffset() SBE_NOEXCEPT
    {
        return 28;
    }

    static SBE_CONSTEXPR std::uint16_t msgCountNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT16;
    }

    static SBE_CONSTEXPR std::uint16_t msgCountMinValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)0;
    }

    static SBE_CONSTEXPR std::uint16_t msgCountMaxValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65534;
    }

    static SBE_CONSTEXPR std::size_t msgCountEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t msgCount() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 28, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    RetransmitRequest508 &msgCount(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 28, &val, sizeof(std::uint16_t));
        return *this;
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& builder, RetransmitRequest508 _writer)
    {
        RetransmitRequest508 writer(_writer.m_buffer, _writer.m_offset,
            _writer.m_bufferLength, _writer.sbeBlockLength(), _writer.m_actingVersion);
        builder << '{';
        builder << R"("Name": "RetransmitRequest508", )";
        builder << R"("sbeTemplateId": )";
        builder << writer.sbeTemplateId();
        builder << ", ";

        builder << R"("UUID": )";
        builder << +writer.uUID();

        builder << ", ";
        builder << R"("LastUUID": )";
        builder << +writer.lastUUID();

        builder << ", ";
        builder << R"("RequestTimestamp": )";
        builder << +writer.requestTimestamp();

        builder << ", ";
        builder << R"("FromSeqNo": )";
        builder << +writer.fromSeqNo();

        builder << ", ";
        builder << R"("MsgCount": )";
        builder << +writer.msgCount();

        builder << '}';

        return builder;
    }
};
}
#endif
