#pragma once

#include "RedBase.h"

REDPLAYER_NS_BEGIN ;

    class CGlobalBuffer {
    public:
        CGlobalBuffer() = default;
        ~CGlobalBuffer();
        // 显式释放内部资源
        void free();

        // 禁用拷贝和移动操作（确保数据安全）
        CGlobalBuffer(const CGlobalBuffer &buffer) = delete;
        CGlobalBuffer &operator=(const CGlobalBuffer &buffer) = delete;
        CGlobalBuffer(CGlobalBuffer &&buffer) = delete;
        CGlobalBuffer &operator=(CGlobalBuffer &&buffer) = delete;

        // 最大平面数（用于多声道音频）
        static const int MAX_PALANARS = 8;

        // Android MediaCodec硬件解码缓冲区上下文
        struct MediaCodecBufferContext {
            int buffer_index;    // MediaCodec缓冲区索引
            void *media_codec;   // AMediaCodec实例指针
            int decoder_serial;  // 解码器序列号（用于多实例识别）
            void *decoder;       // 解码器对象指针
            void *opaque;        // 用户自定义数据
            // 必须调用的释放回调
            void (*release_output_buffer)(MediaCodecBufferContext *context,
                                          bool render);
        };

        // 鸿蒙系统硬件解码缓冲区上下文
        struct HarmonyMediaBufferContext {
            int buffer_index;
            void *video_decoder;
            int decoder_serial;
            void *decoder;
            void *opaque;
            // 必须调用的释放回调
            void (*release_output_buffer)(HarmonyMediaBufferContext *context,
                                          bool render);
        };

        // FFmpeg软件解码帧上下文
        struct FFmpegBufferContext {
            void *av_frame;    // 指向AVFrame的指针
            void (*release_av_frame)(FFmpegBufferContext *context);   // 释放回调
            void *opaque;      // 用户自定义数据
        };

        // iOS VideoToolbox硬件解码缓冲区
        struct VideoToolBufferContext {
            void *buffer = nullptr;    // CVPixelBufferRef类型
        };

        // 像素格式枚举（跨平台统一标识）
        enum PixelFormat {
            kUnknow = 0,                // 未知格式
            kYUV420 = 1,                // 8bit YUV420P
            kVTBBuffer = 2,             // iOS VideoToolbox输出
            kMediaCodecBuffer = 3,      // Android MediaCodec输出
            kYUVJ420P = 4,              // JPEG标准YUV420（全范围）
            kYUV420P10LE = 5,           // 10bit YUV420P小端
            kAudio = 7,                 // 音频数据标识
            kHarmonyVideoDecoderBuffer = 8  // 鸿蒙视频解码输出
        };

    public:
        /* 基础信息 */
        int format{0};              // 格式标识（与PixelFormat配合使用）
        int serial{-1};             // 序列号（用于Seek后数据同步）
        int64_t datasize{0};        // 数据总大小（字节）
        int64_t dts{0};             // 解码时间戳（微秒）
        double pts{0};              // 显示时间戳（秒，兼容旧系统）

        /* 数据存储 */
        uint8_t *data{nullptr};     // 通用数据指针（兼容旧接口）
        short *audioBuf{nullptr};   // SoundTouch音频处理专用缓冲区

        /* 视频相关参数 */
        int width{0};               // 视频宽度（像素）
        int height{0};              // 视频高度（像素）
        int key_frame{0};           // 是否为关键帧（1=是，0=否）

        /* YUV平面数据（当pixel_format为YUV420系列时有效） */
        int yStride{0}; // stride of Y data buffer
        int uStride{0}; // stride of U data buffer
        int vStride{0}; // stride of V data buffer
        uint8_t *yBuffer{nullptr};  // Y数据指针（亮度）
        uint8_t *uBuffer{nullptr};  // U数据指针（色度）
        uint8_t *vBuffer{nullptr};  // V数据指针（色度）

        /* 平台特定数据 */
        void *opaque{nullptr};      // 指向平台特定结构的指针（如MediaCodecBufferContext）
        PixelFormat pixel_format{kUnknow}; // 当前数据的实际格式

        /**
         * number of audio samples (per channel) described by this frame
         */
        /* 音频相关参数 */
        int nb_samples{0};          // 本帧包含的采样点数（单通道）
        int sample_rate{0};         // 采样率（Hz）
        int num_channels{0};        // 声道数
        uint8_t *channel[MAX_PALANARS]; // 多声道数据指针数组
        uint64_t channel_layout{0}; // 声道布局（参照AV_CH_LAYOUT_*）
    };

REDPLAYER_NS_END;
