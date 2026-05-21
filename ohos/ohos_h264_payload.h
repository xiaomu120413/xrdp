#ifndef XRDP_OHOS_H264_PAYLOAD_H
#define XRDP_OHOS_H264_PAYLOAD_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xrdp_ohos {

void AppendH264Payload(std::vector<uint8_t>& target, const uint8_t* data, size_t bytes);

} // namespace xrdp_ohos

#endif
