/*
 * RedQueue.cpp
 *
 *  Created on: 2022年7月25日
 *      Author: liuhongda
 */
#include "RedQueue.h"
#include "RedBase.h"
#include "RedDebug.h"
#include "RedError.h"
#include <algorithm>

#define TAG "RedQueue"

REDPLAYER_NS_BEGIN ;

    // CRedThreadBase 类的构造函数
    // 初始化 mAbort 标志为 false，表示线程未被中止
    CRedThreadBase::CRedThreadBase() { mAbort = false; }

    // CRedThreadBase 类的析构函数
    // 检查线程是否可连接，如果可连接则等待线程结束，避免资源泄漏
    CRedThreadBase::~CRedThreadBase() {
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    // 启动线程的方法
    // 如果 mAbort 标志为 true，表示线程已被中止，直接返回
    // 尝试创建一个新线程，执行 ThreadFunc 方法
    // 捕获可能的系统错误异常并记录日志
    // 捕获其他未知异常并记录日志
    void CRedThreadBase::run() {
        if (mAbort)
            return;
        try {
            mThread = std::thread(std::bind(&CRedThreadBase::ThreadFunc, this));
        } catch (const std::system_error &e) {
            AV_LOGE(TAG, "[%s:%d] Exception caught: %s!\n", __FUNCTION__, __LINE__,
                    e.what());
            return;
        } catch (...) {
            AV_LOGE(TAG, "[%s:%d] Exception caught!\n", __FUNCTION__, __LINE__);
            return;
        }
    }

    // PktQueue 类的构造函数
    // 注释掉的部分原本用于初始化 mType 成员变量，这里暂时不进行初始化
    PktQueue::PktQueue(int type) /*: mType(type)*/ {}

    // 向 PktQueue 队列中添加一个数据包的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 如果传入的数据包不为空，更新队列的总字节数和总时长
    // 将数据包移动到队列中，并通知等待的线程队列不为空
    // 返回操作结果为 OK
    RED_ERR PktQueue::putPkt(std::unique_ptr<RedAvPacket> &pkt) {
        std::unique_lock<std::mutex> lck(mLock);
        AVPacket *packet = pkt ? pkt->GetAVPacket() : nullptr;
        mBytes += packet ? packet->size : 0;
        mDuration +=
                packet ? std::max(packet->duration, (int64_t) MIN_PKT_DURATION) : 0;
        mPktQueue.push(std::move(pkt));
        mNotEmptyCond.notify_one();
        return OK;
    }

    // 从 PktQueue 队列中获取一个数据包的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 如果队列空且不阻塞，则返回 ME_RETRY 表示需要重试
    // 如果队列空且阻塞，则等待队列不为空或超时
    // 如果 mAbort 标志为 true，表示队列已被中止，直接返回 OK
    // 如果等待超时，记录日志
    // 从队列中取出数据包，更新队列的总字节数和总时长
    // 返回操作结果为 OK
    RED_ERR PktQueue::getPkt(std::unique_ptr<RedAvPacket> &pkt, bool block) {
        std::unique_lock<std::mutex> lck(mLock);
        if (mPktQueue.empty() && !block)
            return ME_RETRY;
        while (mPktQueue.empty() && block) {
            if (mAbort) {
                return OK;
            }
            if (mNotEmptyCond.wait_for(lck, std::chrono::seconds(1)) ==
                std::cv_status::timeout) {
                AV_LOGV(TAG, "pktqueue[%d] EMPTY for 1s!\n", mType);
            }
        }
        pkt = std::move(mPktQueue.front());
        AVPacket *packet = pkt ? pkt->GetAVPacket() : nullptr;
        mBytes -= packet ? packet->size : 0;
        mDuration -=
                packet ? std::max(packet->duration, (int64_t) MIN_PKT_DURATION) : 0;
        mPktQueue.pop();
        return OK;
    }

    // 检查 PktQueue 队列头部是否为刷新数据包的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 如果队列为空，返回 false
    // 如果队列头部为空，返回 false
    // 否则返回队列头部是否为刷新数据包的结果
    bool PktQueue::frontIsFlush() {
        std::unique_lock<std::mutex> lck(mLock);
        if (mPktQueue.empty()) {
            return false;
        }
        if (!mPktQueue.front()) {
            return false;
        }
        return mPktQueue.front()->IsFlushPacket();
    }

    // 刷新 PktQueue 队列的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 遍历队列，统计刷新数据包的数量，并清空队列
    // 重新添加统计数量的刷新数据包到队列中
    // 重置队列的总字节数和总时长为 0
    void PktQueue::flush() {
        std::unique_lock<std::mutex> lck(mLock);
        int flush_pkt_count = 0;
        while (!mPktQueue.empty()) {
            auto pkt = std::move(mPktQueue.front());
            if (pkt->IsFlushPacket()) {
                flush_pkt_count++;
            }
            mPktQueue.pop();
        }
        for (int i = 0; i < flush_pkt_count; ++i) {
            std::unique_ptr<RedAvPacket> flush_pkt(new RedAvPacket(PKT_TYPE_FLUSH));
            mPktQueue.push(std::move(flush_pkt));
        }
        mBytes = 0;
        mDuration = 0;
    }

    // 中止 PktQueue 队列操作的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 设置 mAbort 标志为 true，表示队列已被中止
    // 通知等待的线程队列状态已改变
    void PktQueue::abort() {
        std::unique_lock<std::mutex> lck(mLock);
        mAbort = true;
        mNotEmptyCond.notify_one();
    }

    // 清空 PktQueue 队列的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 遍历队列，清空所有数据包
    // 重置队列的总字节数和总时长为 0
    void PktQueue::clear() {
        std::unique_lock<std::mutex> lck(mLock);
        while (!mPktQueue.empty()) {
            mPktQueue.pop();
        }
        mBytes = 0;
        mDuration = 0;
    }

    // 获取 PktQueue 队列大小的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 返回队列中数据包的数量
    size_t PktQueue::size() {
        std::unique_lock<std::mutex> lck(mLock);
        return mPktQueue.size();
    }

    // 获取 PktQueue 队列总字节数的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 返回队列中所有数据包的总字节数
    int64_t PktQueue::bytes() {
        std::unique_lock<std::mutex> lck(mLock);
        return mBytes;
    }

    // 获取 PktQueue 队列总时长的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 返回队列中所有数据包的总时长
    int64_t PktQueue::duration() {
        std::unique_lock<std::mutex> lck(mLock);
        return mDuration;
    }

    // FrameQueue 类的构造函数
    // 初始化队列的容量为传入的参数
    // 注释掉的部分原本用于初始化 mType 成员变量，这里暂时不进行初始化
    FrameQueue::FrameQueue(size_t capacity, int type)
            : mCapacity(capacity) /*, mType(type)*/ {}

    // 向 FrameQueue 队列中添加一个帧的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 如果队列已满且未被中止，则等待队列有空间或超时
    // 如果等待超时，记录日志
    // 将帧移动到队列中，并通知等待的线程队列不为空
    // 返回操作结果为 OK
    RED_ERR FrameQueue::putFrame(std::shared_ptr<CGlobalBuffer> &frame) {
        std::unique_lock<std::mutex> lck(mLock);
        while (mFrameQueue.size() >= mCapacity) {
            if (mAbort) {
                return OK;
            }
            if (mNotFullCond.wait_for(lck, std::chrono::seconds(3)) ==
                std::cv_status::timeout) {
                AV_LOGV(TAG, "framequeue[%d] FULL for 3s!\n", mType);
            }
        }
        mFrameQueue.push(std::move(frame));
        mNotEmptyCond.notify_one();
        return OK;
    }

    // 从 FrameQueue 队列中获取一个帧的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 如果队列空且未被中止和唤醒，则等待队列不为空或超时
    // 如果等待超时，记录日志
    // 如果 mAbort 标志为 true，表示队列已被中止，直接返回 OK
    // 如果 mWakeup 标志为 true，表示队列被唤醒，重置标志并返回 OK
    // 从队列中取出帧，并通知等待的线程队列有空间
    // 返回操作结果为 OK
    RED_ERR FrameQueue::getFrame(std::shared_ptr<CGlobalBuffer> &frame) {
        std::unique_lock<std::mutex> lck(mLock);
        while (mFrameQueue.empty()) {
            if (mAbort) {
                return OK;
            }
            if (mWakeup) {
                mWakeup = false;
                return OK;
            }
            if (mNotEmptyCond.wait_for(lck, std::chrono::seconds(1)) ==
                std::cv_status::timeout) {
                AV_LOGV(TAG, "framequeue[%d] EMPTY for 1s!\n", mType);
            }
        }
        frame = std::move(mFrameQueue.front());
        mFrameQueue.pop();
        mNotFullCond.notify_one();
        return OK;
    }

    // 刷新 FrameQueue 队列的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 遍历队列，清空所有帧
    // 通知等待的线程队列有空间
    void FrameQueue::flush() {
        std::unique_lock<std::mutex> lck(mLock);
        while (!mFrameQueue.empty()) {
            mFrameQueue.pop();
        }
        mNotFullCond.notify_one();
    }

    // 中止 FrameQueue 队列操作的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 设置 mAbort 标志为 true，表示队列已被中止
    // 通知等待的线程队列状态已改变
    void FrameQueue::abort() {
        std::unique_lock<std::mutex> lck(mLock);
        mAbort = true;
        mNotFullCond.notify_one();
        mNotEmptyCond.notify_one();
    }

    // 唤醒 FrameQueue 队列等待线程的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 设置 mWakeup 标志为 true，表示队列被唤醒
    // 通知等待的线程队列状态已改变
    void FrameQueue::wakeup() {
        std::unique_lock<std::mutex> lck(mLock);
        mWakeup = true;
        mNotEmptyCond.notify_one();
    }

    // 获取 FrameQueue 队列大小的方法
    // 使用互斥锁保护队列操作，确保线程安全
    // 返回队列中帧的数量
    size_t FrameQueue::size() {
        std::unique_lock<std::mutex> lck(mLock);
        return mFrameQueue.size();
    }

REDPLAYER_NS_END;
