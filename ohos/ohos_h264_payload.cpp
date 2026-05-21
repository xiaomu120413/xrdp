#include "ohos/ohos_h264_payload.h"

#include <cstring>

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

} // namespace xrdp_ohos
