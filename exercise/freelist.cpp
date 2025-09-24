#include <atomic>
#include <iostream>

// 辅助模板类，不能直接实例化
template <typename N>
struct FreeListNode {
    // 线程数有限，使用 32 位的引用计数应该足够
    std::atomic<std::uint32_t> freeListRefs;
    std::atomic<N*> next;  // 不能使用 FreeListNode 本身来特例化
};

// N 必须继承自 FreeListNode 或具有相同的成员
template <typename N>
class FreeList {
public:
    FreeList() : freeListHead(nullptr) {
    }

    void add(N* node) {
        // 因为我们已经将 node 摘下并使用了，所以此时 SHOULD_BE_ON_FREELIST 位必定为 0
        // node 可能同时被其他线程持有（尽管他们不能前进，因为 node 已经从 freelist 被摘下），因为 node 可能来自 try_get()
        // release: 保证本线程前面对 node 的使用完毕
        // acquire: 防止 add_knowing_refcount_is_zero() 提前
        if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST, std::memory_order_acq_rel) == 0) {
            // 原子操作前，引用计数为 0，说明现在当前线程独享 node
            add_knowing_refcount_is_zero(node);
        }
    }

    N* try_get() {
        // TODO
    }

private:
    void add_knowing_refcount_is_zero(N* node) {
        auto head = freeListHead.load(std::memory_order_relaxed);
        while (true) {
            node->next->store(head, std::memory_order_relaxed);
            // 清除 SHOULD_BE_ON_FREELIST 位并将引用计数设为 1
            // release: 发布前面的修改
            node->freeListRefs.store(1, std::memory_order_release);
            // 引用计数已经被修改，现在 node 处于共享状态
            // 尝试将 freeListHead 设为 node
            // 因为 CAS 可能失败，所以依赖 CAS 成功的 release 消息不可靠，node->freeListRefs 的 release 不能改成 relaxed
            if (!freeListHead.compare_exchange_strong(head, node, std::memory_order_release, std::memory_order_relaxed)) {
                // freeListHead 已经被其他线程更新了，我们之前将 node 指向 head 可能是错误的，需要回退
                // 减去本线程增加的引用计数；重新设置 SHOULD_BE_ON_FREELIST（因为还没有成功把 node 挂上 freelist）
                // 不需要 CAS，因为 node 还是游离状态，可以无脑修改引用计数
                if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST - 1, std::memory_order_release) == 1) {
                    // 本线程还是独享 node
                    continue;
                }
            }
            // CAS 竞争失败
            break;
        }
    }

    static constexpr uint32_t REFS_MASK = 0x7fffffff;
    static constexpr uint32_t SHOULD_BE_ON_FREELIST = 0x80000000;

    std::atomic<N*> freeListHead;
};