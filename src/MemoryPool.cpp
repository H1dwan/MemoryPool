//
// Created by 11361 on 25-3-26.
//
#include "../include/MemoryPool.h"

namespace MemoryPoolV1
{

    MemoryPool::MemoryPool(size_t block_size)
        : block_size_(block_size), slot_size_(0), first_block_(nullptr), cur_slot_(nullptr), free_list_(nullptr), last_slot_(nullptr)
    {}

    MemoryPool::~MemoryPool() {
        auto cur = first_block_;
        while (cur) {
            Slot* nxt = cur->next;
            // 等同于 free(reinterpret_cast<void*>(firstBlock_));
            // 转化为 void 指针，因为 void 类型不需要调用析构函数，只释放空间
            operator delete(reinterpret_cast<void*>(cur));
            cur = nxt;
        }
    }

    void MemoryPool::init(size_t slot_size) {
        assert(slot_size > 0);
        slot_size_ = slot_size;
        first_block_ = nullptr;
        cur_slot_ = nullptr;
        free_list_ = nullptr;
        last_slot_ = nullptr;
    }

    void* MemoryPool::allocate() {
        // 优先使用空闲链表中的内存槽
        auto slot = popFreeList();
        if (slot != nullptr) {
            return slot;
        }

        Slot* temp;
        {
            std::lock_guard<std::mutex> lock(mutex_for_block_);
            // 当前内存块已无内存槽可用，开辟一块新的内存
            if (cur_slot_ >= last_slot_) {
                allocateNewBlock();
            }
            temp = cur_slot_;
            // 让 curSlot_ 指向下一个合适的 Slot 位置
            cur_slot_ = reinterpret_cast<Slot*>(reinterpret_cast<char*>(cur_slot_) + slot_size_);
        }
        return temp;
    }

    void MemoryPool::deallocate(void* p) {
        if (p == nullptr) {
            return;
        }
        auto slot = reinterpret_cast<Slot*>(p);
        // 将内存槽放回空闲链表
        pushFreeList(slot);
    }

    void MemoryPool::allocateNewBlock() {
        // operator new 是 C++ 中用于分配原始内存的函数，它不会调用对象的构造函数
        void* new_block = operator new(block_size_);
        // 头插法将新分配的内存块插入到内存块链表的头部
        reinterpret_cast<Slot*>(new_block)->next = first_block_;
        first_block_ = reinterpret_cast<Slot*>(new_block);

        // 跳过Slot中存储next指针的空间
        char* body = reinterpret_cast<char*>(new_block) + sizeof(Slot*);
        // 计算对齐slot大小所需的字节数
        size_t padding = padPointer(body, slot_size_);
        // 第一个可用的内存槽的地址
        cur_slot_ = reinterpret_cast<Slot*>(body + padding);

        // 超过该标记位置，则说明该内存块已无内存槽可用，需向系统申请新的内存块
        last_slot_ = reinterpret_cast<Slot*>(reinterpret_cast<size_t>(new_block) + block_size_ - slot_size_ + 1);
        free_list_ = nullptr;
    }

    size_t MemoryPool::padPointer(const char* p, size_t align) {
        // align 是槽大小
        return (align - reinterpret_cast<size_t>(p)) % align;
    }

    // TODO: 无锁队列
    bool MemoryPool::pushFreeList(Slot* slot) {
        while (true) {
            // 获取当前头节点
            auto old_head = free_list_.load(std::memory_order_relaxed);
            // 将新节点的 next 指向当前头节点（头插法）
            slot->next.store(old_head, std::memory_order_relaxed);
            // 尝试将新节点设置为头节点
            if (free_list_.compare_exchange_weak(old_head, slot,
                                                 std::memory_order_release, std::memory_order_relaxed)) {
                return true;
            }
            // 失败：说明另一个线程可能已经修改了 freeList_
            // CAS 失败则重试直到插入成功为止
        }
    }

    Slot* MemoryPool::popFreeList() {
        while (true) {
            auto old_head = free_list_.load(std::memory_order_acquire);
            // 队列为空
            if (old_head == nullptr) {
                return nullptr;
            }

            // 在访问 newHead 之前再次验证 oldHead 的有效性
            Slot* new_head = nullptr;
            try {
                new_head = old_head->next.load(std::memory_order_relaxed);
            } catch (...) {
                // 如果返回失败，则continue重新尝试申请内存
                continue;
            }

            // 尝试更新头结点
            if (free_list_.compare_exchange_weak(old_head, new_head,
                                                 std::memory_order_acquire, std::memory_order_relaxed)) {
                return old_head;
            }
            // 失败：说明另一个线程可能已经修改了 freeList_
            // CAS 失败则重试
        }
    }

    void HashBucket::initMemoryPool() {
        for (size_t i = 0; i < MEMORY_POOL_NUM; i++) {
            getMemoryPool(i).init((i+1) * SLOT_BASE_SIZE);
        }
    }


    MemoryPool& HashBucket::getMemoryPool(size_t index) {
        static MemoryPool memory_pool[MEMORY_POOL_NUM];
        return memory_pool[index];
    }
}