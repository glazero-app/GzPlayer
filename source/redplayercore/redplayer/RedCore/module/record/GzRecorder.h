//
// GzRecorder.h - 跨平台原生编码器头文件(Android/iOS)
// Created by guoshichao on 2025/4/14.
//

#pragma once

#include "base/RedBuffer.h"
#include "base/RedQueue.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

// 平台相关前置声明
#if defined(__ANDROID__)
typedef struct AMediaCodec AMediaCodec;
typedef struct AMediaFormat AMediaFormat;
typedef struct AMediaMuxer AMediaMuxer;
#endif
#if defined(__APPLE__)
#import <CoreMedia/CMSampleBuffer.h>
#import <CoreVideo/CVPixelBuffer.h>
@class AVAssetWriter;
@class AVAssetWriterInput;
#endif

REDPLAYER_NS_BEGIN;

/**
 * 跨平台音视频录制器 (Android MediaCodec / iOS AVAssetWriter)
 * 功能：将YUV/PCM数据编码为MP4文件
 */
    class GzRecorder {
    public:
        explicit GzRecorder(int id);
        ~GzRecorder();

        // 初始化输出文件容器
        bool init(const std::string &path);

        // 编码器配置
        bool initVideoEncoder(int width, int height, float fps);
        bool initAudioEncoder(int sampleRate, int channels, int sampleFmt);

        // 控制接口
        void startRecording();
        void stopRecording();
        bool isRecording() const;

        // 数据输入接口（线程安全）
        void pushVideoFrame(std::shared_ptr<CGlobalBuffer> buffer);
        void pushAudioFrame(std::shared_ptr<CGlobalBuffer> buffer);

    private:
        // 平台私有实现
        void encodeLoop();
        void releaseResources();

#if defined(__ANDROID__)
        // Android NDK实现
        void androidEncodeLoop();
#endif
#if defined(__APPLE__)
        // iOS实现
    void appleEncodeLoop();
#endif

        const int mID;
        std::atomic<bool> mIsRecording{false};
        std::thread mEncodeThread;
        FrameQueue mFrameQueue;

#if defined(__ANDROID__)
        // Android MediaCodec资源
        AMediaMuxer* mMuxer = nullptr;
        AMediaCodec* mVideoCodec = nullptr;
        AMediaCodec* mAudioCodec = nullptr;
        AMediaFormat* mVideoFormat = nullptr;
        AMediaFormat* mAudioFormat = nullptr;
#endif
#if defined(__APPLE__)
        // iOS AVFoundation资源
    AVAssetWriter* __strong mAssetWriter = nil;
    AVAssetWriterInput* __strong mVideoInput = nil;
    AVAssetWriterInput* __strong mAudioInput = nil;
    CMFormatDescriptionRef mVideoFormatDesc = nullptr;
    CMFormatDescriptionRef mAudioFormatDesc = nullptr;
    dispatch_queue_t mWriteQueue;
#endif
    };

REDPLAYER_NS_END;