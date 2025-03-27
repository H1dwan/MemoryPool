//
// Created by 11361 on 25-3-26.
//
#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"

namespace MemoryPoolV2
{

/**
 * 通过从中心缓存批量获取内存块，将其中一个返回给调用者，其余的内存块插入到线程本地的自由链表中
 * @param index
 * @return
 */
void* ThreadCache::fetchFromCentralCache(size_t index) {
    // 从中心缓存批量获取内存
    void* start = CentralCache::getInstance().fetchRange(index);
    if (start == nullptr) {
        return nullptr;
    }
    // 取一个返回，其余放入自由链表
    void* result = start;
    free_list_[index] = *reinterpret_cast<void**>(start);
    // 计算从中心缓存获取的内存块数量
    size_t num_batch = 0;
    void* cur = start;
    while (cur) {
        num_batch++;
        cur = *reinterpret_cast<void**>(cur);
    }
    // 更新freeListSize_，增加获取的内存块数量
    free_list_size_[index] += num_batch;
    return result;
}

/**
 * 根据一定的规则决定保留多少内存块在线程本地缓存中，然后将剩余的内存块归还给中心缓存
 * @param start 要归还的内存块链表起始位置的指针
 * @param size  每个内存块的大小
 */
void ThreadCache::returnToCentralCache(void* start, size_t size) {
    // 根据大小计算对应的索引
    size_t index = SizeClass::getIndex(size);
    // 获取对齐后的实际块大小
    size_t aligned_size = SizeClass::roundUp(size);

    // 计算要归还内存块数量
    size_t num_batch = free_list_size_[index];
    if (num_batch <= 1) {
        return; // 如果只有一个块，则不归还
    }
    // 保留一部分在ThreadCache中（比如保留1/4），至少为1
    size_t num_keep = std::max(num_batch / 4, size_t(1));
    size_t num_return = num_batch - num_keep;

    // 寻找分割点
    char* cur = static_cast<char*>(start);
    char* split_node = cur;
    for (size_t i = 0; i < num_keep - 1; ++i) {
        split_node = reinterpret_cast<char*>(*reinterpret_cast<void**>(split_node));
        if (split_node == nullptr) {
            // 如果链表提前结束，更新实际的返回数量
            num_return = num_batch - (i + 1);
            break;
        }
    }

    // 断开链表并更新线程本地缓存
    if (split_node != nullptr) {
        // 将要返回的部分和要保留的部分断开
        void* next_node = *reinterpret_cast<void**>(split_node);
        *reinterpret_cast<void**>(split_node) = nullptr;
        //更新 ThreadCache 的空闲链表
        free_list_[index] = start;
        free_list_size_[index] = num_keep;
        // 将剩余部分返回给 CentralCache
        if (num_return > 0 && next_node != nullptr) {
            CentralCache::getInstance().returnRange(next_node, num_return * aligned_size, index);
        }
    }
}

/**
 * 判断是否需要将内存回收给中心缓存
 * @param index
 * @return
 */
bool ThreadCache::shouldReturnToCentralCache(size_t index) {
    return free_list_size_[index] > threshold_;
}

/**
 * 从线程本地缓存（ThreadCache）中分配指定大小的内存块。如果线程本地缓存中没有可用的内存块，则从中心缓存（CentralCache）获取一批内存块
 * @param size
 * @return
 */
void* ThreadCache::allocate(size_t size) {
    if (size == 0) {
        size = ALIGNMENT;   // 至少分配一个对齐大小
    }
    // 大对象直接从系统分配
    if (size > MAX_BYTES) {
        return malloc(size);
    }
    // 计算索引并更新自由链表大小
    size_t index = SizeClass::getIndex(size);
    free_list_size_[index]--;
    // 检查线程本地自由链表
    if (void* ptr = free_list_[index]) {
        // 将链表头指针后移一位
        free_list_[index] = *reinterpret_cast<void**>(ptr);
        return ptr;
    }
    // 如果线程本地自由链表为空，则从中心缓存获取一批内存
    return fetchFromCentralCache(index);
}

/**
 * 将不再使用的内存块归还给线程本地缓存（ThreadCache），并且在必要时将部分内存块从线程本地缓存归还给中心缓存（CentralCache）
 * @param ptr
 * @param size
 */
void ThreadCache::deallocate(void* ptr, size_t size) {
    if (size > MAX_BYTES) {
        free(ptr);
        return;
    }
    // 将内存块插入线程本地自由链表
    size_t index = SizeClass::getIndex(size);
    *reinterpret_cast<void**>(ptr) = free_list_[index];
    free_list_[index] = ptr;
    free_list_size_[index]++;
    // 判断是否需要将部分内存回收给中心缓存
    if (shouldReturnToCentralCache(index)) {
        returnToCentralCache(free_list_[index], size);
    }
}

}   // namespace MemoryPoolV2