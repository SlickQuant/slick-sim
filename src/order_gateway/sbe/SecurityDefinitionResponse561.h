/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_SECURITYDEFINITIONRESPONSE561_H_
#define _SBE_SECURITYDEFINITIONRESPONSE561_H_

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

class SecurityDefinitionResponse561
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

    SecurityDefinitionResponse561() = default;

    SecurityDefinitionResponse561(
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

    SecurityDefinitionResponse561(char *buffer, const std::uint64_t bufferLength) :
        SecurityDefinitionResponse561(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    SecurityDefinitionResponse561(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        SecurityDefinitionResponse561(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return (std::uint16_t)430;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return (std::uint16_t)561;
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
        return "d";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    SecurityDefinitionResponse561 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = SecurityDefinitionResponse561(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    SecurityDefinitionResponse561 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        MessageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = SecurityDefinitionResponse561(
            buffer,
            offset + MessageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    SecurityDefinitionResponse561 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = SecurityDefinitionResponse561(buffer, offset, bufferLength, actingBlockLength, actingVersion);
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
        return 0;
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
        std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    SecurityDefinitionResponse561 &seqNum(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint32_t));
        return *this;
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
        return 4;
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
        std::memcpy(&val, m_buffer + m_offset + 4, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    SecurityDefinitionResponse561 &uUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 4, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * TextMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t textId() SBE_NOEXCEPT
    {
        return 58;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t textSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool textInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= textSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t textEncodingOffset() SBE_NOEXCEPT
    {
        return 12;
    }

    static SBE_CONSTEXPR char textNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char textMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char textMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t textEncodingLength() SBE_NOEXCEPT
    {
        return 256;
    }

    static SBE_CONSTEXPR std::uint64_t textLength() SBE_NOEXCEPT
    {
        return 256;
    }

    SBE_NODISCARD const char *text() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 12;
    }

    SBE_NODISCARD char *text() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 12;
    }

    SBE_NODISCARD char text(const std::uint64_t index) const
    {
        if (index >= 256)
        {
            throw std::runtime_error("index out of range for text [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 12 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &text(const std::uint64_t index, const char value)
    {
        if (index >= 256)
        {
            throw std::runtime_error("index out of range for text [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 12 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getText(char *const dst, const std::uint64_t length) const
    {
        if (length > 256)
        {
            throw std::runtime_error("length too large for getText [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 12, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putText(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 12, src, sizeof(char) * 256);
        return *this;
    }

    std::string getTextAsString() const
    {
        const char *buffer = m_buffer + m_offset + 12;
        size_t length = 0;

        for (; length < 256 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getTextAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getTextAsString();

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
    std::string_view getTextAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 12;
        size_t length = 0;

        for (; length < 256 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putText(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 256)
        {
            throw std::runtime_error("string too large for putText [E106]");
        }

        std::memcpy(m_buffer + m_offset + 12, str.data(), srcLength);
        for (size_t start = srcLength; start < 256; ++start)
        {
            m_buffer[m_offset + 12 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putText(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 256)
        {
            throw std::runtime_error("string too large for putText [E106]");
        }

        std::memcpy(m_buffer + m_offset + 12, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 256; ++start)
        {
            m_buffer[m_offset + 12 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * FinancialInstrumentFullNameMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t financialInstrumentFullNameId() SBE_NOEXCEPT
    {
        return 2714;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t financialInstrumentFullNameSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool financialInstrumentFullNameInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= financialInstrumentFullNameSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t financialInstrumentFullNameEncodingOffset() SBE_NOEXCEPT
    {
        return 268;
    }

    static SBE_CONSTEXPR char financialInstrumentFullNameNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char financialInstrumentFullNameMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char financialInstrumentFullNameMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t financialInstrumentFullNameEncodingLength() SBE_NOEXCEPT
    {
        return 35;
    }

    static SBE_CONSTEXPR std::uint64_t financialInstrumentFullNameLength() SBE_NOEXCEPT
    {
        return 35;
    }

    SBE_NODISCARD const char *financialInstrumentFullName() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 268;
    }

    SBE_NODISCARD char *financialInstrumentFullName() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 268;
    }

    SBE_NODISCARD char financialInstrumentFullName(const std::uint64_t index) const
    {
        if (index >= 35)
        {
            throw std::runtime_error("index out of range for financialInstrumentFullName [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 268 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &financialInstrumentFullName(const std::uint64_t index, const char value)
    {
        if (index >= 35)
        {
            throw std::runtime_error("index out of range for financialInstrumentFullName [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 268 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getFinancialInstrumentFullName(char *const dst, const std::uint64_t length) const
    {
        if (length > 35)
        {
            throw std::runtime_error("length too large for getFinancialInstrumentFullName [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 268, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putFinancialInstrumentFullName(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 268, src, sizeof(char) * 35);
        return *this;
    }

    std::string getFinancialInstrumentFullNameAsString() const
    {
        const char *buffer = m_buffer + m_offset + 268;
        size_t length = 0;

        for (; length < 35 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getFinancialInstrumentFullNameAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getFinancialInstrumentFullNameAsString();

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
    std::string_view getFinancialInstrumentFullNameAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 268;
        size_t length = 0;

        for (; length < 35 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putFinancialInstrumentFullName(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 35)
        {
            throw std::runtime_error("string too large for putFinancialInstrumentFullName [E106]");
        }

        std::memcpy(m_buffer + m_offset + 268, str.data(), srcLength);
        for (size_t start = srcLength; start < 35; ++start)
        {
            m_buffer[m_offset + 268 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putFinancialInstrumentFullName(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 35)
        {
            throw std::runtime_error("string too large for putFinancialInstrumentFullName [E106]");
        }

        std::memcpy(m_buffer + m_offset + 268, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 35; ++start)
        {
            m_buffer[m_offset + 268 + start] = 0;
        }

        return *this;
    }
    #endif

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
        return 303;
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
        return m_buffer + m_offset + 303;
    }

    SBE_NODISCARD char *senderID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 303;
    }

    SBE_NODISCARD char senderID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 303 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &senderID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 303 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSenderID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getSenderID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 303, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putSenderID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 303, src, sizeof(char) * 20);
        return *this;
    }

    std::string getSenderIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 303;
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
        const char *buffer = m_buffer + m_offset + 303;
        size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putSenderID(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 303, str.data(), srcLength);
        for (size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 303 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putSenderID(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 303, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 303 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * SymbolMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t symbolId() SBE_NOEXCEPT
    {
        return 55;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t symbolSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool symbolInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= symbolSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t symbolEncodingOffset() SBE_NOEXCEPT
    {
        return 323;
    }

    static SBE_CONSTEXPR char symbolNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char symbolMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char symbolMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t symbolEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t symbolLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *symbol() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 323;
    }

    SBE_NODISCARD char *symbol() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 323;
    }

    SBE_NODISCARD char symbol(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for symbol [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 323 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &symbol(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for symbol [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 323 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSymbol(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getSymbol [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 323, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putSymbol(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 323, src, sizeof(char) * 20);
        return *this;
    }

    std::string getSymbolAsString() const
    {
        const char *buffer = m_buffer + m_offset + 323;
        size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSymbolAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSymbolAsString();

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
    std::string_view getSymbolAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 323;
        size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putSymbol(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSymbol [E106]");
        }

        std::memcpy(m_buffer + m_offset + 323, str.data(), srcLength);
        for (size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 323 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putSymbol(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSymbol [E106]");
        }

        std::memcpy(m_buffer + m_offset + 323, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 323 + start] = 0;
        }

        return *this;
    }
    #endif

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
        return 343;
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
        std::memcpy(&val, m_buffer + m_offset + 343, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    SecurityDefinitionResponse561 &partyDetailsListReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 343, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * SecurityReqIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityReqIDId() SBE_NOEXCEPT
    {
        return 320;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityReqIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityReqIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityReqIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityReqIDEncodingOffset() SBE_NOEXCEPT
    {
        return 351;
    }

    static SBE_CONSTEXPR std::uint64_t securityReqIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t securityReqIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t securityReqIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t securityReqIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t securityReqID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 351, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    SecurityDefinitionResponse561 &securityReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 351, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char * SecurityResponseIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityResponseIDId() SBE_NOEXCEPT
    {
        return 322;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityResponseIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityResponseIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityResponseIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityResponseIDEncodingOffset() SBE_NOEXCEPT
    {
        return 359;
    }

    static SBE_CONSTEXPR std::uint64_t securityResponseIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t securityResponseIDMinValue() SBE_NOEXCEPT
    {
        return 0x0L;
    }

    static SBE_CONSTEXPR std::uint64_t securityResponseIDMaxValue() SBE_NOEXCEPT
    {
        return 0xfffffffffffffffeL;
    }

    static SBE_CONSTEXPR std::size_t securityResponseIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t securityResponseID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 359, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    SecurityDefinitionResponse561 &securityResponseID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 359, &val, sizeof(std::uint64_t));
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
        return 367;
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
        std::memcpy(&val, m_buffer + m_offset + 367, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    SecurityDefinitionResponse561 &sendingTimeEpoch(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 367, &val, sizeof(std::uint64_t));
        return *this;
    }

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
        return 375;
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
        return m_buffer + m_offset + 375;
    }

    SBE_NODISCARD char *securityGroup() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 375;
    }

    SBE_NODISCARD char securityGroup(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityGroup [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 375 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &securityGroup(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityGroup [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 375 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSecurityGroup(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getSecurityGroup [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 375, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putSecurityGroup(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 375, src, sizeof(char) * 6);
        return *this;
    }

    std::string getSecurityGroupAsString() const
    {
        const char *buffer = m_buffer + m_offset + 375;
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
        const char *buffer = m_buffer + m_offset + 375;
        size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putSecurityGroup(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityGroup [E106]");
        }

        std::memcpy(m_buffer + m_offset + 375, str.data(), srcLength);
        for (size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 375 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putSecurityGroup(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityGroup [E106]");
        }

        std::memcpy(m_buffer + m_offset + 375, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 375 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * SecurityTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityTypeId() SBE_NOEXCEPT
    {
        return 167;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 381;
    }

    static SBE_CONSTEXPR char securityTypeNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char securityTypeMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char securityTypeMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t securityTypeEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t securityTypeLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *securityType() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 381;
    }

    SBE_NODISCARD char *securityType() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 381;
    }

    SBE_NODISCARD char securityType(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityType [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 381 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &securityType(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityType [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 381 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSecurityType(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getSecurityType [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 381, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putSecurityType(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 381, src, sizeof(char) * 6);
        return *this;
    }

    std::string getSecurityTypeAsString() const
    {
        const char *buffer = m_buffer + m_offset + 381;
        size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSecurityTypeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSecurityTypeAsString();

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
    std::string_view getSecurityTypeAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 381;
        size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putSecurityType(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 381, str.data(), srcLength);
        for (size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 381 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putSecurityType(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 381, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 381 + start] = 0;
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
        return 387;
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
        return m_buffer + m_offset + 387;
    }

    SBE_NODISCARD char *location() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 387;
    }

    SBE_NODISCARD char location(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 387 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &location(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 387 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getLocation(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getLocation [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 387, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putLocation(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 387, src, sizeof(char) * 5);
        return *this;
    }

    std::string getLocationAsString() const
    {
        const char *buffer = m_buffer + m_offset + 387;
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
        const char *buffer = m_buffer + m_offset + 387;
        size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putLocation(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 387, str.data(), srcLength);
        for (size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 387 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putLocation(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 387, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 387 + start] = 0;
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
        return 392;
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
        std::memcpy(&val, m_buffer + m_offset + 392, sizeof(std::int32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    SecurityDefinitionResponse561 &securityID(const std::int32_t value) SBE_NOEXCEPT
    {
        std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 392, &val, sizeof(std::int32_t));
        return *this;
    }

    SBE_NODISCARD static const char * CurrencyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t currencyId() SBE_NOEXCEPT
    {
        return 15;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t currencySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool currencyInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= currencySinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t currencyEncodingOffset() SBE_NOEXCEPT
    {
        return 396;
    }

    static SBE_CONSTEXPR char currencyNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char currencyMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char currencyMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t currencyEncodingLength() SBE_NOEXCEPT
    {
        return 3;
    }

    static SBE_CONSTEXPR std::uint64_t currencyLength() SBE_NOEXCEPT
    {
        return 3;
    }

    SBE_NODISCARD const char *currency() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 396;
    }

    SBE_NODISCARD char *currency() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 396;
    }

    SBE_NODISCARD char currency(const std::uint64_t index) const
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for currency [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 396 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &currency(const std::uint64_t index, const char value)
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for currency [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 396 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getCurrency(char *const dst, const std::uint64_t length) const
    {
        if (length > 3)
        {
            throw std::runtime_error("length too large for getCurrency [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 396, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putCurrency(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 396, src, sizeof(char) * 3);
        return *this;
    }

    SecurityDefinitionResponse561 &putCurrency(
        const char value0,
        const char value1,
        const char value2) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 396, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 397, &val1, sizeof(char));
        char val2 = (value2);
        std::memcpy(m_buffer + m_offset + 398, &val2, sizeof(char));

        return *this;
    }

    std::string getCurrencyAsString() const
    {
        const char *buffer = m_buffer + m_offset + 396;
        size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getCurrencyAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getCurrencyAsString();

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
    std::string_view getCurrencyAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 396;
        size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putCurrency(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putCurrency [E106]");
        }

        std::memcpy(m_buffer + m_offset + 396, str.data(), srcLength);
        for (size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 396 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putCurrency(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putCurrency [E106]");
        }

        std::memcpy(m_buffer + m_offset + 396, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 396 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * SecurityIDSourceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityIDSourceId() SBE_NOEXCEPT
    {
        return 22;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityIDSourceSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityIDSourceInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityIDSourceSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityIDSourceEncodingOffset() SBE_NOEXCEPT
    {
        return 399;
    }

    static SBE_CONSTEXPR char securityIDSourceNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char securityIDSourceMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char securityIDSourceMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t securityIDSourceEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t securityIDSourceLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD const char *securityIDSource() const
    {
        static const std::uint8_t securityIDSourceValues[] = { 56 };

        return (const char *)securityIDSourceValues;
    }

    SBE_NODISCARD char securityIDSource(const std::uint64_t index) const
    {
        static const std::uint8_t securityIDSourceValues[] = { 56 };

        return (char)securityIDSourceValues[index];
    }

    std::uint64_t getSecurityIDSource(char *dst, const std::uint64_t length) const
    {
        static std::uint8_t securityIDSourceValues[] = { 56 };
        std::uint64_t bytesToCopy = length < sizeof(securityIDSourceValues) ? length : sizeof(securityIDSourceValues);

        std::memcpy(dst, securityIDSourceValues, static_cast<size_t>(bytesToCopy));
        return bytesToCopy;
    }

    std::string getSecurityIDSourceAsString() const
    {
        static const std::uint8_t SecurityIDSourceValues[] = { 56 };

        return std::string((const char *)SecurityIDSourceValues, 1);
    }

    std::string getSecurityIDSourceAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSecurityIDSourceAsString();

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

    SBE_NODISCARD static const char * MaturityMonthYearMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "MonthYear";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t maturityMonthYearId() SBE_NOEXCEPT
    {
        return 200;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t maturityMonthYearSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool maturityMonthYearInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= maturityMonthYearSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t maturityMonthYearEncodingOffset() SBE_NOEXCEPT
    {
        return 399;
    }

private:
    MaturityMonthYear m_maturityMonthYear;

public:
    SBE_NODISCARD MaturityMonthYear &maturityMonthYear()
    {
        m_maturityMonthYear.wrap(m_buffer, m_offset + 399, m_actingVersion, m_bufferLength);
        return m_maturityMonthYear;
    }

    SBE_NODISCARD static const char * DelayDurationMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t delayDurationId() SBE_NOEXCEPT
    {
        return 5904;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t delayDurationSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool delayDurationInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= delayDurationSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t delayDurationEncodingOffset() SBE_NOEXCEPT
    {
        return 404;
    }

    static SBE_CONSTEXPR std::uint16_t delayDurationNullValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65535;
    }

    static SBE_CONSTEXPR std::uint16_t delayDurationMinValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)0;
    }

    static SBE_CONSTEXPR std::uint16_t delayDurationMaxValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65534;
    }

    static SBE_CONSTEXPR std::size_t delayDurationEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t delayDuration() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 404, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    SecurityDefinitionResponse561 &delayDuration(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 404, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char * StartDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t startDateId() SBE_NOEXCEPT
    {
        return 916;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t startDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool startDateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= startDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t startDateEncodingOffset() SBE_NOEXCEPT
    {
        return 406;
    }

    static SBE_CONSTEXPR std::uint16_t startDateNullValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65535;
    }

    static SBE_CONSTEXPR std::uint16_t startDateMinValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)0;
    }

    static SBE_CONSTEXPR std::uint16_t startDateMaxValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65534;
    }

    static SBE_CONSTEXPR std::size_t startDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t startDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 406, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    SecurityDefinitionResponse561 &startDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 406, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char * EndDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t endDateId() SBE_NOEXCEPT
    {
        return 917;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t endDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool endDateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= endDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t endDateEncodingOffset() SBE_NOEXCEPT
    {
        return 408;
    }

    static SBE_CONSTEXPR std::uint16_t endDateNullValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65535;
    }

    static SBE_CONSTEXPR std::uint16_t endDateMinValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)0;
    }

    static SBE_CONSTEXPR std::uint16_t endDateMaxValue() SBE_NOEXCEPT
    {
        return (std::uint16_t)65534;
    }

    static SBE_CONSTEXPR std::size_t endDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t endDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 408, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    SecurityDefinitionResponse561 &endDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 408, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char * MaxNoOfSubstitutionsMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t maxNoOfSubstitutionsId() SBE_NOEXCEPT
    {
        return 37715;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t maxNoOfSubstitutionsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool maxNoOfSubstitutionsInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= maxNoOfSubstitutionsSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t maxNoOfSubstitutionsEncodingOffset() SBE_NOEXCEPT
    {
        return 410;
    }

    static SBE_CONSTEXPR std::uint8_t maxNoOfSubstitutionsNullValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)255;
    }

    static SBE_CONSTEXPR std::uint8_t maxNoOfSubstitutionsMinValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)0;
    }

    static SBE_CONSTEXPR std::uint8_t maxNoOfSubstitutionsMaxValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)254;
    }

    static SBE_CONSTEXPR std::size_t maxNoOfSubstitutionsEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t maxNoOfSubstitutions() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 410, sizeof(std::uint8_t));
        return (val);
    }

    SecurityDefinitionResponse561 &maxNoOfSubstitutions(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 410, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * SourceRepoIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t sourceRepoIDId() SBE_NOEXCEPT
    {
        return 5677;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sourceRepoIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool sourceRepoIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= sourceRepoIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sourceRepoIDEncodingOffset() SBE_NOEXCEPT
    {
        return 411;
    }

    static SBE_CONSTEXPR std::int32_t sourceRepoIDNullValue() SBE_NOEXCEPT
    {
        return 2147483647;
    }

    static SBE_CONSTEXPR std::int32_t sourceRepoIDMinValue() SBE_NOEXCEPT
    {
        return -2147483647;
    }

    static SBE_CONSTEXPR std::int32_t sourceRepoIDMaxValue() SBE_NOEXCEPT
    {
        return 2147483647;
    }

    static SBE_CONSTEXPR std::size_t sourceRepoIDEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::int32_t sourceRepoID() const SBE_NOEXCEPT
    {
        std::int32_t val;
        std::memcpy(&val, m_buffer + m_offset + 411, sizeof(std::int32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    SecurityDefinitionResponse561 &sourceRepoID(const std::int32_t value) SBE_NOEXCEPT
    {
        std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 411, &val, sizeof(std::int32_t));
        return *this;
    }

    SBE_NODISCARD static const char * TerminationTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t terminationTypeId() SBE_NOEXCEPT
    {
        return 788;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t terminationTypeSinceVersion() SBE_NOEXCEPT
    {
        return 3;
    }

    SBE_NODISCARD bool terminationTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= terminationTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t terminationTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 415;
    }

    static SBE_CONSTEXPR char terminationTypeNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char terminationTypeMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char terminationTypeMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t terminationTypeEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    static SBE_CONSTEXPR std::uint64_t terminationTypeLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD const char *terminationType() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 3)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 415;
    }

    SBE_NODISCARD char *terminationType() SBE_NOEXCEPT
    {
        if (m_actingVersion < 3)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 415;
    }

    SBE_NODISCARD char terminationType(const std::uint64_t index) const
    {
        if (index >= 8)
        {
            throw std::runtime_error("index out of range for terminationType [E104]");
        }

        if (m_actingVersion < 3)
        {
            return (char)0;
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 415 + (index * 1), sizeof(char));
        return (val);
    }

    SecurityDefinitionResponse561 &terminationType(const std::uint64_t index, const char value)
    {
        if (index >= 8)
        {
            throw std::runtime_error("index out of range for terminationType [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 415 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getTerminationType(char *const dst, const std::uint64_t length) const
    {
        if (length > 8)
        {
            throw std::runtime_error("length too large for getTerminationType [E106]");
        }

        if (m_actingVersion < 3)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 415, sizeof(char) * static_cast<size_t>(length));
        return length;
    }

    SecurityDefinitionResponse561 &putTerminationType(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 415, src, sizeof(char) * 8);
        return *this;
    }

    std::string getTerminationTypeAsString() const
    {
        const char *buffer = m_buffer + m_offset + 415;
        size_t length = 0;

        for (; length < 8 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getTerminationTypeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getTerminationTypeAsString();

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
    std::string_view getTerminationTypeAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 415;
        size_t length = 0;

        for (; length < 8 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    SecurityDefinitionResponse561 &putTerminationType(const std::string_view str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 8)
        {
            throw std::runtime_error("string too large for putTerminationType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 415, str.data(), srcLength);
        for (size_t start = srcLength; start < 8; ++start)
        {
            m_buffer[m_offset + 415 + start] = 0;
        }

        return *this;
    }
    #else
    SecurityDefinitionResponse561 &putTerminationType(const std::string& str)
    {
        const size_t srcLength = str.length();
        if (srcLength > 8)
        {
            throw std::runtime_error("string too large for putTerminationType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 415, str.c_str(), srcLength);
        for (size_t start = srcLength; start < 8; ++start)
        {
            m_buffer[m_offset + 415 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char * SecurityResponseTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityResponseTypeId() SBE_NOEXCEPT
    {
        return 323;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityResponseTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityResponseTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityResponseTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityResponseTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 423;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityResponseTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD SecRspTyp::Value securityResponseType() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 423, sizeof(std::uint8_t));
        return SecRspTyp::get((val));
    }

    SecurityDefinitionResponse561 &securityResponseType(const SecRspTyp::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 423, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * UserDefinedInstrumentMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t userDefinedInstrumentId() SBE_NOEXCEPT
    {
        return 9779;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t userDefinedInstrumentSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool userDefinedInstrumentInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= userDefinedInstrumentSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t userDefinedInstrumentEncodingOffset() SBE_NOEXCEPT
    {
        return 424;
    }

    static SBE_CONSTEXPR char userDefinedInstrumentNullValue() SBE_NOEXCEPT
    {
        return (char)0;
    }

    static SBE_CONSTEXPR char userDefinedInstrumentMinValue() SBE_NOEXCEPT
    {
        return (char)32;
    }

    static SBE_CONSTEXPR char userDefinedInstrumentMaxValue() SBE_NOEXCEPT
    {
        return (char)126;
    }

    static SBE_CONSTEXPR std::size_t userDefinedInstrumentEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t userDefinedInstrumentLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD const char *userDefinedInstrument() const
    {
        static const std::uint8_t userDefinedInstrumentValues[] = { 89 };

        return (const char *)userDefinedInstrumentValues;
    }

    SBE_NODISCARD char userDefinedInstrument(const std::uint64_t index) const
    {
        static const std::uint8_t userDefinedInstrumentValues[] = { 89 };

        return (char)userDefinedInstrumentValues[index];
    }

    std::uint64_t getUserDefinedInstrument(char *dst, const std::uint64_t length) const
    {
        static std::uint8_t userDefinedInstrumentValues[] = { 89 };
        std::uint64_t bytesToCopy = length < sizeof(userDefinedInstrumentValues) ? length : sizeof(userDefinedInstrumentValues);

        std::memcpy(dst, userDefinedInstrumentValues, static_cast<size_t>(bytesToCopy));
        return bytesToCopy;
    }

    std::string getUserDefinedInstrumentAsString() const
    {
        static const std::uint8_t UserDefinedInstrumentValues[] = { 89 };

        return std::string((const char *)UserDefinedInstrumentValues, 1);
    }

    std::string getUserDefinedInstrumentAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getUserDefinedInstrumentAsString();

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

    SBE_NODISCARD static const char * ExpirationCycleMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t expirationCycleId() SBE_NOEXCEPT
    {
        return 827;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t expirationCycleSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool expirationCycleInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= expirationCycleSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t expirationCycleEncodingOffset() SBE_NOEXCEPT
    {
        return 424;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t expirationCycleEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD ExpCycle::Value expirationCycle() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 424, sizeof(std::uint8_t));
        return ExpCycle::get((val));
    }

    SecurityDefinitionResponse561 &expirationCycle(const ExpCycle::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 424, &val, sizeof(std::uint8_t));
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
        return 425;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t manualOrderIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD ManualOrdIndReq::Value manualOrderIndicator() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 425, sizeof(std::uint8_t));
        return ManualOrdIndReq::get((val));
    }

    SecurityDefinitionResponse561 &manualOrderIndicator(const ManualOrdIndReq::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 425, &val, sizeof(std::uint8_t));
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
        return 426;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t splitMsgEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD SplitMsg::Value splitMsg() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 426, sizeof(std::uint8_t));
        return SplitMsg::get((val));
    }

    SecurityDefinitionResponse561 &splitMsg(const SplitMsg::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 426, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * AutoQuoteRequestMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t autoQuoteRequestId() SBE_NOEXCEPT
    {
        return 9776;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t autoQuoteRequestSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool autoQuoteRequestInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= autoQuoteRequestSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t autoQuoteRequestEncodingOffset() SBE_NOEXCEPT
    {
        return 427;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t autoQuoteRequestEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD BooleanFlag::Value autoQuoteRequest() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 427, sizeof(std::uint8_t));
        return BooleanFlag::get((val));
    }

    SecurityDefinitionResponse561 &autoQuoteRequest(const BooleanFlag::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 427, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * PossRetransFlagMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t possRetransFlagId() SBE_NOEXCEPT
    {
        return 9765;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t possRetransFlagSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool possRetransFlagInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= possRetransFlagSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t possRetransFlagEncodingOffset() SBE_NOEXCEPT
    {
        return 428;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t possRetransFlagEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD BooleanFlag::Value possRetransFlag() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 428, sizeof(std::uint8_t));
        return BooleanFlag::get((val));
    }

    SecurityDefinitionResponse561 &possRetransFlag(const BooleanFlag::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 428, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char * BrokenDateTermTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t brokenDateTermTypeId() SBE_NOEXCEPT
    {
        return 6651;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t brokenDateTermTypeSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool brokenDateTermTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= brokenDateTermTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t brokenDateTermTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 429;
    }

    static SBE_CONSTEXPR std::uint8_t brokenDateTermTypeNullValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)255;
    }

    static SBE_CONSTEXPR std::uint8_t brokenDateTermTypeMinValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)0;
    }

    static SBE_CONSTEXPR std::uint8_t brokenDateTermTypeMaxValue() SBE_NOEXCEPT
    {
        return (std::uint8_t)254;
    }

    static SBE_CONSTEXPR std::size_t brokenDateTermTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t brokenDateTermType() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return (std::uint8_t)255;
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 429, sizeof(std::uint8_t));
        return (val);
    }

    SecurityDefinitionResponse561 &brokenDateTermType(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 429, &val, sizeof(std::uint8_t));
        return *this;
    }

    class NoLegs
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
            dimensions.blockLength((std::uint16_t)19);
            dimensions.numInGroup((std::uint8_t)count);
            m_index = 0;
            m_count = count;
            m_blockLength = 19;
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
            return 19;
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

        inline NoLegs &next()
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

        SBE_NODISCARD static const char * LegPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "Price";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t legPriceId() SBE_NOEXCEPT
        {
            return 566;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t legPriceSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool legPriceInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= legPriceSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legPriceEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

private:
        PRICENULL9 m_legPrice;

public:
        SBE_NODISCARD PRICENULL9 &legPrice()
        {
            m_legPrice.wrap(m_buffer, m_offset + 0, m_actingVersion, m_bufferLength);
            return m_legPrice;
        }

        SBE_NODISCARD static const char * LegOptionDeltaMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "float";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t legOptionDeltaId() SBE_NOEXCEPT
        {
            return 1017;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t legOptionDeltaSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool legOptionDeltaInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= legOptionDeltaSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legOptionDeltaEncodingOffset() SBE_NOEXCEPT
        {
            return 8;
        }

private:
        Decimal32NULL m_legOptionDelta;

public:
        SBE_NODISCARD Decimal32NULL &legOptionDelta()
        {
            m_legOptionDelta.wrap(m_buffer, m_offset + 8, m_actingVersion, m_bufferLength);
            return m_legOptionDelta;
        }

        SBE_NODISCARD static const char * LegSecurityIDSourceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "char";
                case MetaAttribute::PRESENCE: return "constant";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t legSecurityIDSourceId() SBE_NOEXCEPT
        {
            return 603;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t legSecurityIDSourceSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool legSecurityIDSourceInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= legSecurityIDSourceSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legSecurityIDSourceEncodingOffset() SBE_NOEXCEPT
        {
            return 13;
        }

        static SBE_CONSTEXPR char legSecurityIDSourceNullValue() SBE_NOEXCEPT
        {
            return (char)0;
        }

        static SBE_CONSTEXPR char legSecurityIDSourceMinValue() SBE_NOEXCEPT
        {
            return (char)32;
        }

        static SBE_CONSTEXPR char legSecurityIDSourceMaxValue() SBE_NOEXCEPT
        {
            return (char)126;
        }

        static SBE_CONSTEXPR std::size_t legSecurityIDSourceEncodingLength() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint64_t legSecurityIDSourceLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD const char *legSecurityIDSource() const
        {
            static const std::uint8_t legSecurityIDSourceValues[] = { 56 };

            return (const char *)legSecurityIDSourceValues;
        }

        SBE_NODISCARD char legSecurityIDSource(const std::uint64_t index) const
        {
            static const std::uint8_t legSecurityIDSourceValues[] = { 56 };

            return (char)legSecurityIDSourceValues[index];
        }

        std::uint64_t getLegSecurityIDSource(char *dst, const std::uint64_t length) const
        {
            static std::uint8_t legSecurityIDSourceValues[] = { 56 };
            std::uint64_t bytesToCopy = length < sizeof(legSecurityIDSourceValues) ? length : sizeof(legSecurityIDSourceValues);

            std::memcpy(dst, legSecurityIDSourceValues, static_cast<size_t>(bytesToCopy));
            return bytesToCopy;
        }

        std::string getLegSecurityIDSourceAsString() const
        {
            static const std::uint8_t LegSecurityIDSourceValues[] = { 56 };

            return std::string((const char *)LegSecurityIDSourceValues, 1);
        }

        std::string getLegSecurityIDSourceAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getLegSecurityIDSourceAsString();

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

        SBE_NODISCARD static const char * LegSecurityIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t legSecurityIDId() SBE_NOEXCEPT
        {
            return 602;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t legSecurityIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool legSecurityIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= legSecurityIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legSecurityIDEncodingOffset() SBE_NOEXCEPT
        {
            return 13;
        }

        static SBE_CONSTEXPR std::int32_t legSecurityIDNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_INT32;
        }

        static SBE_CONSTEXPR std::int32_t legSecurityIDMinValue() SBE_NOEXCEPT
        {
            return -2147483647;
        }

        static SBE_CONSTEXPR std::int32_t legSecurityIDMaxValue() SBE_NOEXCEPT
        {
            return 2147483647;
        }

        static SBE_CONSTEXPR std::size_t legSecurityIDEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::int32_t legSecurityID() const SBE_NOEXCEPT
        {
            std::int32_t val;
            std::memcpy(&val, m_buffer + m_offset + 13, sizeof(std::int32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoLegs &legSecurityID(const std::int32_t value) SBE_NOEXCEPT
        {
            std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 13, &val, sizeof(std::int32_t));
            return *this;
        }

        SBE_NODISCARD static const char * LegSideMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t legSideId() SBE_NOEXCEPT
        {
            return 624;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t legSideSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool legSideInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= legSideSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legSideEncodingOffset() SBE_NOEXCEPT
        {
            return 17;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legSideEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD SideReq::Value legSide() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 17, sizeof(std::uint8_t));
            return SideReq::get((val));
        }

        NoLegs &legSide(const SideReq::Value value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 17, &val, sizeof(std::uint8_t));
            return *this;
        }

        SBE_NODISCARD static const char * LegRatioQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t legRatioQtyId() SBE_NOEXCEPT
        {
            return 623;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t legRatioQtySinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool legRatioQtyInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= legRatioQtySinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t legRatioQtyEncodingOffset() SBE_NOEXCEPT
        {
            return 18;
        }

        static SBE_CONSTEXPR std::uint8_t legRatioQtyNullValue() SBE_NOEXCEPT
        {
            return (std::uint8_t)255;
        }

        static SBE_CONSTEXPR std::uint8_t legRatioQtyMinValue() SBE_NOEXCEPT
        {
            return (std::uint8_t)0;
        }

        static SBE_CONSTEXPR std::uint8_t legRatioQtyMaxValue() SBE_NOEXCEPT
        {
            return (std::uint8_t)254;
        }

        static SBE_CONSTEXPR std::size_t legRatioQtyEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t legRatioQty() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 18, sizeof(std::uint8_t));
            return (val);
        }

        NoLegs &legRatioQty(const std::uint8_t value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 18, &val, sizeof(std::uint8_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits>& operator<<(
            std::basic_ostream<CharT, Traits>& builder, NoLegs writer)
        {
            builder << '{';
            builder << R"("LegPrice": )";
            builder << writer.legPrice();

            builder << ", ";
            builder << R"("LegOptionDelta": )";
            builder << writer.legOptionDelta();

            builder << ", ";
            builder << R"("LegSecurityID": )";
            builder << +writer.legSecurityID();

            builder << ", ";
            builder << R"("LegSide": )";
            builder << '"' << writer.legSide() << '"';

            builder << ", ";
            builder << R"("LegRatioQty": )";
            builder << +writer.legRatioQty();

            builder << '}';

            return builder;
        }
    };

private:
    NoLegs m_noLegs;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoLegsId() SBE_NOEXCEPT
    {
        return 555;
    }

    SBE_NODISCARD inline NoLegs &noLegs()
    {
        m_noLegs.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noLegs;
    }

    NoLegs &noLegsCount(const std::uint8_t count)
    {
        m_noLegs.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noLegs;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noLegsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noLegsInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noLegsSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    class NoBrokenDates
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
            dimensions.blockLength((std::uint16_t)16);
            dimensions.numInGroup((std::uint8_t)count);
            m_index = 0;
            m_count = count;
            m_blockLength = 16;
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
            return 16;
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

        inline NoBrokenDates &next()
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

        SBE_NODISCARD static const char * BrokenDateGUIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateGUIDId() SBE_NOEXCEPT
        {
            return 39031;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t brokenDateGUIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool brokenDateGUIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= brokenDateGUIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t brokenDateGUIDEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint64_t brokenDateGUIDNullValue() SBE_NOEXCEPT
        {
            return 0xffffffffffffffffL;
        }

        static SBE_CONSTEXPR std::uint64_t brokenDateGUIDMinValue() SBE_NOEXCEPT
        {
            return 0x0L;
        }

        static SBE_CONSTEXPR std::uint64_t brokenDateGUIDMaxValue() SBE_NOEXCEPT
        {
            return 0xfffffffffffffffeL;
        }

        static SBE_CONSTEXPR std::size_t brokenDateGUIDEncodingLength() SBE_NOEXCEPT
        {
            return 8;
        }

        SBE_NODISCARD std::uint64_t brokenDateGUID() const SBE_NOEXCEPT
        {
            std::uint64_t val;
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint64_t));
            return SBE_LITTLE_ENDIAN_ENCODE_64(val);
        }

        NoBrokenDates &brokenDateGUID(const std::uint64_t value) SBE_NOEXCEPT
        {
            std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
            std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
            return *this;
        }

        SBE_NODISCARD static const char * BrokenDateSecurityIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateSecurityIDId() SBE_NOEXCEPT
        {
            return 39027;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t brokenDateSecurityIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool brokenDateSecurityIDInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= brokenDateSecurityIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t brokenDateSecurityIDEncodingOffset() SBE_NOEXCEPT
        {
            return 8;
        }

        static SBE_CONSTEXPR std::int32_t brokenDateSecurityIDNullValue() SBE_NOEXCEPT
        {
            return 2147483647;
        }

        static SBE_CONSTEXPR std::int32_t brokenDateSecurityIDMinValue() SBE_NOEXCEPT
        {
            return -2147483647;
        }

        static SBE_CONSTEXPR std::int32_t brokenDateSecurityIDMaxValue() SBE_NOEXCEPT
        {
            return 2147483647;
        }

        static SBE_CONSTEXPR std::size_t brokenDateSecurityIDEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::int32_t brokenDateSecurityID() const SBE_NOEXCEPT
        {
            std::int32_t val;
            std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::int32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoBrokenDates &brokenDateSecurityID(const std::int32_t value) SBE_NOEXCEPT
        {
            std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::int32_t));
            return *this;
        }

        SBE_NODISCARD static const char * BrokenDateStartMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateStartId() SBE_NOEXCEPT
        {
            return 6748;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t brokenDateStartSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool brokenDateStartInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= brokenDateStartSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t brokenDateStartEncodingOffset() SBE_NOEXCEPT
        {
            return 12;
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateStartNullValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)65535;
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateStartMinValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)0;
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateStartMaxValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)65534;
        }

        static SBE_CONSTEXPR std::size_t brokenDateStartEncodingLength() SBE_NOEXCEPT
        {
            return 2;
        }

        SBE_NODISCARD std::uint16_t brokenDateStart() const SBE_NOEXCEPT
        {
            std::uint16_t val;
            std::memcpy(&val, m_buffer + m_offset + 12, sizeof(std::uint16_t));
            return SBE_LITTLE_ENDIAN_ENCODE_16(val);
        }

        NoBrokenDates &brokenDateStart(const std::uint16_t value) SBE_NOEXCEPT
        {
            std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
            std::memcpy(m_buffer + m_offset + 12, &val, sizeof(std::uint16_t));
            return *this;
        }

        SBE_NODISCARD static const char * BrokenDateEndMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateEndId() SBE_NOEXCEPT
        {
            return 6741;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t brokenDateEndSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool brokenDateEndInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= brokenDateEndSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t brokenDateEndEncodingOffset() SBE_NOEXCEPT
        {
            return 14;
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateEndNullValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)65535;
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateEndMinValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)0;
        }

        static SBE_CONSTEXPR std::uint16_t brokenDateEndMaxValue() SBE_NOEXCEPT
        {
            return (std::uint16_t)65534;
        }

        static SBE_CONSTEXPR std::size_t brokenDateEndEncodingLength() SBE_NOEXCEPT
        {
            return 2;
        }

        SBE_NODISCARD std::uint16_t brokenDateEnd() const SBE_NOEXCEPT
        {
            std::uint16_t val;
            std::memcpy(&val, m_buffer + m_offset + 14, sizeof(std::uint16_t));
            return SBE_LITTLE_ENDIAN_ENCODE_16(val);
        }

        NoBrokenDates &brokenDateEnd(const std::uint16_t value) SBE_NOEXCEPT
        {
            std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
            std::memcpy(m_buffer + m_offset + 14, &val, sizeof(std::uint16_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits>& operator<<(
            std::basic_ostream<CharT, Traits>& builder, NoBrokenDates writer)
        {
            builder << '{';
            builder << R"("BrokenDateGUID": )";
            builder << +writer.brokenDateGUID();

            builder << ", ";
            builder << R"("BrokenDateSecurityID": )";
            builder << +writer.brokenDateSecurityID();

            builder << ", ";
            builder << R"("BrokenDateStart": )";
            builder << +writer.brokenDateStart();

            builder << ", ";
            builder << R"("BrokenDateEnd": )";
            builder << +writer.brokenDateEnd();

            builder << '}';

            return builder;
        }
    };

private:
    NoBrokenDates m_noBrokenDates;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoBrokenDatesId() SBE_NOEXCEPT
    {
        return 39026;
    }

    SBE_NODISCARD inline NoBrokenDates &noBrokenDates()
    {
        m_noBrokenDates.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noBrokenDates;
    }

    NoBrokenDates &noBrokenDatesCount(const std::uint8_t count)
    {
        m_noBrokenDates.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noBrokenDates;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noBrokenDatesSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool noBrokenDatesInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noBrokenDatesSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& builder, SecurityDefinitionResponse561 _writer)
    {
        SecurityDefinitionResponse561 writer(_writer.m_buffer, _writer.m_offset,
            _writer.m_bufferLength, _writer.sbeBlockLength(), _writer.m_actingVersion);
        builder << '{';
        builder << R"("Name": "SecurityDefinitionResponse561", )";
        builder << R"("sbeTemplateId": )";
        builder << writer.sbeTemplateId();
        builder << ", ";

        builder << R"("SeqNum": )";
        builder << +writer.seqNum();

        builder << ", ";
        builder << R"("UUID": )";
        builder << +writer.uUID();

        builder << ", ";
        builder << R"("Text": )";
        builder << '"' <<
            writer.getTextAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("FinancialInstrumentFullName": )";
        builder << '"' <<
            writer.getFinancialInstrumentFullNameAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("SenderID": )";
        builder << '"' <<
            writer.getSenderIDAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("Symbol": )";
        builder << '"' <<
            writer.getSymbolAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("PartyDetailsListReqID": )";
        builder << +writer.partyDetailsListReqID();

        builder << ", ";
        builder << R"("SecurityReqID": )";
        builder << +writer.securityReqID();

        builder << ", ";
        builder << R"("SecurityResponseID": )";
        builder << +writer.securityResponseID();

        builder << ", ";
        builder << R"("SendingTimeEpoch": )";
        builder << +writer.sendingTimeEpoch();

        builder << ", ";
        builder << R"("SecurityGroup": )";
        builder << '"' <<
            writer.getSecurityGroupAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("SecurityType": )";
        builder << '"' <<
            writer.getSecurityTypeAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("Location": )";
        builder << '"' <<
            writer.getLocationAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("SecurityID": )";
        builder << +writer.securityID();

        builder << ", ";
        builder << R"("Currency": )";
        builder << '"' <<
            writer.getCurrencyAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("MaturityMonthYear": )";
        builder << writer.maturityMonthYear();

        builder << ", ";
        builder << R"("DelayDuration": )";
        builder << +writer.delayDuration();

        builder << ", ";
        builder << R"("StartDate": )";
        builder << +writer.startDate();

        builder << ", ";
        builder << R"("EndDate": )";
        builder << +writer.endDate();

        builder << ", ";
        builder << R"("MaxNoOfSubstitutions": )";
        builder << +writer.maxNoOfSubstitutions();

        builder << ", ";
        builder << R"("SourceRepoID": )";
        builder << +writer.sourceRepoID();

        builder << ", ";
        builder << R"("TerminationType": )";
        builder << '"' <<
            writer.getTerminationTypeAsJsonEscapedString().c_str() << '"';

        builder << ", ";
        builder << R"("SecurityResponseType": )";
        builder << '"' << writer.securityResponseType() << '"';

        builder << ", ";
        builder << R"("ExpirationCycle": )";
        builder << '"' << writer.expirationCycle() << '"';

        builder << ", ";
        builder << R"("ManualOrderIndicator": )";
        builder << '"' << writer.manualOrderIndicator() << '"';

        builder << ", ";
        builder << R"("SplitMsg": )";
        builder << '"' << writer.splitMsg() << '"';

        builder << ", ";
        builder << R"("AutoQuoteRequest": )";
        builder << '"' << writer.autoQuoteRequest() << '"';

        builder << ", ";
        builder << R"("PossRetransFlag": )";
        builder << '"' << writer.possRetransFlag() << '"';

        builder << ", ";
        builder << R"("BrokenDateTermType": )";
        builder << +writer.brokenDateTermType();

        builder << ", ";
        {
            bool atLeastOne = false;
            builder << R"("NoLegs": [)";
            writer.noLegs().forEach([&](NoLegs& noLegs)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noLegs;
            });
            builder << ']';
        }

        builder << ", ";
        {
            bool atLeastOne = false;
            builder << R"("NoBrokenDates": [)";
            writer.noBrokenDates().forEach([&](NoBrokenDates& noBrokenDates)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noBrokenDates;
            });
            builder << ']';
        }

        builder << '}';

        return builder;
    }
};
}
#endif
