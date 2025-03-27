//
// Created by 11361 on 25-3-26.
//

#pragma once
#include <array>
#include <mutex>
#include <atomic>
#include "Common.h"

namespace MemoryPoolV2
{
class CentralCache {
public:
    // 获取 CentralCache 类的单例实例
    static CentralCache& getInstance() {
        static CentralCache instance;
        return instance;
    }

    void* fetchRange(size_t index);
    void returnRange(void* start, size_t size, size_t index);

private:
    CentralCache() {
        // 初始时所有的空闲链表
        for (auto& ptr : central_free_list_) {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        // 初始化所有锁
        for (auto& lock : locks_) {
            lock.clear();
        }
    }

    // 从页缓存（PageCache）中获取指定大小 size 的内存块
    static void* fetchFromPageCache(size_t size);

private:
    // 中心缓存的自由链表，用于存储不同大小类别的空闲内存块链表的头指针
    std::array<std::atomic<void*>, FREE_LIST_SIZE> central_free_list_{};
    // 用于保护 central_free_list_ 数组中对应的空闲链表
    std::array<std::atomic_flag, FREE_LIST_SIZE> locks_{};
};
}