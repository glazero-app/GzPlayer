//
// Created by guoshichao on 2025/4/14.
//

#include "GzRecorder.h"
#include "RedLog.h"

#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

#define TAG "GzRecorder"

REDPLAYER_NS_BEGIN;

GzRecorder::GzRecorder() {
    // 初始化FFmpeg库（线程安全）
    static std::once_flag ffmpeg_init_flag;
    std::call_once(ffmpeg_init_flag, [](){
        avformat_network_init();
    });
}

GzRecorder::~GzRecorder() {
    stopRecording();
}

// 初始化视频编码器（H.264）
bool GzRecorder::initVideoEncoder(int width, int height, int fps) {
    // 1. 创建输出上下文
    int ret = avformat_alloc_output_context2(&mFormatCtx, nullptr, nullptr,
                                             "/sdcard/Android/data/com.xingin.openredplayercore/output.mp4");
    if (ret < 0) {
        AV_LOGE(TAG, "avformat_alloc_output_context2 failed: %d \n", ret);
        return false;
    }

    // 2. 添加视频流
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    mVideoStream = avformat_new_stream(mFormatCtx, codec);

    // 3. 配置编码参数
    mVideoCodecCtx = avcodec_alloc_context3(codec);
    mVideoCodecCtx->width = width;
    mVideoCodecCtx->height = height;
    mVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    mVideoCodecCtx->time_base = {1, fps};
    mVideoCodecCtx->bit_rate = width * height * 4; // 粗略计算码率
    avcodec_open2(mVideoCodecCtx, codec, nullptr);

    // 4. 初始化像素转换器
    mVideoSwsCtx = sws_getContext(width, height, AV_PIX_FMT_YUV420P10LE,
                                  width, height, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
    return true;
}

// 初始化音频编码器（AAC）
bool GzRecorder::initAudioEncoder(int sampleRate, int channels) {
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    mAudioStream = avformat_new_stream(mFormatCtx, codec);

    mAudioCodecCtx = avcodec_alloc_context3(codec);
    mAudioCodecCtx->sample_rate = sampleRate;
    mAudioCodecCtx->channel_layout = av_get_default_channel_layout(channels);
    mAudioCodecCtx->channels = channels;
    mAudioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    avcodec_open2(mAudioCodecCtx, codec, nullptr);

    // 初始化音频重采样器（S16 → FLTP）
    mAudioSwrCtx = swr_alloc_set_opts(nullptr,
                                      mAudioCodecCtx->channel_layout,
                                      mAudioCodecCtx->sample_fmt,
                                      mAudioCodecCtx->sample_rate,
                                      av_get_default_channel_layout(channels),
                                      AV_SAMPLE_FMT_S16,
                                      sampleRate, 0, nullptr);
    swr_init(mAudioSwrCtx);
    return true;
}

// 启动编码线程
void GzRecorder::startRecording() {
    if (mIsRecording) return;
    mIsRecording = true;
    mEncodeThread = std::thread(&GzRecorder::encodeLoop, this);
}

// 停止录制（阻塞等待线程结束）
void GzRecorder::stopRecording() {
    mIsRecording = false;
    if (mEncodeThread.joinable()) {
        mQueueCond.notify_all(); // 唤醒可能等待的线程
        mEncodeThread.join();
    }
    releaseResources();
}

// 编码线程主循环
void GzRecorder::encodeLoop() {
    // 打开输出文件
    int ret = avio_open(&mFormatCtx->pb, mFormatCtx->url, AVIO_FLAG_WRITE);
    if (ret < 0) {
        AV_LOGE(TAG, "Failed to open output file\n");
        return;
    }

    // 写入文件头
    ret = avformat_write_header(mFormatCtx, nullptr);
    if (ret < 0) {
        AV_LOGE(TAG, "Failed to write header: %d\n", ret);
        return;
    }

    // 主循环
    while (mIsRecording || !mVideoQueue.empty() || !mAudioQueue.empty()) {
        // 处理视频帧（示例，音频类似）
        std::unique_lock<std::mutex> lock(mQueueMutex);
        if (!mVideoQueue.empty()) {
            CGlobalBuffer* buffer = mVideoQueue.front();
            mVideoQueue.pop();
            lock.unlock();

            AVFrame* frame = convertVideoFrame(buffer);
            if (frame) {
                avcodec_send_frame(mVideoCodecCtx, frame);
                writePackets(mVideoCodecCtx, mVideoStream);
                av_frame_free(&frame);
            }
            delete buffer; // 释放深拷贝数据
        } else {
            mQueueCond.wait_for(lock, std::chrono::milliseconds(10));
        }
    }

    // 冲刷编码器
    avcodec_send_frame(mVideoCodecCtx, nullptr);
    writePackets(mVideoCodecCtx, mVideoStream);

    // 写入文件尾
    av_write_trailer(mFormatCtx);
}

// 释放所有资源
void GzRecorder::releaseResources() {
    if (mVideoSwsCtx) sws_freeContext(mVideoSwsCtx);
    if (mAudioSwrCtx) swr_free(&mAudioSwrCtx);
    if (mVideoCodecCtx) avcodec_free_context(&mVideoCodecCtx);
    if (mAudioCodecCtx) avcodec_free_context(&mAudioCodecCtx);
    if (mFormatCtx) avformat_free_context(mFormatCtx);

    // 清空队列
    std::lock_guard<std::mutex> lock(mQueueMutex);
    while (!mVideoQueue.empty()) {
        delete mVideoQueue.front();
        mVideoQueue.pop();
    }
}

REDPLAYER_NS_END;