//
// Created by 11361 on 25-3-26.
//

#pragma once
#include <atomic>
#include <mutex>
#include <cassert>
#include "ThreadCache.h"

namespace MemoryPoolV1
{
#define MEMORY_POOL_NUM 64
#define SLOT_BASE_SIZE 8
#define MAX_SLOT_SIZE 512

struct Slot {
    std::atomic<Slot*> next;
};

class MemoryPool {
public:
    explicit MemoryPool(size_t block_size = 4096);
    ~MemoryPool();

    void init(size_t);

    void* allocate();
    void deallocate(void*);

private:
    void allocateNewBlock();
    // 计算指针p对齐到slot（align）大小倍数所需填充的字节数
    static size_t padPointer(const char* p, size_t align);

    // 使用CAS操作进行无锁入队和出队
    bool pushFreeList(Slot* slot);
    Slot* popFreeList();

private:
    size_t block_size_;     // 内存块的大小
    size_t slot_size_;      // 内存槽的大小
    Slot* first_block_;     // 指向第一个内存块Block的指针(第一个slot只用来链接，不用于数据存储)
    Slot* cur_slot_;        // 指向当前可用内存槽的指针
    std::atomic<Slot*> free_list_;  // 指向空闲的槽(被使用过后又被释放的槽)
    Slot* last_slot_;       // 指向当前内存块最后一个可用内存槽的指针
    std::mutex mutex_for_block_;    // // 保证多线程情况下避免不必要的重复开辟内存导致的浪费行为
};

class HashBucket {
public:
    static void initMemoryPool();
    // 单例模式
    static MemoryPool& getMemoryPool(size_t index);

    // 根据所需内存的大小，选择合适的方式来分配内存
    static void* useMemory(size_t size) {
        if (size == 0) {
            return nullptr;
        }
        // 大于512字节的内存，则使用 operator new
        if (size > MAX_SLOT_SIZE) {
            return operator new(size);
        }
        // 计算合适的内存池索引，相当于 size / 8 向上取整
        // 因为内存分配只能大不能小，所以通过 (size + 7) / SLOT_BASE_SIZE - 1 来确定索引
        return getMemoryPool((size + 7) % SLOT_BASE_SIZE - 1).allocate();
    }

    static void freeMemory(void* ptr, size_t size) {
        if (ptr == nullptr) {
            return;
        }
        if (size > MAX_SLOT_SIZE) {
            operator delete(ptr);
            return;
        }
        getMemoryPool((size + 7) % SLOT_BASE_SIZE - 1).deallocate(ptr);
    }

    // 利用自定义的内存池机制为对象分配内存，并在这块内存上构造对象
    template<typename T, typename... Args>
    friend T* newElement(Args&&... args);

    // 析构对象，并将内存空间返回给内存池
    template<typename T>
    friend void deleteElement(T* ptr);
};

template<typename T, typename... Args>
T* newElement(Args&& ... args) {
    T* ptr = nullptr;
    // 根据元素大小选取合适的内存池分配内存
    if ((ptr = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr) {
        // 在分配的内存上构造对象（使用 std::forward 进行完美转发，将传入的参数原封不动地传递给 T 类型对象的构造函数）
        new(ptr) T(std::forward<Args>(args)...);
    }
    return ptr;
}

template<typename T>
void deleteElement(T* ptr) {
    if (ptr) {
        // 对象析构
        ptr->~T();
        // 内存回收
        HashBucket::freeMemory(reinterpret_cast<void*>(ptr), sizeof(T));
    }
}

}   // namespace MemoryPoolV1

namespace MemoryPoolV2
{
class MemoryPool {
public:
    static void* allocate(size_t size) {
        return ThreadCache::getInstance().allocate(size);
    }

    static void deallocate(void* ptr, size_t size) {
        ThreadCache::getInstance().deallocate(ptr, size);
    }
};
}   // namespace MemoryPoolV2