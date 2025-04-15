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
#include "libavutil/imgutils.h"

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
        mFrameQueue.abort();
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
    while (mIsRecording) {
        std::shared_ptr<CGlobalBuffer> buffer;
        mFrameQueue.getFrame(buffer);
        // 处理视频帧（示例，音频类似）
        if (buffer == nullptr) {
            continue;
        }
        switch (buffer->pixel_format) {
            case CGlobalBuffer::kUnknow: {
                // 可能需要报错
                break;
            }
            case CGlobalBuffer::kAudio: {
                // 编码音频帧
                AVFrame* audioFrame = convertAudioFrame(buffer);
                if (audioFrame) {
                    avcodec_send_frame(mAudioCodecCtx, audioFrame);
                    writePackets(mAudioCodecCtx, mAudioStream);
                    av_frame_free(&audioFrame);
                }
                break;
            }
            case CGlobalBuffer::kYUV420:
            case CGlobalBuffer::kVTBBuffer:
            case CGlobalBuffer::kMediaCodecBuffer:
            case CGlobalBuffer::kYUVJ420P:
            case CGlobalBuffer::kYUV420P10LE:
            case CGlobalBuffer::kHarmonyVideoDecoderBuffer: {
                // 编码视频帧
                AVFrame* videoFrame = convertVideoFrame(buffer);
                if (videoFrame) {
                    avcodec_send_frame(mVideoCodecCtx, videoFrame);
                    writePackets(mVideoCodecCtx, mVideoStream);
                    av_frame_free(&videoFrame);
                }
                break;
            }
        }
    }

    // 冲刷编码器
    avcodec_send_frame(mVideoCodecCtx, nullptr);
    writePackets(mVideoCodecCtx, mVideoStream);
    avcodec_send_frame(mAudioCodecCtx, nullptr);
    writePackets(mAudioCodecCtx, mAudioStream);

    // 写入文件尾
    av_write_trailer(mFormatCtx);
}

// ================== 数据输入方法 ================== //
void GzRecorder::pushVideoFrame(std::shared_ptr<CGlobalBuffer> buffer) {

}

void GzRecorder::pushAudioFrame(std::shared_ptr<CGlobalBuffer> buffer) {

}

// ================== 数据转换方法 ================== //
AVFrame* GzRecorder::convertVideoFrame(std::shared_ptr<CGlobalBuffer> buffer) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) return nullptr;

    frame->width = buffer->width;
    frame->height = buffer->height;
    frame->format = AV_PIX_FMT_YUV420P;
    frame->pts = av_rescale_q(buffer->pts, {1, 1000000}, mVideoCodecCtx->time_base);

    // 根据不同格式处理数据
    switch (buffer->pixel_format) {
        case CGlobalBuffer::kYUV420:
        case CGlobalBuffer::kYUVJ420P: {
            // 直接使用8bit YUV420数据
            av_frame_get_buffer(frame, 32);
            av_image_fill_arrays(frame->data, frame->linesize, buffer->yBuffer,
                                 AV_PIX_FMT_YUV420P, buffer->width, buffer->height, 1);
            break;
        }
        case CGlobalBuffer::kYUV420P10LE: {
            // 10bit转8bit
            av_frame_get_buffer(frame, 32);
            const uint8_t* srcData[3] = {buffer->yBuffer, buffer->uBuffer, buffer->vBuffer};
            int srcStride[3] = {buffer->yStride, buffer->uStride, buffer->vStride};
            sws_scale(mVideoSwsCtx, srcData, srcStride, 0,
                      buffer->height, frame->data, frame->linesize);
            break;
        }
        case CGlobalBuffer::kVTBBuffer: {
#if defined(__APPLE__)
    // VideoToolbox输出处理
    auto* toolBuffer = static_cast<CGlobalBuffer::VideoToolBufferContext*>(buffer->opaque);
    CVPixelBufferRef pixelBuffer = toolBuffer->buffer;

    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    av_frame_get_buffer(frame, 32);

    // 从CVPixelBuffer提取YUV数据
    uint8_t* dstData[3] = {frame->data[0], frame->data[1], frame->data[2]};
    int dstLinesize[3] = {frame->linesize[0], frame->linesize[1], frame->linesize[2]};

    for (int i = 0; i < CVPixelBufferGetPlaneCount(pixelBuffer); i++) {
        memcpy(dstData[i], CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, i),
              CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, i) * CVPixelBufferGetHeightOfPlane(pixelBuffer, i));
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
#endif
            break;
        }
        default:
            AV_LOGE(TAG, "Unsupported video format: %d", buffer->pixel_format);
            av_frame_free(&frame);
            return nullptr;
    }

    return frame;
}

AVFrame* GzRecorder::convertAudioFrame(std::shared_ptr<CGlobalBuffer> buffer) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) return nullptr;

    frame->nb_samples = buffer->nb_samples;
    frame->channel_layout = mAudioCodecCtx->channel_layout;
    frame->format = mAudioCodecCtx->sample_fmt;
    frame->sample_rate = mAudioCodecCtx->sample_rate;
    frame->pts = av_rescale_q(mAudioPts, {1, frame->sample_rate}, mAudioCodecCtx->time_base);
    mAudioPts += buffer->nb_samples;

    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    // 执行音频重采样（S16 → FLTP）
    const uint8_t* srcData = reinterpret_cast<const uint8_t*>(buffer->audioBuf);
    swr_convert(mAudioSwrCtx, frame->data, buffer->nb_samples, &srcData, buffer->nb_samples);

    return frame;
}

// ================== 编码输出方法 ================== //
void GzRecorder::writePackets(AVCodecContext* codecCtx, AVStream* stream) {
    AVPacket pkt;
    av_init_packet(&pkt);

    while (true) {
        int ret = avcodec_receive_packet(codecCtx, &pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            AV_LOGE(TAG, "Error receiving packet: %d", ret);
            break;
        }

        // 设置输出流时间戳
        av_packet_rescale_ts(&pkt, codecCtx->time_base, stream->time_base);
        pkt.stream_index = stream->index;

        // 写入文件
        if (av_interleaved_write_frame(mFormatCtx, &pkt) < 0) {
            AV_LOGE(TAG, "Error writing packet");
        }

        av_packet_unref(&pkt);
    }
}

// 释放所有资源
void GzRecorder::releaseResources() {
    if (mVideoSwsCtx) sws_freeContext(mVideoSwsCtx);
    if (mAudioSwrCtx) swr_free(&mAudioSwrCtx);
    if (mVideoCodecCtx) avcodec_free_context(&mVideoCodecCtx);
    if (mAudioCodecCtx) avcodec_free_context(&mAudioCodecCtx);
    if (mFormatCtx) avformat_free_context(mFormatCtx);
}

REDPLAYER_NS_END;