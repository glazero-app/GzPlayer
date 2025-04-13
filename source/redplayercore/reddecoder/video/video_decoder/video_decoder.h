#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "reddecoder/common/buffer.h"
#include "reddecoder/video/video_codec_info.h"
#include "reddecoder/video/video_common_definition.h"

namespace reddecoder {

    struct CodecContext {
        int width = 0;   // 视频宽度
        int height = 0;   // 视频高度
        bool is_hdr = false;    // 是否HDR视频
        bool is_annexb_extradata = false;    // 是否Annex-B格式的extradata
        int nal_size = 4;   // NAL单元长度字段字节数（通常为4）
        int rotate_degree = 0;   // 旋转角度（0/90/180/270）
        VideoColorRange color_range = VideoColorRange::kMPEG;   // 颜色范围（MPEG/JPEG）
        VideoSampleAspectRatio sample_aspect_ratio;   // 像素宽高比
    };

    class VideoDecodedCallback {
    public:
        // 解码完成回调（输出视频帧）
        virtual VideoCodecError
        on_decoded_frame(std::unique_ptr<Buffer> decoded_frame) = 0;
        // 解码错误回调
        virtual void on_decode_error(VideoCodecError error,
                                     int internal_error_code = 0) = 0;

        virtual ~VideoDecodedCallback() = default;
    };

    class VideoDecoder {
    public:
        VideoDecoder(VideoCodecInfo codec);
        virtual ~VideoDecoder();
        virtual VideoCodecError init(const Buffer *buffer = nullptr) = 0;
        virtual VideoCodecError release() = 0;
        virtual VideoCodecError decode(const Buffer *buffer) = 0;
        virtual VideoCodecError
        register_decode_complete_callback(VideoDecodedCallback *callback,
                                          bool free_callback_when_release = false);
        virtual VideoDecodedCallback *get_decode_complete_callback();
        virtual VideoCodecError set_option(std::string key, std::string value);
        virtual std::string get_option(std::string key);
        virtual VideoCodecError
        set_video_format_description(const Buffer *buffer) = 0;
        virtual VideoCodecError flush() = 0;
        virtual VideoCodecError get_delayed_frames() = 0;
        virtual VideoCodecError get_delayed_frame() = 0;

        virtual VideoCodecError set_hardware_context(HardWareContext *context) {
            return VideoCodecError::kNotSupported;
        }
        virtual VideoCodecError update_hardware_context(HardWareContext *context) {
            return VideoCodecError::kNotSupported;
        }
        virtual VideoCodecError set_skip_frame(int skip_frame) {
            return VideoCodecError::kNotSupported;
        }

    protected:
        VideoDecodedCallback *video_decoded_callback_ = nullptr;
        bool free_callback_when_release_ = false;
        VideoCodecInfo codec_info_;
        std::map<std::string, std::string> options_;
        std::mutex option_mutex_;
    };
} // namespace reddecoder
