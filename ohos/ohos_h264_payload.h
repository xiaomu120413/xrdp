#ifndef XRDP_OHOS_H264_PAYLOAD_H
#define XRDP_OHOS_H264_PAYLOAD_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xrdp_ohos {

void AppendH264Payload(std::vector<uint8_t>& target, const uint8_t* data, size_t bytes);
bool H264PayloadHasIdr(const uint8_t* data, size_t bytes);
bool H264PayloadHasParameterSet(const uint8_t* data, size_t bytes);
std::string DescribeH264ParameterSets(const uint8_t* data, size_t bytes);

} // namespace xrdp_ohos

#endif
