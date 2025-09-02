/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_ESTABLISHMENTACK504_H_
#define _SBE_ESTABLISHMENTACK504_H_

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

class EstablishmentAck504
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

    EstablishmentAck504() = default;

    EstablishmentAck504(
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

    EstablishmentAck504(char *buffer, const std::uint64_t bufferLength) :
        EstablishmentAck504(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    EstablishmentAck504(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        EstablishmentAck504(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return (std::uint16_t)38;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return (std::uint16_t)504;
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
        return "EstablishmentAck";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    EstablishmentAck504 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = EstablishmentAck504(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    EstablishmentAck504 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        MessageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = EstablishmentAck504(
            buffer,
            offset + MessageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    EstablishmentAck504 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = EstablishmentAck504(buffer, offset, bufferLength, actingBlockLength, actingVersion);
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

    EstablishmentAck504 &uUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
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
        return 8;
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
        std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    EstablishmentAck504 &requestTimestamp(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * NextSeqNoMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t nextSeqNoId() SBE_NOEXCEPT
    {
        return 39013;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t nextSeqNoSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool nextSeqNoInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= nextSeqNoSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t nextSeqNoEncodingOffset() SBE_NOEXCEPT
    {
        return 16;
    }

    static SBE_CONSTEXPR std::uint32_t nextSeqNoNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t nextSeqNoMinValue() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint32_t nextSeqNoMaxValue() SBE_NOEXCEPT
    {
        return 4294967294;
    }

    static SBE_CONSTEXPR std::size_t nextSeqNoEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t nextSeqNo() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 16, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    EstablishmentAck504 &nextSeqNo(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 16, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char * PreviousSeqNoMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t previousSeqNoId() SBE_NOEXCEPT
    {
        return 39021;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t previousSeqNoSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool previousSeqNoInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= previousSeqNoSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t previousSeqNoEncodingOffset() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint32_t previousSeqNoNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t previousSeqNoMinValue() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint32_t previousSeqNoMaxValue() SBE_NOEXCEPT
    {
        return 4294967294;
    }

    static SBE_CONSTEXPR std::size_t previousSeqNoEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t previousSeqNo() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 20, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    EstablishmentAck504 &previousSeqNo(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 20, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char * PreviousUUIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t previousUUIDId() SBE_NOEXCEPT
    {
        return 39015;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t previousUUIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool previousUUIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= previousUUIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t previousUUIDEncodingOffset() SBE_NOEXCEPT
    {
        return 24;
    }

    static SBE_CONSTEXPR std::uint64_t previousUUIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t previousUUIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t previousUUIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t previousUUIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t previousUUID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 24, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    EstablishmentAck504 &previousUUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 24, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * KeepAliveIntervalMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t keepAliveIntervalId() SBE_NOEXCEPT
    {
        return 39014;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t keepAliveIntervalSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool keepAliveIntervalInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= keepAliveIntervalSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t keepAliveIntervalEncodingOffset() SBE_NOEXCEPT
    {
        return 32;
    }

    static SBE_CONSTEXPR std::uint16_t keepAliveIntervalNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT16;
    }

    static SBE_CONSTEXPR std::uint16_t keepAliveIntervalMinValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)0;
    }

    static SBE_CONSTEXPR std::uint16_t keepAliveIntervalMaxValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65534;
    }

    static SBE_CONSTEXPR std::size_t keepAliveIntervalEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t keepAliveInterval() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 32, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    EstablishmentAck504 &keepAliveInterval(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 32, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char * SecretKeySecureIDExpirationMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t secretKeySecureIDExpirationId() SBE_NOEXCEPT
    {
        return 39022;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t secretKeySecureIDExpirationSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool secretKeySecureIDExpirationInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= secretKeySecureIDExpirationSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t secretKeySecureIDExpirationEncodingOffset() SBE_NOEXCEPT
    {
        return 34;
    }

    static SBE_CONSTEXPR std::uint16_t secretKeySecureIDExpirationNullValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65535;
    }

    static SBE_CONSTEXPR std::uint16_t secretKeySecureIDExpirationMinValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)0;
    }

    static SBE_CONSTEXPR std::uint16_t secretKeySecureIDExpirationMaxValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65534;
    }

    static SBE_CONSTEXPR std::size_t secretKeySecureIDExpirationEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t secretKeySecureIDExpiration() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 34, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    EstablishmentAck504 &secretKeySecureIDExpiration(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 34, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char * FaultToleranceIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t faultToleranceIndicatorId() SBE_NOEXCEPT
    {
        return 39010;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t faultToleranceIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool faultToleranceIndicatorInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= faultToleranceIndicatorSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t faultToleranceIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 36;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t faultToleranceIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD FTI::Value faultToleranceIndicator() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 36, sizeof(std::uint8_t));
        return FTI::get((val));
    }

    EstablishmentAck504 &faultToleranceIndicator(const FTI::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 36, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * SplitMsgMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t splitMsgId() SBE_NOEXCEPT
    {
        return 9553;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t splitMsgSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool splitMsgInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= splitMsgSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t splitMsgEncodingOffset() SBE_NOEXCEPT
    {
        return 37;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t splitMsgEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD SplitMsg::Value splitMsg() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 37, sizeof(std::uint8_t));
        return SplitMsg::get((val));
    }

    EstablishmentAck504 &splitMsg(const SplitMsg::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 37, &val, sizeof(std::uint8_t));
        return *this;
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& builder, EstablishmentAck504 _writer)
    {
        EstablishmentAck504 writer(_writer.m_buffer, _writer.m_offset,
            _writer.m_bufferLength, _writer.sbeBlockLength(), _writer.m_actingVersion);
        builder << '{';
        builder << R"("Name": "EstablishmentAck504", )";
        builder << R"("sbeTemplateId": )";
        builder << writer.sbeTemplateId();
        builder << ", ";

        builder << R"("UUID": )";
        builder << +writer.uUID();

        builder << ", ";
        builder << R"("RequestTimestamp": )";
        builder << +writer.requestTimestamp();

        builder << ", ";
        builder << R"("NextSeqNo": )";
        builder << +writer.nextSeqNo();

        builder << ", ";
        builder << R"("PreviousSeqNo": )";
        builder << +writer.previousSeqNo();

        builder << ", ";
        builder << R"("PreviousUUID": )";
        builder << +writer.previousUUID();

        builder << ", ";
        builder << R"("KeepAliveInterval": )";
        builder << +writer.keepAliveInterval();

        builder << ", ";
        builder << R"("SecretKeySecureIDExpiration": )";
        builder << +writer.secretKeySecureIDExpiration();

        builder << ", ";
        builder << R"("FaultToleranceIndicator": )";
        builder << '"' << writer.faultToleranceIndicator() << '"';

        builder << ", ";
        builder << R"("SplitMsg": )";
        builder << '"' << writer.splitMsg() << '"';

        builder << '}';

        return builder;
    }
};
}
#endif
