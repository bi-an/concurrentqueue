#include <atomic>
#include <iostream>

// 辅助模板类，不能直接实例化
template <typename N>
struct FreeListNode {
    // 线程数有限，使用 32 位的引用计数应该足够
    std::atomic<std::uint32_t> freeListRefs;
    std::atomic<N *> next;  // 不能使用 FreeListNode 本身来特例化
};

// N 必须继承自 FreeListNode 或具有相同的成员
template <typename N>
class FreeList {
public:
    FreeList() : freeListHead(nullptr) {
    }

    // ABA 问题：CAS(head, head->next)
    // 获取 head->next 之后、CAS 执行之前，可能 head 已经被摘下兜了一圈又回来了
    // 此时 CAS 还是会确认 head 符合预期，继续尝试，如图：
    //
    //   1 → 2 → 3 → 4 → 5
    //   ↑
    //  head
    //
    //  4 → 5 → 1 → 3 → 2
    //          ↑
    //         head'
    //
    // 显然，此时 CAS 尝试摘取 head' 是错误的。

    // 使用引用计数防范 ABA 问题
    // 1. 所有线程 try_get() 的时候，都要先增加 head 的引用计数。
    // 2. 当 add() 的时候，发现引用计数不为 0 ，则放弃：
    //    只设置一个 SHOULD_BE_ON_LIST 位，让最后一个持有引用计数的线程帮助自己完成 add()。
    //    注：引用计数之所以不为 0，是因为其他线程与本线程竞争 try_get() 的时候失败，且没有来得及回退。
    // 当前线程主动放弃执行 add()，以等待所有前序线程完成回退操作。
    // 这样可以避免 ABA 问题中，旧的 A 状态在其他线程尚未退出前阶段时提前恢复，确保所有线程都已退出旧状态后再恢复 A。
    // 显然，这种等待可以自旋或加锁，但是效率不高。所以作者选择设置一个 SHOULD_BE_ON_LIST 位，转交 add() 任务
    // 给其他回退中的线程。

    // memory_order
    // 涉及三个变量：freeListHead, freeListHead->refs, freeListHead->next
    // 需要同步这三个变量的读写序

    void add(N *node) {
        // 因为我们已经将 node 摘下并使用了，所以此时 SHOULD_BE_ON_FREELIST 位必定为  0 ， node
        // 可能同时被其他线程持有（尽管他们不能前进，因为 node 已经从 freelist 被摘下），因为 node 可能来自 try_get().
        // release: 保证本线程前面对 node 的使用完毕 acquire: 防止 add_knowing_refcount_is_zero() 提前
        if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST, std::memory_order_acq_rel) == 0) {
            // 原子操作前，引用计数为 0，说明现在当前线程独享 node
            add_knowing_refcount_is_zero(node);
        }
    }

    // TODO: memory_order
    N *try_get() {
        // 获取 head 之后，需要对
        auto head = freeListHead.load();

        // 只要 head != nullptr ，说明还有节点可以摘取
        while (head) {
            auto refs = head->freeListRefs.load();
            auto prevHead = head;
            // 先将 head 的引用计数加 1 。
            // refs=0 说明节点已经被摘下，且其他线程都放弃摘取。
            // 不能无脑使用 fetch_add 增加，否则该摘下的节点 add() 回来的时候，可能永远无法等待到 refs=0
            // ，导致永远无法成功 add()
            if ((refs & REFS_MASK) == 0 || !head->freeListRefs.compare_exchange_strong(refs, refs + 1)) {
                // 更新 head 继续尝试
                head = freeListHead.load();
                // refs 已经被 CAS 更新，无需再次更新
                continue;
            }

            // 成功增加引用计数（可以防止 ABA），所以现在可以安心地 CAS(head, head->next)
            auto next = head->next.load(std::memory_order_relaxed);
            if (freeListHead.compare_exchange_strong(head, next)) {
                // 成功摘下节点
                // TODO: assert(refs)
                // 减去当前线程增加的引用计数和 freelist 持有的引用计数
                // release: 需要将前面的所有操作都完成且发布到其他线程
                head->freeListRefs.fetch_sub(2, std::memory_order_release);
                // TODO: 没有将 head->next 设为 nullptr
                return head;
            }

            //  CAS 失败，被其他线程抢先摘取了
            // 回退当前线程引入的引用计数（要使用暂存的 prevHead 来回退，因为上一步的 CAS 必定把 head 更新了）
            refs = prevHead->freeListRefs.fetch_sub(1);
            if (refs == SHOULD_BE_ON_FREELIST + 1) {
                // 引用计数降为 0，不会有其他线程有机会增加引用计数了
                // 当前线程退出前，将会是唯一一个引用该节点的线程
                // 帮忙把 add() 完成
                add_knowing_refcount_is_zero(prevHead);
            }
        }

        return nullptr;
    }

private:
    void add_knowing_refcount_is_zero(N *node) {
        auto head = freeListHead.load(std::memory_order_relaxed);
        while (true) {
            node->next->store(head, std::memory_order_relaxed);
            // 清除 SHOULD_BE_ON_FREELIST 位并将引用计数设为 1 (freelist
            // 本身持有一个对节点的引用计数)
            // release: 发布前面的修改
            node->freeListRefs.store(1, std::memory_order_release);
            // 引用计数已经被修改，现在 node 处于共享状态
            // 尝试将 freeListHead 设为 node
            // 因为 CAS 可能失败，所以依赖 CAS 成功的 release
            // 消息不可靠，node->freeListRefs 的 release 不能改成 relaxed
            if (!freeListHead.compare_exchange_strong(head, node, std::memory_order_release,
                                                      std::memory_order_relaxed)) {
                // freeListHead 已经被其他线程更新了，我们之前将 node 指向 head
                // 可能是错误的，需要回退 减去本线程增加的引用计数；重新设置
                // SHOULD_BE_ON_FREELIST（因为还没有成功把 node 挂上 freelist）
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

    std::atomic<N *> freeListHead;
};