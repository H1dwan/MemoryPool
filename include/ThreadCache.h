//
// Created by 11361 on 25-3-26.
//

#pragma once
#include <array>
#include "../include/Common.h"

namespace MemoryPoolV2
{
class ThreadCache{
public:
    static ThreadCache& getInstance() {
        static thread_local ThreadCache instance;
        return instance;
    }

    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

private:
    explicit ThreadCache(size_t threshold = 64) : threshold_(threshold){
        // 初始化自由链表和大小统计
        free_list_.fill(nullptr);
        free_list_size_.fill(0);
    }

    // 从中心缓存获取内存
    void* fetchFromCentralCache(size_t index);
    // 归还内存到中心缓存
    void returnToCentralCache(void* start, size_t size);
    // 判断是否需要将线程本地缓存中的内存块归还给中心缓存
    bool shouldReturnToCentralCache(size_t index);
    // 计算批量获取内存块的数量
    size_t getBatchNum(size_t size);

private:
    size_t threshold_;
    std::array<void*, FREE_LIST_SIZE> free_list_{};   // 存储单个线程的自由链表
    std::array<size_t, FREE_LIST_SIZE> free_list_size_{}; // 用于统计每个自由链表的大小。每个元素表示对应自由链表中内存块的数量
};
}   // namespace MemoryPoolV2
