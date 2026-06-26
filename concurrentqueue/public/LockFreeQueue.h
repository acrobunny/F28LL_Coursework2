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
#include <memory>
#include <cstdint>
#include <cassert>

 /**
  * @brief A lock-free MPMC queue based on the Michael-Scott algorithm
  *        with tagged pointer ABA protection.
  *
  * Uses a singly-linked list with atomic head and tail TaggedPtrs.
  * A sentinel dummy node decouples producers and consumers.
  * The upper TAG_BITS bits of each pointer store a version counter that
  * increments on every CAS, preventing ABA false positives.
  *
  * @note Assumes a 64-bit address space where pointers occupy at most
  *       PTR_BITS bits. Asserted at construction time via is_lock_free().
  */
template <typename T>
class LockFreeQueue
{
private:

    /// Number of bits used for the version tag packed into the upper pointer bits.
    static constexpr uintptr_t TAG_BITS = 16;

    /// Number of bits used for the actual pointer address.
    static constexpr uintptr_t PTR_BITS = 48;

    /// Mask to extract the pointer address from a tagged value.
    static constexpr uintptr_t PTR_MASK = (uintptr_t(1) << PTR_BITS) - 1;

    /// Mask to extract the tag from a tagged value.
    static constexpr uintptr_t TAG_MASK = ~PTR_MASK;

    /// Amount to shift the tag into/out of the upper bits.
    static constexpr uintptr_t TAG_SHIFT = PTR_BITS;

    /// Maximum tag value before wrapping (2^16 - 1).
    static constexpr uintptr_t MAX_TAG = (uintptr_t(1) << TAG_BITS) - 1;

    /// Tag increment applied on every successful CAS to prevent ABA.
    static constexpr uintptr_t TAG_INCREMENT = 1;

    // ----------------------------------------------------------------

    struct Node;

    /**
     * @brief A pointer + version tag packed into a single uintptr_t.
     *
     * Fits in one 64-bit word so std::atomic<TaggedPtr> is lock-free
     * on all mainstream 64-bit platforms without needing 128-bit CAS.
     */
    struct TaggedPtr
    {
        uintptr_t value{ 0 };

        TaggedPtr() = default;

        /**
         * @brief Constructs a TaggedPtr from a raw pointer and tag value.
         * @param ptr  The node pointer (must fit within PTR_BITS).
         * @param tag  The version counter.
         */
        TaggedPtr(Node* ptr, uintptr_t tag)
            : value((reinterpret_cast<uintptr_t>(ptr)& PTR_MASK)
                | ((tag & MAX_TAG) << TAG_SHIFT))
        {
        }

        /** @brief Extracts the raw node pointer. */
        Node* ptr() const
        {
            return reinterpret_cast<Node*>(value & PTR_MASK);
        }

        /** @brief Extracts the version tag. */
        uintptr_t tag() const
        {
            return (value >> TAG_SHIFT) & MAX_TAG;
        }

        /**
         * @brief Returns a new TaggedPtr with the same pointer but an incremented tag.
         * Tag wraps around at MAX_TAG.
         */
        TaggedPtr nextTag() const
        {
            return TaggedPtr(ptr(), (tag() + TAG_INCREMENT) & MAX_TAG);
        }

        bool operator==(const TaggedPtr& other) const { return value == other.value; }
        bool operator!=(const TaggedPtr& other) const { return value != other.value; }
    };

    /**
     * @brief Internal linked list node.
     */
    struct Node
    {
        /** @brief The data stored in the node. */
        T data;

        /** @brief Tagged pointer to the next node in the list. */
        std::atomic<TaggedPtr> next;

        /** @brief Constructs a sentinel node with no data. */
        Node() : next(TaggedPtr{}) {}

        /**
         * @brief Constructs a data node.
         * @param value The value to store.
         */
        explicit Node(T value) : data(std::move(value)), next(TaggedPtr{}) {}
    };

    std::atomic<TaggedPtr> head; ///< Tagged pointer to the dummy sentinel node.
    std::atomic<TaggedPtr> tail; ///< Tagged pointer to (approximately) the last node.

public:

    /**
     * @brief Constructs the queue and initialises the sentinel dummy node.
     *
     * Asserts that TaggedPtr atomics are lock-free on this platform.
     */
    LockFreeQueue()
    {
        Node* dummy = new Node();
        TaggedPtr initial(dummy, 0);
        head.store(initial, std::memory_order_relaxed);
        tail.store(initial, std::memory_order_relaxed);

        assert(head.is_lock_free() && "TaggedPtr atomic is not lock-free on this platform");
    }

    /**
     * @brief Destructor — drains and frees all remaining nodes including the sentinel.
     */
    ~LockFreeQueue()
    {
        while (tryPop()) {}
        Node* dummy = head.load(std::memory_order_relaxed).ptr();
        delete dummy;
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    /**
     * @brief Enqueues a value at the back of the queue.
     *
     * Uses a CAS loop to link the new node atomically. If tail is lagging
     * behind the true last node, advances it before retrying (helping mechanism).
     * The tag is incremented on every successful CAS to prevent ABA.
     *
     * @param value The value to enqueue.
     */
    void push(T value)
    {
        Node* newNode = new Node(std::move(value));

        while (true)
        {
            TaggedPtr oldTail = tail.load(std::memory_order_acquire);
            TaggedPtr next = oldTail.ptr()->next.load(std::memory_order_acquire);

            // Validate tail hasn't changed under us
            if (oldTail != tail.load(std::memory_order_acquire))
                continue;

            if (next.ptr() == nullptr)
            {
                // Tail->next is null: try to link the new node
                TaggedPtr newNext(newNode, next.tag() + TAG_INCREMENT);
                if (oldTail.ptr()->next.compare_exchange_weak(
                    next, newNext,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    // Linked successfully; best-effort swing of tail
                    TaggedPtr newTail(newNode, oldTail.tag() + TAG_INCREMENT);
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
                TaggedPtr newTail(next.ptr(), oldTail.tag() + TAG_INCREMENT);
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
     * Reads the value from next (the first real node) before the CAS so that
     * the read is safe — only the CAS winner returns the value, preventing
     * double-reads across threads. The old dummy node is freed by the winner.
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

            // Validate head hasn't changed under us
            if (oldHead != head.load(std::memory_order_acquire))
                continue;

            if (oldHead.ptr() == oldTail.ptr())
            {
                if (next.ptr() == nullptr)
                    return std::nullopt; // queue is empty

                // Tail is lagging; help advance it
                TaggedPtr newTail(next.ptr(), oldTail.tag() + TAG_INCREMENT);
                tail.compare_exchange_strong(
                    oldTail, newTail,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
            else
            {
                // Read value before CAS — safe because next cannot be deleted
                // until it wins its own pop and becomes the dummy
                T value = next.ptr()->data;

                TaggedPtr newHead(next.ptr(), oldHead.tag() + TAG_INCREMENT);
                if (head.compare_exchange_weak(
                    oldHead, newHead,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    delete oldHead.ptr(); // we own the old dummy; safe to free
                    return value;
                }
            }
        }
    }

    /**
     * @brief Returns true if the queue appears empty.
     *
     * @warning Not linearisable with push/tryPop in a multi-threaded context —
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