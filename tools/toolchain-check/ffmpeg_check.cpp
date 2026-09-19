// Toolchain check: FFmpeg headers + shared libraries. Also previews hardware capabilities.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include <cstdio>
#include <string>

static std::string ver(unsigned v) {
    return std::to_string(AV_VERSION_MAJOR(v)) + "." + std::to_string(AV_VERSION_MINOR(v)) + "." + std::to_string(AV_VERSION_MICRO(v));
}

int main() {
    std::printf("ffmpeg avcodec=%s avformat=%s avutil=%s swscale=%s swresample=%s\n",
                ver(avcodec_version()).c_str(), ver(avformat_version()).c_str(), ver(avutil_version()).c_str(),
                ver(swscale_version()).c_str(), ver(swresample_version()).c_str());
    std::printf("license=%s\n", avcodec_license());

    std::printf("hwdevice types:");
    for (AVHWDeviceType t = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE); t != AV_HWDEVICE_TYPE_NONE;
         t = av_hwdevice_iterate_types(t)) {
        std::printf(" %s", av_hwdevice_get_type_name(t));
    }
    std::printf("\n");

    // Presence of an encoder in the build is NOT the same as it working on this GPU.
    // That is what the Hardware module's runtime detection is for.
    const char* encoders[] = { "libx264", "h264_nvenc", "h264_qsv", "h264_amf", "h264_mf", "libopenh264" };
    std::printf("h264 encoders compiled in:");
    for (const char* e : encoders) {
        if (avcodec_find_encoder_by_name(e)) std::printf(" %s", e);
    }
    std::printf("\n");
    std::printf("h264 decoder: %s\n", avcodec_find_decoder(AV_CODEC_ID_H264) ? "yes" : "no");
    return 0;
}
