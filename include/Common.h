//
// Created by 11361 on 25-3-26.
//

#pragma once
#include <cstddef>
#include <algorithm>

namespace MemoryPoolV2
{
// 对齐数和大小定义
constexpr size_t ALIGNMENT = 8;
constexpr size_t MAX_BYTES = 256 * 1024; // 256KB 内存池所能管理的最大内存块大小
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // ALIGNMENT等于指针void*的大小

// 内存块头部信息
struct BlockHeader {
    size_t size;        // 内存块大小
    bool in_use;        // 使用标志
    BlockHeader* next;  // 指向下一个内存块
};

// 大小类管理
class SizeClass {
public:
    // 将传入的字节数 bytes 向上对齐到 ALIGNMENT 的倍数
    static size_t roundUp(size_t bytes) {
        return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    // 根据传入的字节数 bytes 计算其在空闲列表中的索引
    static size_t getIndex(size_t bytes) {
        // 确保bytes至少为ALIGNMENT
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};
}
