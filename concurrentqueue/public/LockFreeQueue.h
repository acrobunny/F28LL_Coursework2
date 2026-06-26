/**************************************************************************
 * @file LockFreeQueue.h
 * @brief Author: Ewan Begg
 *
 * This class provides a simple interface for a lock-free queue that allows
 * multiple producers and consumers to safely enqueue and dequeue elements.
 *
 *************************************************************************/

#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <cassert>

#include "NodePool.h"

 /**
  * @brief A lock-free MPMC queue based on the Michael-Scott algorithm
  *        with tagged pointer ABA protection and pooled node allocation.
  *
  * Uses a singly-linked list with atomic head and tail TaggedPtrs.
  * A sentinel dummy node decouples producers and consumers.
  * Nodes are acquired from and released to an owned NodePool, eliminating
  * per-operation allocator contention.
  *
  * TaggedPtr is sourced from NodePool to avoid duplication — all pointer
  * tagging logic lives in one place.
  *
  * @tparam T The element type stored in the queue.
  */
template <typename T>
class LockFreeQueue
{
private:

    using Pool = NodePool<T>;
    using PoolNode = typename Pool::PoolNode;
    using TaggedPtr = typename Pool::TaggedPtr;

    /// CPU cache line size — head and tail are padded onto separate lines
    /// to prevent false sharing between producers and consumers.
    static constexpr std::size_t CACHE_LINE_SIZE = Pool::CACHE_LINE_SIZE;

    /// Owned node pool — declared before head/tail so it is constructed
    /// first and destroyed last.
    Pool pool;

    alignas(CACHE_LINE_SIZE) std::atomic<TaggedPtr> head; ///< Points to the dummy sentinel node.
    alignas(CACHE_LINE_SIZE) std::atomic<TaggedPtr> tail; ///< Points to (approximately) the last node.

public:

    /**
     * @brief Constructs the queue with a given pool size.
     * @param poolSize Number of nodes to pre-allocate (default: NodePool::DEFAULT_POOL_SIZE).
     */
    explicit LockFreeQueue(std::size_t poolSize = Pool::DEFAULT_POOL_SIZE)
        : pool(poolSize)
    {
        PoolNode* dummy = pool.acquireSentinel();
        TaggedPtr initial(dummy, 0);
        head.store(initial, std::memory_order_relaxed);
        tail.store(initial, std::memory_order_relaxed);

        assert(head.is_lock_free() && "TaggedPtr atomic is not lock-free on this platform");
    }

    /**
     * @brief Destructor — drains the queue and releases all nodes back to the pool.
     */
    ~LockFreeQueue()
    {
        while (tryPop()) {}
        PoolNode* dummy = head.load(std::memory_order_relaxed).ptr();
        pool.release(dummy);
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    /**
     * @brief Enqueues a value at the back of the queue.
     *
     * Acquires a node from the pool (or heap fallback), then links it
     * atomically using the Michael-Scott CAS loop with helping mechanism.
     *
     * @param value The value to enqueue.
     */
    void push(T value)
    {
        PoolNode* newNode = pool.acquire(std::move(value));

        while (true)
        {
            TaggedPtr oldTail = tail.load(std::memory_order_acquire);
            TaggedPtr next = oldTail.ptr()->next.load(std::memory_order_acquire);

            if (next.ptr() == nullptr)
            {
                TaggedPtr newNext(newNode, next.tag() + Pool::TAG_INCREMENT);
                if (oldTail.ptr()->next.compare_exchange_weak(
                    next, newNext,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    TaggedPtr newTail(newNode, oldTail.tag() + Pool::TAG_INCREMENT);
                    tail.compare_exchange_strong(
                        oldTail, newTail,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                    return;
                }
            }
            else
            {
                // Tail is lagging; help advance it
                TaggedPtr newTail(next.ptr(), oldTail.tag() + Pool::TAG_INCREMENT);
                tail.compare_exchange_strong(
                    oldTail, newTail,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Attempts to dequeue a value without blocking.
     *
     * Reads the value from next before the CAS so the read is safe —
     * only the CAS winner returns the value. The old dummy is released
     * back to the pool by the winning thread.
     *
     * @return The front value if available, std::nullopt if the queue is empty.
     */
    std::optional<T> tryPop()
    {
        while (true)
        {
            TaggedPtr oldHead = head.load(std::memory_order_acquire);
            TaggedPtr oldTail = tail.load(std::memory_order_acquire);
            TaggedPtr next = oldHead.ptr()->next.load(std::memory_order_acquire);

            if (oldHead.ptr() == oldTail.ptr())
            {
                if (next.ptr() == nullptr)
                    return std::nullopt;

                // Tail is lagging; help advance it
                TaggedPtr newTail(next.ptr(), oldTail.tag() + Pool::TAG_INCREMENT);
                tail.compare_exchange_strong(
                    oldTail, newTail,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
            else
            {
                T value = next.ptr()->data;

                TaggedPtr newHead(next.ptr(), oldHead.tag() + Pool::TAG_INCREMENT);
                if (head.compare_exchange_weak(
                    oldHead, newHead,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    pool.release(oldHead.ptr());
                    return value;
                }
            }
        }
    }

    /**
     * @brief Returns true if the queue appears empty.
     *
     * @warning Not linearisable in a multi-threaded context —
     *          use for diagnostics and single-threaded checks only.
     */
    bool empty() const
    {
        TaggedPtr oldHead = head.load(std::memory_order_acquire);
        TaggedPtr oldTail = tail.load(std::memory_order_acquire);
        TaggedPtr next = oldHead.ptr()->next.load(std::memory_order_acquire);
        return (oldHead.ptr() == oldTail.ptr() && next.ptr() == nullptr);
    }
};

#endif // !LOCK_FREE_QUEUE_H