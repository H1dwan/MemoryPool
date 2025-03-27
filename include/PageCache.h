//
// Created by 11361 on 25-3-26.
//
#pragma once
#include <cstddef>
#include <map>
#include <mutex>

namespace MemoryPoolV2
{
class PageCache {
public:
    static const size_t PAGE_SIZE = 4096;   // 4K页大小（操作系统分配内存的基本单位）

    // 线程安全的懒汉式单例实现
    static PageCache& getInstance() {
        static PageCache instance;
        return instance;
    }

    // 分配指定页数的内存块（Span）
    void* allocateSpan(size_t num_pages);
    // 释放指定地址和页数的 Span。释放时会尝试合并相邻的 Span，以减少内存碎片
    void deallocateSpan(void* ptr, size_t num_pages);

private:
    PageCache() = default;
    // 用于向操作系统申请指定页数的内存
    void* systemAlloc(size_t num_pages);

private:
    // Span 结构体表示一个连续的内存块
    struct Span {
        void* page_addr;    // 实际物理内存的地址
        size_t num_pages;   // 页数
        Span* next;
    };
    // 按页数管理空闲的 Span 链表。键为页数，值为对应页数的 Span 链表头指针
    std::map<size_t, Span*> free_spans_;
    // 存储页号(页起始地址)到 Span 的映射，用于在释放内存时快速找到对应的 Span
    std::map<void*, Span*> span_map_;
    std::mutex mutex_;
};


} // namespace MemoryPoolV2