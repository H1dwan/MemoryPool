//
// Created by 11361 on 25-3-26.
//
#include <thread>
#include "../include/CentralCache.h"
#include "../include/PageCache.h"
#include "CentralCache.h"

namespace MemoryPoolV2
{
static const size_t SPAN_PAGES = 8; // 每次从PageCache获取 span 大小（以页为单位）

/**
 * 从 PageCache 获取 size 大小的内存块，并根据不同的大小情况采取不同的分配策略
 * @param size  需要分配的内存块的大小
 * @return 内存块的地址
 */
void* CentralCache::fetchFromPageCache(size_t size) {
    // 1. 计算实际需要的页数（向上取整）
    size_t num_pages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;
    // 2. 根据大小决定分配策略
    if (num_pages < SPAN_PAGES) {
        // 小于等于32KB的请求，使用固定8页
        return PageCache::getInstance().allocateSpan(SPAN_PAGES);
    } else {
        // 大于32KB的请求，按实际需求分配
        return PageCache::getInstance().allocateSpan(num_pages);
    }
}

/**
 * 从中心缓存中获取该大小类别的内存块，如果中心缓存为空，则从页缓存中获取新的内存块并将其切分成合适大小的小块，然后返回一个可用的内存块指针
 * @param index 所需内存块的大小类别索引
 * @return 内存块的地址
 */
void* CentralCache::fetchRange(size_t index) {
    // 索引检查，当索引大于等于FREE_LIST_SIZE时，说明申请内存过大应直接向系统申请
    if (index >= FREE_LIST_SIZE) {
        return nullptr;
    }
    // 获取自旋锁
    while (locks_[index].test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();  // 添加线程让步，避免忙等待，避免过度消耗CPU
    }
    //
    void* result = nullptr;
    try {
        // 尝试从中心缓存获取内存块
        result = central_free_list_[index].load(std::memory_order_relaxed);
        if (result) {   // 中心缓存不为空
            // 保存 result 的下一个节点
            // TODO: 每个内存块的开头部分被用作指向下一个内存块的指针，从而形成一个链表结构
            // void** 类型的强制转换是为了操作内存块链表中的指针，实现链表节点的取出和链表头指针的更新
            void* next = *reinterpret_cast<void**>(result);
            // 将 result 与链表断开
            *reinterpret_cast<void**>(result) = nullptr;
            // 更新中心缓存
            central_free_list_[index].store(next, std::memory_order_release);
        } else {    // 中心缓存为空，则从页缓存中获取新的内存块，并切分
            // 1. 从页缓存获取新的内存块
            size_t size = (index + 1) * ALIGNMENT;
            result = fetchFromPageCache(size);
            if (result == nullptr) {    // 从页缓存中获取失败，释放自旋锁并返回
                locks_[index].clear(std::memory_order_release);
                return nullptr;
            }

            // 2. 将获取的内存块切分成小块，并用链表管理
            char* start = reinterpret_cast<char*>(result);
            size_t num_block = (SPAN_PAGES * PageCache::PAGE_SIZE) / size;
            if (num_block > 1) {
                for (size_t i = 1; i < num_block; ++i) {
                    void* cur = start + (i - 1) * size;
                    void* nxt = start + i * size;
                    *reinterpret_cast<void**>(cur) = nxt;
                }
                *reinterpret_cast<void**>(start + (num_block - 1) * size) = nullptr;    // 最后一个block

                // 保存result的下一个节点
                void* next = *reinterpret_cast<void**>(result);
                // 将 result 与链表断开
                *reinterpret_cast<void**>(result) = nullptr;
                // 更新中心缓存
                central_free_list_[index].store(next, std::memory_order_release);
            }
        }
    } catch (...) {
        locks_[index].clear(std::memory_order_release);
        throw;
    }

    // 释放锁
    locks_[index].clear(std::memory_order_release);
    return result;
}

/**
 * 从中心缓存中获取该大小类别的内存块，如果中心缓存为空，则从页缓存中获取新的内存块并将其切分成合适大小的小块，然后返回一个可用的内存块指针
 * @param index 所需内存块的大小类别索引
 * @param num_batch 需要获取的内存块数量
 * @return 内存块的地址
 */
void *CentralCache::fetchRange(size_t index, size_t num_batch)
{
    // 索引检查，当索引大于等于FREE_LIST_SIZE时，说明申请内存过大应直接向系统申请
    if (index >= FREE_LIST_SIZE || num_batch == 0) {
        return nullptr;
    }
    // 获取自旋锁
    while (locks_[index].test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();  // 添加线程让步，避免忙等待，避免过度消耗CPU
    }
    //
    void* result = nullptr;
    try {
        // 尝试从中心缓存获取内存块
        result = central_free_list_[index].load(std::memory_order_relaxed);
        if (result) {   // 中心缓存不为空，则从现有链表中获取指定数量的块
            // 保存 result 的下一个节点
            // TODO: 每个内存块的开头部分被用作指向下一个内存块的指针，从而形成一个链表结构
            // void** 类型的强制转换是为了操作内存块链表中的指针，实现链表节点的取出和链表头指针的更新
            void* cur = result;
            void* pre = nullptr;
            size_t cnt = 0;
            
            while (cur && cnt < num_batch) {
                pre = cur;
                cur = *reinterpret_cast<void**>(cur);
                cnt++;
            }
            if (pre) {
                *reinterpret_cast<void**>(pre) = nullptr;
            }
            // 更新中心缓存
            central_free_list_[index].store(cur, std::memory_order_release);
        } else {    // 中心缓存为空，则从页缓存中获取新的内存块，并切分
            // 1. 从页缓存获取新的内存块
            size_t size = (index + 1) * ALIGNMENT;
            result = fetchFromPageCache(size);
            if (result == nullptr) {    // 从页缓存中获取失败，释放自旋锁并返回
                locks_[index].clear(std::memory_order_release);
                return nullptr;
            }

            // 2. 将获取的内存块切分成小块，并用链表管理
            char* start = reinterpret_cast<char*>(result);
            size_t num_block = (SPAN_PAGES * PageCache::PAGE_SIZE) / size;
            size_t num_alloc = std::min(num_batch, num_block);

            // 构建返回给 ThreadCache 的内存块链表
            if (num_alloc > 1) {
                for (size_t i = 1; i < num_alloc; ++i) {
                    void* cur = start + (i - 1) * size;
                    void* nxt = start + i * size;
                    *reinterpret_cast<void**>(cur) = nxt;
                }
                *reinterpret_cast<void**>(start + (num_alloc - 1) * size) = nullptr;    // 最后一个block
            }
            // 构建保留在 CentralCache 的链表
            if (num_block > num_alloc) {
                void* remain_start = start + num_alloc * size;
                for (size_t i = num_alloc + 1; i < num_block; ++i) {
                    void* cur = start + (i - 1) * size;
                    void* nxt = start + i * size;
                    *reinterpret_cast<void**>(cur) = nxt;
                }
                *reinterpret_cast<void**>(start + (num_block - 1) * size) = nullptr;
                central_free_list_[index].store(remain_start, std::memory_order_release);
            }
        }
    } catch (...) {
        locks_[index].clear(std::memory_order_release);
        throw;
    }

    // 释放锁
    locks_[index].clear(std::memory_order_release);
    return result;
}
/**
 * 将一批内存块归还给中心缓存，并将其合并到对应的空闲链表中
 * @param start 指向要归还的内存块链表的起始地址
 * @param size  要归还的内存块的数量
 * @param index 表示这些内存块所属的大小类别索引，用于定位中心缓存中对应的空闲链表
 */
void CentralCache::returnRange(void* start, size_t size, size_t index) {
    // 当索引大于等于FREE_LIST_SIZE时，说明内存过大应直接向系统归还
    if (start == nullptr || index >= FREE_LIST_SIZE) {
        return;
    }
    // 获取自旋锁
    while (locks_[index].test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    try {
        // 寻找归还内存块链表的最后一个节点
        void* end = start;
        size_t cnt = 1;
        while (*reinterpret_cast<void**>(end) != nullptr && cnt < size) {
            end = *reinterpret_cast<void**>(end);
            ++cnt;
        }
        // 合并链表，将归还的链表连接到中心缓存的链表头部（头插）
        void* head = central_free_list_[index].load(std::memory_order_relaxed); // 中心缓存中对应索引的空闲链表的头指针
        *reinterpret_cast<void**>(end) = head;  // 将原链表头接到归还链表的尾部
        central_free_list_[index].store(end, std::memory_order_release);
    } catch (...) {
        locks_[index].clear(std::memory_order_release);
        throw;
    }
    locks_[index].clear(std::memory_order_release);
}

}   // namespace MemoryPoolV2