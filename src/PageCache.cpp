//
// Created by 11361 on 25-3-26.
//
#include <sys/mman.h>
#include <cstring>
#include "PageCache.h"

namespace MemoryPoolV2
{
    void* PageCache::systemAlloc(size_t num_pages) {
        size_t total_size = num_pages * PAGE_SIZE;
        // TODO: 使用mmap分配内存
        void* ptr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return nullptr;
        }
        // 清零内存
        memset(ptr, 0, total_size);
        return ptr;
    }

    void* PageCache::allocateSpan(size_t num_pages) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 1. 从freeSpans_ 中查找合适的空闲span
        auto iter = free_spans_.lower_bound(num_pages);
        if (iter != free_spans_.end()) {
            auto span = iter->second;
            // 2. 从空闲链表中移除选中的 Span
            if (span->next) {
                // 如果该 Span 后面还有其他 Span，则将链表头指针指向 span->next
                free_spans_[iter->first] = span->next;
            } else {
                // 从 freeSpans_ 中移除该页数对应的链表（无后继）
                free_spans_.erase(iter);
            }
            // 3. 分割 Span（如果必要）
            if (span->num_pages > num_pages) {
                auto new_span = new Span;
                new_span->num_pages = span->num_pages - num_pages;
                new_span->page_addr = static_cast<char*>(span->page_addr) + num_pages * PAGE_SIZE;
                new_span->next = nullptr;
                // 更新原 Span 的页数为 numPages
                span->num_pages = num_pages;
                // 将 newSpan 插入到 freeSpans_ 中对应页数的链表头部
                auto list = free_spans_[new_span->num_pages];
                new_span->next = list;
                free_spans_[new_span->num_pages] = new_span;
            }
            // 4. 记录span信息用于回收
            span_map_[span->page_addr] = span;
            return span->page_addr;
        }

        // 没有合适的span，向系统申请
        void* ptr = systemAlloc(num_pages);
        if (ptr == nullptr) {
            return nullptr;
        }
        // 创建新的span
        auto span = new Span;
        span->page_addr = ptr;
        span->num_pages = num_pages;
        span->next = nullptr;

        // 记录span信息用于回收
        span_map_[ptr] = span;
        return span->page_addr;
    }

    void PageCache::deallocateSpan(void* ptr, size_t num_pages) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 查找对应的span，没找到代表不是PageCache分配的内存，直接返回
        auto iter = span_map_.find(ptr);
        if (iter == span_map_.end()) {
            return;
        }
        auto span = iter->second;

        // 尝试合并相邻的 Span
        void* next_addr = static_cast<char*>(span->page_addr) + span->num_pages * PAGE_SIZE;
        auto next_it = span_map_.find(next_addr);
        // 1. 相邻span是PageCache所分配的
        if (next_it != span_map_.end()) {
            auto next_span = next_it->second;
            // 2. 检查 next_span 是否在空闲链表中（如果在则移除）
            bool found = false;
            auto& next_list = free_spans_[next_span->num_pages];
            if (next_list == next_span) {   // 检查是否是头节点
                next_list = next_span->next;
                found = true;
            } else if (next_list) {     // 链表不为空
                auto pre = next_list;
                while (pre->next) {
                    if (pre->next == next_span) {   // 将nextSpan从空闲链表中移除
                        pre->next = next_span->next;
                        found = true;
                        break;
                    }
                    pre = pre->next;
                }
            }
            // 3. 只有在空闲链表中找到 next_span 的情况下才进行合并
            if (found) {
                span->num_pages += next_span->num_pages;
                span_map_.erase(next_addr);
                delete next_span;
            }
        }
        // 将合并后的span通过头插法插入空闲列表
        span->next = free_spans_[span->num_pages];
        free_spans_[span->num_pages] = span;
    }
}  // namespace MemoryPoolV2