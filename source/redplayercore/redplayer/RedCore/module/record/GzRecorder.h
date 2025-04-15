//
// Created by guoshichao on 2025/4/14.
//

#pragma once

#include "base/RedBuffer.h"
#include "base/RedQueue.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

// FFmpeg 前置声明（避免直接包含FFmpeg头文件污染全局命名空间）
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct SwrContext;
struct AVFrame;

REDPLAYER_NS_BEGIN;

/**
 * MP4文件录制器（线程安全）
 * 功能：将CGlobalBuffer中的YUV/PCM数据编码为MP4
 */
    class GzRecorder {
    public:
        GzRecorder(int id);
        ~GzRecorder();

        // 初始化容器上下文
        bool init(const std::string &path);

        // 初始化编码器
        bool initVideoEncoder(int width, int height, float fps);
        bool initAudioEncoder(int sampleRate, int channels, int sampleFmt);

        // 控制接口
        void startRecording(const std::string& path);
        void stopRecording();
        bool isRecording();

        // 数据输入（自动深拷贝）
        void pushVideoFrame(std::shared_ptr<CGlobalBuffer> buffer);
        void pushAudioFrame(std::shared_ptr<CGlobalBuffer> buffer);

    private:
        const int mID{0};
        // FFmpeg 核心对象
        AVFormatContext* mFormatCtx = nullptr;
        AVCodecContext* mVideoCodecCtx = nullptr;
        AVCodecContext* mAudioCodecCtx = nullptr;
        AVStream* mVideoStream = nullptr;
        AVStream* mAudioStream = nullptr;
        SwsContext* mVideoSwsCtx = nullptr;
        SwrContext* mAudioSwrCtx = nullptr;

        // 线程控制
        std::thread mEncodeThread;
        FrameQueue mFrameQueue;
        std::atomic<bool> mIsRecording{false};
        int64_t mAudioPts = 0; // 音频时间戳累加器

        // 内部方法
        void encodeLoop();
        void releaseResources();
        AVFrame* convertVideoFrame(std::shared_ptr<CGlobalBuffer> buffer);
        AVFrame* convertAudioFrame(std::shared_ptr<CGlobalBuffer> buffer);
        void writePackets(AVCodecContext* codecCtx, AVStream* stream);
    };

REDPLAYER_NS_END;