#pragma once

enum PlayerReq {
  RED_REQ_START = 20001,
  RED_REQ_PAUSE = 20002,
  RED_REQ_SEEK = 20003,
  RED_REQ_INTERNAL_PAUSE = 21001,
  RED_REQ_INTERNAL_PLAYBACK_RATE = 21002
};

enum PlayerMsg {
  RED_MSG_FLUSH = 0,   // 播放器已清空数据，可以开始播放

  RED_MSG_BPREPARED = 2,   // 后台准备完成（与RED_MSG_PREPARED的区别在于不触发界面更新）

  RED_MSG_ERROR = 100,   // 播放器发生错误   arg1 = error

  RED_MSG_PREPARED = 200,   // 播放器已完成初始化，可以开始播放

  RED_MSG_COMPLETED = 300,   // 播放完成

  RED_MSG_VIDEO_SIZE_CHANGED = 400, // 视频分辨率发生变化   arg1 = width, arg2 = height
  RED_MSG_SAR_CHANGED = 401,        // 视频采样宽高比(SAR)变化  arg1 = sar.num, arg2 = sar.den
  RED_MSG_VIDEO_RENDERING_START = 402,   // 视频首次渲染到屏幕
  RED_MSG_AUDIO_RENDERING_START = 403,   // 音频首次输出到声卡
  RED_MSG_VIDEO_ROTATION_CHANGED = 404, // 视频旋转角度变化（0/90/180/270）  arg1 = degree
  RED_MSG_AUDIO_DECODED_START = 405,   // 视频解码器开始输出帧
  RED_MSG_VIDEO_DECODED_START = 406,   // 音频解码器开始输出数据
  RED_MSG_OPEN_INPUT = 407,    // 打开媒体文件/网络流开始
  RED_MSG_FIND_STREAM_INFO = 408,   // 正在解析媒体流信息
  RED_MSG_COMPONENT_OPEN = 409,   // 音视频解码器初始化完成
  RED_MSG_VIDEO_SEEK_RENDERING_START = 410,   // 跳转后视频重新开始渲染
  RED_MSG_AUDIO_SEEK_RENDERING_START = 411,   // 跳转后音频重新开始播放
  RED_MSG_VIDEO_FIRST_PACKET_IN_DECODER = 412,   // 视频解码器收到首帧数据
  RED_MSG_VIDEO_START_ON_PLAYING = 413,   // 视频开始播放

  RED_MSG_BUFFERING_START = 500,   // 缓冲开始
  RED_MSG_BUFFERING_END = 501,   // 缓冲结束
  RED_MSG_BUFFERING_UPDATE = 502, // arg1 = buffering head position in time,
                                  // arg2 = minimum percent in time or bytes
  RED_MSG_BUFFERING_BYTES_UPDATE = 503, // arg1 = cached data in bytes,
                                        // arg2 = high water mark
  RED_MSG_BUFFERING_TIME_UPDATE = 504, // arg1 = cached duration in milliseconds
                                       // arg2 = high water mark

  RED_MSG_SEEK_COMPLETE = 600, // arg1 = seek position
                               // arg2 = error

  RED_MSG_PLAYBACK_STATE_CHANGED = 700,   // 播放状态变化

  RED_MSG_TIMED_TEXT = 800,   // 字幕信息

  RED_MSG_ACCURATE_SEEK_COMPLETE = 900, /* arg1 = current position*/
  RED_MSG_SEEK_LOOP_START = 901,        /* arg1 = loop count */
  RED_MSG_URL_CHANGE = 902,   // 播放的URL发生改变（如HLS分片切换）

  RED_MSG_VIDEO_DECODER_OPEN = 10001,
  RED_MSG_VTB_COLOR_PRIMARIES_SMPTE432 = 10002
};
