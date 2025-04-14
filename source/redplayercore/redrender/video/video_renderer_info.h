#pragma once

#include "./video_inc_internal.h"

NS_REDRENDER_BEGIN

// 渲染类型
typedef enum VRClusterType {
  VRClusterTypeUnknown = 0,             //< 未知/未指定的渲染类型
  VRClusterTypeOpenGL = 1,              //< 基于OpenGL/ES的渲染（跨平台通用）
  VRClusterTypeMetal = 2,               //< 苹果Metal渲染（iOS/macOS专用）
  VRClusterTypeMediaCodec = 3,          //< Android MediaCodec表面渲染（硬件加速）
  VRClusterTypeAVSBDL = 5,              //< iOS AVSampleBufferDisplayLayer（硬件解码渲染）
  VRClusterTypeHarmonyVideoDecoder = 6, //< 鸿蒙系统视频解码渲染
} VRClusterType;

struct VideoRendererInfo {
  VideoRendererInfo(VRClusterType rendererType = VRClusterTypeUnknown);
  ~VideoRendererInfo() = default;
  VRClusterType _videoRendererClusterType;
};

NS_REDRENDER_END
