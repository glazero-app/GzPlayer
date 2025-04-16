//
// GzRecorder.cpp - 跨平台原生编码器实现(Android/iOS)
// Created by guoshichao on 2025/4/14.
//

#include "GzRecorder.h"
#include "base/RedBuffer.h"
#include "base/RedQueue.h"
#include "RedLog.h"

#include <atomic>
#include <thread>
#include <mutex>

#if defined(__ANDROID__)
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>
#include <android/api-level.h>
#include <fcntl.h>
#endif
#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AudioToolbox/AudioToolbox.h>
#endif

#define TAG "GzRecorder"

REDPLAYER_NS_BEGIN;

    GzRecorder::GzRecorder(int id) : mID(id) {
#ifdef __APPLE__
        mWriteQueue = dispatch_queue_create("recorder.queue", DISPATCH_QUEUE_SERIAL);
#endif
    }

    GzRecorder::~GzRecorder() {
        if (mIsRecording) {
            stopRecording();
        }
    }

    bool GzRecorder::init(const std::string &path) {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
#if defined(__ANDROID__)
        // 以读写模式打开文件（如果文件不存在则创建）
        int flags = O_CREAT | O_RDWR | O_CLOEXEC;
        int fd = open(path.c_str(), flags, 0644);
        if (fd < 0) {
            AV_LOGE_ID(TAG, mID, "File open failed: %s", strerror(errno));
            return false;
        }
        mMuxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!mMuxer) {
            AV_LOGE_ID(TAG, mID, "mMuxer: %s", strerror(errno));
            close(fd);
            return false;
        }
        return true;
#endif
#if defined(__APPLE__)
    NSError* error = nil;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    mAssetWriter = [[AVAssetWriter alloc] initWithURL:url
                                            fileType:AVFileTypeMPEG4
                                               error:&error];
    if(error) {
        AV_LOGE_ID(TAG, mID, "Init AVAssetWriter failed: %s", [[error localizedDescription] UTF8String]);
        return false;
    }
    return true;
#endif
    }

    bool GzRecorder::initVideoEncoder(int width, int height, float fps) {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
#if defined(__ANDROID__)
        mVideoFormat = AMediaFormat_new();
        AMediaFormat_setString(mVideoFormat, AMEDIAFORMAT_KEY_MIME, "video/avc");
        AMediaFormat_setInt32(mVideoFormat, AMEDIAFORMAT_KEY_WIDTH, width);
        AMediaFormat_setInt32(mVideoFormat, AMEDIAFORMAT_KEY_HEIGHT, height);
        AMediaFormat_setFloat(mVideoFormat, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
        AMediaFormat_setInt32(mVideoFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000100); // Flexible YUV
        AMediaFormat_setInt32(mVideoFormat, AMEDIAFORMAT_KEY_BIT_RATE, width * height * 4);
        AMediaFormat_setInt32(mVideoFormat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);

        mVideoCodec = AMediaCodec_createEncoderByType("video/avc");
        AMediaCodec_configure(mVideoCodec, mVideoFormat, nullptr, nullptr,
                              AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaCodec_start(mVideoCodec);
        return true;
#endif
#if defined(__APPLE__)
        NSDictionary* videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height),
        AVVideoScalingModeKey: AVVideoScalingModeResizeAspect,
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(width * height * 4),
            AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel
        }
    };

    mVideoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                  outputSettings:videoSettings];
    mVideoInput.expectsMediaDataInRealTime = YES;

    if([mAssetWriter canAddInput:mVideoInput]) {
        [mAssetWriter addInput:mVideoInput];
        return true;
    }
    return false;
#endif
    }

    bool GzRecorder::initAudioEncoder(int sampleRate, int channels, int sampleFmt) {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
#if defined(__ANDROID__)
        mAudioFormat = AMediaFormat_new();
        AMediaFormat_setString(mAudioFormat, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32(mAudioFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, sampleRate);
        AMediaFormat_setInt32(mAudioFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, channels);
        AMediaFormat_setInt32(mAudioFormat, AMEDIAFORMAT_KEY_BIT_RATE, 64000);
        AMediaFormat_setInt32(mAudioFormat, AMEDIAFORMAT_KEY_AAC_PROFILE, 2); // AAC LC

        mAudioCodec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
        AMediaCodec_configure(mAudioCodec, mAudioFormat, nullptr, nullptr,
                              AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaCodec_start(mAudioCodec);
        return true;
#endif
#if defined(__APPLE__)
        AudioChannelLayout acl = {
        .mChannelLayoutTag = kAudioChannelLayoutTag_DiscreteInOrder | channels
    };

    NSDictionary* audioSettings = @{
        AVFormatIDKey: @(kAudioFormatMPEG4AAC),
        AVSampleRateKey: @(sampleRate),
        AVNumberOfChannelsKey: @(channels),
        AVChannelLayoutKey: [NSData dataWithBytes:&acl length:sizeof(acl)],
        AVEncoderBitRateKey: @(64000)
    };

    mAudioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                                                  outputSettings:audioSettings];
    mAudioInput.expectsMediaDataInRealTime = YES;

    if([mAssetWriter canAddInput:mAudioInput]) {
        [mAssetWriter addInput:mAudioInput];
        return true;
    }
    return false;
#endif
    }

    void GzRecorder::startRecording() {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
        if (mIsRecording) return;
        mIsRecording = true;
        mEncodeThread = std::thread(&GzRecorder::encodeLoop, this);
    }

    void GzRecorder::stopRecording() {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
        mIsRecording = false;
        if (mEncodeThread.joinable()) {
            mFrameQueue.abort();
            mEncodeThread.join();
        }
        releaseResources();
    }

    bool GzRecorder::isRecording() const {
        return mIsRecording;
    }

    void GzRecorder::pushVideoFrame(std::shared_ptr<CGlobalBuffer> buffer) {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
        mFrameQueue.putFrame(buffer);
    }

    void GzRecorder::pushAudioFrame(std::shared_ptr<CGlobalBuffer> buffer) {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
        mFrameQueue.putFrame(buffer);
    }

    void GzRecorder::encodeLoop() {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
#if defined(__ANDROID__)
        androidEncodeLoop();
#endif
#if defined(__APPLE__)
        appleEncodeLoop();
#endif
    }

#if defined(__ANDROID__)
    void GzRecorder::androidEncodeLoop() {
        bool muxerStarted = false;
        int videoTrackIndex = -1;
        int audioTrackIndex = -1;

        while (mIsRecording) {
            std::shared_ptr<CGlobalBuffer> buffer;
            mFrameQueue.getFrame(buffer);
            if (!buffer) continue;

            if (buffer->pixel_format == CGlobalBuffer::kAudio) {
                // 处理音频帧
                ssize_t inIndex = AMediaCodec_dequeueInputBuffer(mAudioCodec, 2000);
                if (inIndex >= 0) {
                    size_t bufSize;
                    uint8_t* buf = AMediaCodec_getInputBuffer(mAudioCodec, inIndex, &bufSize);
                    if (buf && buffer->datasize <= bufSize) {
                        memcpy(buf, buffer->audioBuf, buffer->datasize);
                        AMediaCodec_queueInputBuffer(mAudioCodec, inIndex, 0, buffer->datasize,
                                                     buffer->pts / 1000, 0);
                    }
                }

                // 处理编码输出
                AMediaCodecBufferInfo info;
                ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mAudioCodec, &info, 0);
                if (outIndex >= 0) {
                    if (!muxerStarted && (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                        AMediaFormat* format = AMediaCodec_getOutputFormat(mAudioCodec);
                        audioTrackIndex = AMediaMuxer_addTrack(mMuxer, format);
                        AMediaFormat_delete(format);

                        if (videoTrackIndex >= 0 && audioTrackIndex >= 0) {
                            AMediaMuxer_start(mMuxer);
                            muxerStarted = true;
                        }
                    } else if (muxerStarted) {
                        AMediaMuxer_writeSampleData(mMuxer, audioTrackIndex,
                                                    AMediaCodec_getOutputBuffer(mAudioCodec, outIndex, nullptr),
                                                    &info);
                    }
                    AMediaCodec_releaseOutputBuffer(mAudioCodec, outIndex, false);
                }
            } else {
                // 处理视频帧（逻辑类似）
                // ...
            }
        }

        // 冲刷编码器
#if __ANDROID_API__ >= 26
        // NDK r21+ 方式（API 26+）
        if (mVideoCodec) AMediaCodec_signalEndOfInputStream(mVideoCodec);
        if (mAudioCodec) AMediaCodec_signalEndOfInputStream(mAudioCodec);
#else
        // API 21-25 的替代方案
        if (mVideoCodec) {
            ssize_t inIndex = AMediaCodec_dequeueInputBuffer(mVideoCodec, 2000);
            if (inIndex >= 0) {
                AMediaCodec_queueInputBuffer(mVideoCodec,inIndex,0, 0, 0,AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            }
        }
        if (mAudioCodec) {
            ssize_t inIndex = AMediaCodec_dequeueInputBuffer(mAudioCodec, 2000);
            if (inIndex >= 0) {
                AMediaCodec_queueInputBuffer(mAudioCodec,inIndex,0, 0, 0,AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            }
        }
#endif
    }
#endif

#if defined(__APPLE__)
    void GzRecorder::appleEncodeLoop() {
    [mAssetWriter startWriting];
    [mAssetWriter startSessionAtSourceTime:kCMTimeZero];

    while (mIsRecording) {
        std::shared_ptr<CGlobalBuffer> buffer;
        mFrameQueue.getFrame(buffer);
        if (!buffer) continue;

        dispatch_sync(mWriteQueue, ^{
            if (buffer->pixel_format == CGlobalBuffer::kAudio) {
                processAppleAudioFrame(buffer.get());
            } else {
                processAppleVideoFrame(buffer.get());
            }
        });
    }

    [mVideoInput markAsFinished];
    [mAudioInput markAsFinished];
    [mAssetWriter finishWritingWithCompletionHandler:^{}];
}

void GzRecorder::processAppleAudioFrame(CGlobalBuffer* buffer) {
    if (!mAudioInput.readyForMoreMediaData) return;

    CMBlockBufferRef blockBuffer = nullptr;
    CMSampleBufferRef sampleBuffer = nullptr;

    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        buffer->audioBuf,
        buffer->datasize,
        kCFAllocatorNull,
        nullptr, 0, buffer->datasize,
        0, &blockBuffer);

    if (status == noErr) {
        const AudioStreamBasicDescription asbd = {
            .mSampleRate = mAudioInput.audioSettings[AVSampleRateKey] ?
                [mAudioInput.audioSettings[AVSampleRateKey] doubleValue] : 44100,
            .mFormatID = kAudioFormatMPEG4AAC,
            .mChannelsPerFrame = (UInt32)[mAudioInput.audioSettings[AVNumberOfChannelsKey] intValue],
            .mBitsPerChannel = 16,
            .mFramesPerPacket = 1024
        };

        CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault,
            &asbd,
            0, nullptr,
            0, nullptr,
            nullptr,
            &mAudioFormatDesc);

        CMSampleTimingInfo timing = {
            .presentationTimeStamp = CMTimeMake(buffer->pts, 1000000)
        };

        status = CMSampleBufferCreate(
            kCFAllocatorDefault,
            blockBuffer,
            true, nullptr, nullptr,
            mAudioFormatDesc,
            1, 1, &timing,
            0, nullptr,
            &sampleBuffer);
    }

    if (status == noErr && sampleBuffer) {
        [mAudioInput appendSampleBuffer:sampleBuffer];
    }

    if (blockBuffer) CFRelease(blockBuffer);
    if (sampleBuffer) CFRelease(sampleBuffer);
}

void GzRecorder::processAppleVideoFrame(CGlobalBuffer* buffer) {
    if (!mVideoInput.readyForMoreMediaData) return;

    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        buffer->width,
        buffer->height,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
        nullptr,
        &pixelBuffer);

    if (status == kCVReturnSuccess) {
        CVPixelBufferLockBaseAddress(pixelBuffer, 0);

        // 填充YUV数据到pixelBuffer
        uint8_t* yDestPlane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
        uint8_t* uvDestPlane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);

        // 实际数据拷贝逻辑根据buffer的具体格式实现
        // ...

        CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

        CMSampleTimingInfo timing = {
            .presentationTimeStamp = CMTimeMake(buffer->pts, 1000000)
        };

        CMSampleBufferRef sampleBuffer = nullptr;
        OSStatus status = CMSampleBufferCreateForImageBuffer(
            kCFAllocatorDefault,
            pixelBuffer,
            true, nullptr, nullptr,
            mVideoFormatDesc,
            &timing,
            &sampleBuffer);

        if (status == noErr && sampleBuffer) {
            [mVideoInput appendSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        }
    }

    if (pixelBuffer) CVPixelBufferRelease(pixelBuffer);
}
#endif

    void GzRecorder::releaseResources() {
        AV_LOGI_ID(TAG, mID, "%s\n", __func__);
#if defined(__ANDROID__)
        if (mMuxer) AMediaMuxer_delete(mMuxer);
        if (mVideoCodec) AMediaCodec_delete(mVideoCodec);
        if (mAudioCodec) AMediaCodec_delete(mAudioCodec);
        if (mVideoFormat) AMediaFormat_delete(mVideoFormat);
        if (mAudioFormat) AMediaFormat_delete(mAudioFormat);
#endif
#if defined(__APPLE__)
    mVideoInput = nil;
    mAudioInput = nil;
    mAssetWriter = nil;
    if (mVideoFormatDesc) CFRelease(mVideoFormatDesc);
    if (mAudioFormatDesc) CFRelease(mAudioFormatDesc);
#endif
    }

REDPLAYER_NS_END;