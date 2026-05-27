#include "ohos/ohos_h264_payload.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

namespace xrdp_ohos {
namespace {

bool AppendBytes(std::vector<uint8_t>& target, const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes == 0U) {
        return true;
    }
    const size_t oldSize = target.size();
    target.resize(oldSize + bytes);
    std::memcpy(target.data() + oldSize, data, bytes);
    return true;
}

bool AppendStartCode(std::vector<uint8_t>& target)
{
    static const uint8_t kStartCode[] = { 0, 0, 0, 1 };
    return AppendBytes(target, kStartCode, sizeof(kStartCode));
}

uint32_t ReadBe32(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
        (static_cast<uint32_t>(data[1]) << 16U) |
        (static_cast<uint32_t>(data[2]) << 8U) |
        static_cast<uint32_t>(data[3]);
}

bool HasStartCode(const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes < 4U) {
        return false;
    }
    for (size_t index = 0; index + 3U < bytes; ++index) {
        if (data[index] == 0 && data[index + 1U] == 0 && data[index + 2U] == 1) {
            return true;
        }
        if (index + 4U < bytes && data[index] == 0 && data[index + 1U] == 0 &&
            data[index + 2U] == 0 && data[index + 3U] == 1) {
            return true;
        }
    }
    return false;
}

bool AppendAvccPayload(std::vector<uint8_t>& target, const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes < 7U || data[0] != 1U) {
        return false;
    }

    size_t offset = 5U;
    const uint8_t spsCount = data[offset++] & 0x1fU;
    for (uint8_t index = 0; index < spsCount; ++index) {
        if (offset + 2U > bytes) {
            return false;
        }
        const size_t nalBytes = (static_cast<size_t>(data[offset]) << 8U) | data[offset + 1U];
        offset += 2U;
        if (nalBytes == 0U || offset + nalBytes > bytes) {
            return false;
        }
        AppendStartCode(target);
        AppendBytes(target, data + offset, nalBytes);
        offset += nalBytes;
    }
    if (offset >= bytes) {
        return true;
    }

    const uint8_t ppsCount = data[offset++];
    for (uint8_t index = 0; index < ppsCount; ++index) {
        if (offset + 2U > bytes) {
            return false;
        }
        const size_t nalBytes = (static_cast<size_t>(data[offset]) << 8U) | data[offset + 1U];
        offset += 2U;
        if (nalBytes == 0U || offset + nalBytes > bytes) {
            return false;
        }
        AppendStartCode(target);
        AppendBytes(target, data + offset, nalBytes);
        offset += nalBytes;
    }
    return true;
}

bool AppendLengthPrefixedPayload(std::vector<uint8_t>& target, const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes <= 4U) {
        return false;
    }

    size_t offset = 0;
    while (offset + 4U < bytes) {
        const size_t nalBytes = static_cast<size_t>(ReadBe32(data + offset));
        offset += 4U;
        if (nalBytes == 0U || nalBytes > bytes - offset) {
            return false;
        }
        AppendStartCode(target);
        AppendBytes(target, data + offset, nalBytes);
        offset += nalBytes;
    }
    return offset == bytes;
}

class BitReader {
public:
    BitReader(const uint8_t* data, size_t bytes) : data_(data), bytes_(bytes) {}

    bool ReadBits(int bits, uint32_t& value)
    {
        value = 0;
        if (bits < 0 || bits > 32) {
            ok_ = false;
            return false;
        }
        for (int index = 0; index < bits; ++index) {
            if (bitOffset_ >= bytes_ * 8U) {
                ok_ = false;
                return false;
            }
            const uint8_t byte = data_[bitOffset_ / 8U];
            const int shift = 7 - static_cast<int>(bitOffset_ % 8U);
            value = (value << 1U) | ((byte >> shift) & 1U);
            ++bitOffset_;
        }
        return true;
    }

    bool ReadBit(uint32_t& value)
    {
        return ReadBits(1, value);
    }

    bool ReadUe(uint32_t& value)
    {
        int zeroBits = 0;
        uint32_t bit = 0;
        while (true) {
            if (!ReadBit(bit)) {
                return false;
            }
            if (bit != 0U) {
                break;
            }
            ++zeroBits;
            if (zeroBits > 31) {
                ok_ = false;
                return false;
            }
        }
        if (zeroBits == 0) {
            value = 0;
            return true;
        }
        uint32_t suffix = 0;
        if (!ReadBits(zeroBits, suffix)) {
            return false;
        }
        value = ((1U << zeroBits) - 1U) + suffix;
        return true;
    }

    bool ReadSe(int32_t& value)
    {
        uint32_t codeNum = 0;
        if (!ReadUe(codeNum)) {
            return false;
        }
        value = (codeNum & 1U) != 0U ?
            static_cast<int32_t>((codeNum + 1U) / 2U) :
            -static_cast<int32_t>(codeNum / 2U);
        return true;
    }

    bool ok() const
    {
        return ok_;
    }

    size_t bit_offset() const
    {
        return bitOffset_;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t bytes_ = 0;
    size_t bitOffset_ = 0;
    bool ok_ = true;
};

std::vector<uint8_t> BuildRbsp(const uint8_t* data, size_t bytes)
{
    std::vector<uint8_t> rbsp;
    if (data == nullptr || bytes == 0U) {
        return rbsp;
    }
    rbsp.reserve(bytes);
    int zeroCount = 0;
    for (size_t index = 0; index < bytes; ++index) {
        const uint8_t byte = data[index];
        if (zeroCount >= 2 && byte == 0x03U) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        if (byte == 0U) {
            ++zeroCount;
        } else {
            zeroCount = 0;
        }
    }
    return rbsp;
}

bool SkipScalingList(BitReader& reader, uint32_t size)
{
    int32_t lastScale = 8;
    int32_t nextScale = 8;
    for (uint32_t index = 0; index < size; ++index) {
        if (nextScale != 0) {
            int32_t deltaScale = 0;
            if (!reader.ReadSe(deltaScale)) {
                return false;
            }
            nextScale = (lastScale + deltaScale + 256) % 256;
        }
        lastScale = nextScale == 0 ? lastScale : nextScale;
    }
    return true;
}

bool ParseH264Sps(const uint8_t* nal, size_t bytes, std::string& description)
{
    if (nal == nullptr || bytes < 2U || (nal[0] & 0x1fU) != 7U) {
        return false;
    }

    std::vector<uint8_t> rbsp = BuildRbsp(nal + 1, bytes - 1U);
    BitReader reader(rbsp.data(), rbsp.size());
    uint32_t profile = 0;
    uint32_t constraint = 0;
    uint32_t level = 0;
    uint32_t value = 0;
    if (!reader.ReadBits(8, profile) || !reader.ReadBits(8, constraint) ||
        !reader.ReadBits(8, level) || !reader.ReadUe(value)) {
        return false;
    }

    uint32_t chromaFormatIdc = 1;
    const uint32_t highProfiles[] = { 100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135 };
    if (std::find(std::begin(highProfiles), std::end(highProfiles), profile) != std::end(highProfiles)) {
        if (!reader.ReadUe(chromaFormatIdc)) {
            return false;
        }
        if (chromaFormatIdc == 3U && !reader.ReadBits(1, value)) {
            return false;
        }
        if (!reader.ReadUe(value) || !reader.ReadUe(value) || !reader.ReadBits(1, value)) {
            return false;
        }
        if (!reader.ReadBit(value)) {
            return false;
        }
        if (value != 0U) {
            const uint32_t listCount = chromaFormatIdc == 3U ? 12U : 8U;
            for (uint32_t index = 0; index < listCount; ++index) {
                if (!reader.ReadBit(value)) {
                    return false;
                }
                if (value != 0U && !SkipScalingList(reader, index < 6U ? 16U : 64U)) {
                    return false;
                }
            }
        }
    }

    uint32_t log2MaxFrameNumMinus4 = 0;
    uint32_t picOrderCntType = 0;
    if (!reader.ReadUe(log2MaxFrameNumMinus4) || !reader.ReadUe(picOrderCntType)) {
        return false;
    }
    if (picOrderCntType == 0U) {
        if (!reader.ReadUe(value)) {
            return false;
        }
    } else if (picOrderCntType == 1U) {
        int32_t signedValue = 0;
        if (!reader.ReadBit(value) || !reader.ReadSe(signedValue) || !reader.ReadSe(signedValue) ||
            !reader.ReadUe(value)) {
            return false;
        }
        const uint32_t cycleCount = value;
        for (uint32_t index = 0; index < cycleCount; ++index) {
            if (!reader.ReadSe(signedValue)) {
                return false;
            }
        }
    }

    uint32_t maxNumRefFrames = 0;
    uint32_t gapsAllowed = 0;
    uint32_t picWidthInMbsMinus1 = 0;
    uint32_t picHeightInMapUnitsMinus1 = 0;
    uint32_t frameMbsOnlyFlag = 0;
    if (!reader.ReadUe(maxNumRefFrames) || !reader.ReadBit(gapsAllowed) ||
        !reader.ReadUe(picWidthInMbsMinus1) || !reader.ReadUe(picHeightInMapUnitsMinus1) ||
        !reader.ReadBit(frameMbsOnlyFlag)) {
        return false;
    }
    if (frameMbsOnlyFlag == 0U && !reader.ReadBit(value)) {
        return false;
    }
    if (!reader.ReadBit(value)) {
        return false;
    }

    uint32_t cropLeft = 0;
    uint32_t cropRight = 0;
    uint32_t cropTop = 0;
    uint32_t cropBottom = 0;
    if (!reader.ReadBit(value)) {
        return false;
    }
    if (value != 0U && (!reader.ReadUe(cropLeft) || !reader.ReadUe(cropRight) ||
        !reader.ReadUe(cropTop) || !reader.ReadUe(cropBottom))) {
        return false;
    }
    const uint32_t width = ((picWidthInMbsMinus1 + 1U) * 16U) - ((cropLeft + cropRight) * 2U);
    const uint32_t height = ((2U - frameMbsOnlyFlag) * (picHeightInMapUnitsMinus1 + 1U) * 16U) -
        ((cropTop + cropBottom) * 2U);

    uint32_t vuiPresent = 0;
    uint32_t videoSignalPresent = 0;
    uint32_t videoFullRange = 0;
    uint32_t colourDescriptionPresent = 0;
    uint32_t colourPrimaries = 0;
    uint32_t transferCharacteristics = 0;
    uint32_t matrixCoefficients = 0;
    if (!reader.ReadBit(vuiPresent)) {
        return false;
    }
    if (vuiPresent != 0U) {
        uint32_t aspectPresent = 0;
        if (!reader.ReadBit(aspectPresent)) {
            return false;
        }
        if (aspectPresent != 0U) {
            uint32_t aspectIdc = 0;
            if (!reader.ReadBits(8, aspectIdc)) {
                return false;
            }
            if (aspectIdc == 255U && (!reader.ReadBits(16, value) || !reader.ReadBits(16, value))) {
                return false;
            }
        }
        uint32_t overscanPresent = 0;
        if (!reader.ReadBit(overscanPresent)) {
            return false;
        }
        if (overscanPresent != 0U && !reader.ReadBit(value)) {
            return false;
        }
        if (!reader.ReadBit(videoSignalPresent)) {
            return false;
        }
        if (videoSignalPresent != 0U) {
            if (!reader.ReadBits(3, value) || !reader.ReadBit(videoFullRange) ||
                !reader.ReadBit(colourDescriptionPresent)) {
                return false;
            }
            if (colourDescriptionPresent != 0U &&
                (!reader.ReadBits(8, colourPrimaries) ||
                 !reader.ReadBits(8, transferCharacteristics) ||
                 !reader.ReadBits(8, matrixCoefficients))) {
                return false;
            }
        }
    }

    description = "sps profile=" + std::to_string(profile) +
        " constraint=" + std::to_string(constraint) +
        " level=" + std::to_string(level) +
        " size=" + std::to_string(width) + "x" + std::to_string(height) +
        " vui=" + std::to_string(vuiPresent) +
        " video_signal=" + std::to_string(videoSignalPresent) +
        " full_range=" + std::to_string(videoFullRange) +
        " color=" + std::to_string(colourPrimaries) +
        " transfer=" + std::to_string(transferCharacteristics) +
        " matrix=" + std::to_string(matrixCoefficients);
    return reader.ok();
}

size_t StartCodeBytes(const uint8_t* data, size_t offset, size_t bytes)
{
    if (offset + 3U <= bytes && data[offset] == 0 && data[offset + 1U] == 0 &&
        data[offset + 2U] == 1) {
        return 3U;
    }
    if (offset + 4U <= bytes && data[offset] == 0 && data[offset + 1U] == 0 &&
        data[offset + 2U] == 0 && data[offset + 3U] == 1) {
        return 4U;
    }
    return 0U;
}

size_t FindStartCode(const uint8_t* data, size_t offset, size_t bytes)
{
    for (size_t index = offset; index + 3U <= bytes; ++index) {
        if (StartCodeBytes(data, index, bytes) != 0U) {
            return index;
        }
    }
    return bytes;
}

bool NalTypeMatches(uint8_t nalType, uint8_t wanted)
{
    if (wanted == 0xffU) {
        return nalType == 7U || nalType == 8U;
    }
    return nalType == wanted;
}

bool PayloadHasNalTypeWithStartCodes(const uint8_t* data, size_t bytes, uint8_t wanted)
{
    size_t offset = FindStartCode(data, 0, bytes);
    while (offset < bytes) {
        const size_t startBytes = StartCodeBytes(data, offset, bytes);
        if (startBytes == 0U) {
            break;
        }
        const size_t nalOffset = offset + startBytes;
        const size_t nextOffset = FindStartCode(data, nalOffset, bytes);
        if (nalOffset < nextOffset && NalTypeMatches(data[nalOffset] & 0x1fU, wanted)) {
            return true;
        }
        offset = nextOffset;
    }
    return false;
}

bool PayloadHasNalTypeLengthPrefixed(const uint8_t* data, size_t bytes, uint8_t wanted)
{
    if (data == nullptr || bytes <= 4U) {
        return false;
    }

    size_t offset = 0;
    while (offset + 4U < bytes) {
        const size_t nalBytes = static_cast<size_t>(ReadBe32(data + offset));
        offset += 4U;
        if (nalBytes == 0U || nalBytes > bytes - offset) {
            return false;
        }
        if (NalTypeMatches(data[offset] & 0x1fU, wanted)) {
            return true;
        }
        offset += nalBytes;
    }
    return false;
}

bool PayloadHasNalType(const uint8_t* data, size_t bytes, uint8_t wanted)
{
    if (data == nullptr || bytes == 0U) {
        return false;
    }
    if (HasStartCode(data, bytes)) {
        return PayloadHasNalTypeWithStartCodes(data, bytes, wanted);
    }
    if (PayloadHasNalTypeLengthPrefixed(data, bytes, wanted)) {
        return true;
    }
    return NalTypeMatches(data[0] & 0x1fU, wanted);
}

} // namespace

void AppendH264Payload(std::vector<uint8_t>& target, const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes == 0U) {
        return;
    }
    if (HasStartCode(data, bytes)) {
        AppendBytes(target, data, bytes);
        return;
    }

    const size_t oldSize = target.size();
    if (AppendAvccPayload(target, data, bytes)) {
        return;
    }
    target.resize(oldSize);
    if (AppendLengthPrefixedPayload(target, data, bytes)) {
        return;
    }
    target.resize(oldSize);
    AppendBytes(target, data, bytes);
}

bool H264PayloadHasIdr(const uint8_t* data, size_t bytes)
{
    return PayloadHasNalType(data, bytes, 5U);
}

bool H264PayloadHasParameterSet(const uint8_t* data, size_t bytes)
{
    return PayloadHasNalType(data, bytes, 0xffU);
}

std::string DescribeH264ParameterSets(const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes == 0U) {
        return "";
    }

    std::vector<uint8_t> normalized;
    AppendH264Payload(normalized, data, bytes);
    if (normalized.empty()) {
        return "";
    }

    std::string result;
    size_t offset = FindStartCode(normalized.data(), 0, normalized.size());
    while (offset < normalized.size()) {
        const size_t startBytes = StartCodeBytes(normalized.data(), offset, normalized.size());
        if (startBytes == 0U) {
            break;
        }
        const size_t nalOffset = offset + startBytes;
        const size_t nextOffset = FindStartCode(normalized.data(), nalOffset, normalized.size());
        if (nalOffset < nextOffset) {
            const uint8_t nalType = normalized[nalOffset] & 0x1fU;
            if (nalType == 7U) {
                std::string sps;
                if (ParseH264Sps(normalized.data() + nalOffset, nextOffset - nalOffset, sps)) {
                    if (!result.empty()) {
                        result += " ";
                    }
                    result += sps;
                }
            }
        }
        offset = nextOffset;
    }
    return result;
}

} // namespace xrdp_ohos
