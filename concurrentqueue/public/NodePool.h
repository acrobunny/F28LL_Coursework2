/**************************************************************************
 * @file NodePool.h
 * @brief Author: Ewan Begg
 *
 * A lock-free node pool for use with LockFreeQueue. Pre-allocates a fixed
 * block of nodes and manages them via a lock-free free list. Falls back to
 * operator new when the pool is exhausted, with correct cleanup on release.
 *
 *************************************************************************/

#ifndef NODE_POOL_H
#define NODE_POOL_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>

 /**
  * @brief A lock-free pool allocator for nodes of type T.
  *
  * Maintains a pre-allocated array of PoolNodes and a lock-free free list
  * threaded through them. When the pool is exhausted, acquire() falls back
  * to operator new and marks the node so release() can delete it directly
  * rather than returning it to the pool.
  *
  * Also owns the TaggedPtr type used by LockFreeQueue for ABA protection
  * on head and tail, keeping all pointer tagging logic in one place.
  */
template <typename T>
class NodePool
{
public:

    // ----------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------

    /// Default number of nodes to pre-allocate.
    static constexpr std::size_t DEFAULT_POOL_SIZE = 4096;

    /// CPU cache line size for padding the free list head.
    static constexpr std::size_t CACHE_LINE_SIZE = 64;

    /// Bits used for the version tag packed into the upper pointer bits.
    static constexpr uintptr_t TAG_BITS = 16;

    /// Bits available for the pointer address on a 64-bit system.
    static constexpr uintptr_t PTR_BITS = 48;

    /// Mask to extract the pointer address from a tagged value.
    static constexpr uintptr_t PTR_MASK = (uintptr_t(1) << PTR_BITS) - 1;

    /// Maximum tag value before wrapping (2^16 - 1).
    static constexpr uintptr_t MAX_TAG = (uintptr_t(1) << TAG_BITS) - 1;

    /// Bit position of the tag within the packed value.
    static constexpr uintptr_t TAG_SHIFT = PTR_BITS;

    /// Amount by which the tag increments on each successful CAS.
    static constexpr uintptr_t TAG_INCREMENT = 1;

    /**
    * @brief Forward declaration of PoolNode to avoid circular dependency with TaggedPtr.
    */
    struct PoolNode;

    /**
     * @brief A pointer + version tag packed into a single uintptr_t.
     *
     * Fits in one 64-bit word so std::atomic<TaggedPtr> is lock-free
     * on all mainstream 64-bit platforms without needing 128-bit CAS.
     * The tag increments on every successful CAS to prevent ABA races.
     * 
     * TaggedPtr is owned by NodePool to avoid duplication of pointer tagging logic.
     */
    struct TaggedPtr
    {
        uintptr_t value{ 0 };

        TaggedPtr() = default;

        /**
         * @brief Constructs a TaggedPtr from a raw pointer and tag.
         * @param ptr The node pointer (must fit within PTR_BITS).
         * @param tag The version counter.
         */
        TaggedPtr(PoolNode* ptr, uintptr_t tag)
            : value((reinterpret_cast<uintptr_t>(ptr)& PTR_MASK)
                | ((tag & MAX_TAG) << TAG_SHIFT))
        {
        }

        /** @brief Extracts the raw node pointer. */
        PoolNode* ptr() const
        {
            return reinterpret_cast<PoolNode*>(value & PTR_MASK);
        }

        /** @brief Extracts the version tag. */
        uintptr_t tag() const
        {
            return (value >> TAG_SHIFT) & MAX_TAG;
        }

        /**
         * @brief Compares two TaggedPtr instances for equality.
         * @param other The other TaggedPtr to compare with.
         * @return True if both the pointer and tag are equal, false otherwise.
         */
        bool operator==(const TaggedPtr& other) const { return value == other.value; }

        /**
         * @brief Compares two TaggedPtr instances for inequality.
         * @param other The other TaggedPtr to compare with.
         * @return True if either the pointer or tag differ, false otherwise.
         */
        bool operator!=(const TaggedPtr& other) const { return value != other.value; }
    };

    // ----------------------------------------------------------------
    // PoolNode
    // ----------------------------------------------------------------

    /**
     * @brief A node managed by the pool.
     *
     * Carries user data, a TaggedPtr next field shared by both the free
     * list and the queue linked list, and a flag indicating whether this
     * node was heap-allocated as a fallback.
     */
    struct PoolNode
    {
        /**
        * @brief The user data stored in this node.
        */
        T data;


        /**
        * @brief Next pointer — used by both the free list and the queue itself.
        * TaggedPtr here prevents ABA on the queue's head/tail CAS operations.
        */
        std::atomic<TaggedPtr> next;

        /**
        * @brief True if this node was allocated via operator new (fallback path).
        */
        bool fromHeap{ false };

        /**
        * @brief Default constructor — initializes the next pointer to an empty TaggedPtr.
        */
        PoolNode() : next(TaggedPtr{}) {}

        /**
        * @brief Constructs a PoolNode with a given value.
        * @param value The value to store in the node.
        */
        explicit PoolNode(T value) : data(std::move(value)), next(TaggedPtr{}) {}
    };

private:

    /// Pre-allocated storage block — owned by this pool.
    PoolNode* block{ nullptr };

    /// Number of nodes in the pre-allocated block.
    std::size_t poolSize{ 0 };

    /// Lock-free free list head — padded to its own cache line.
    alignas(CACHE_LINE_SIZE) std::atomic<TaggedPtr> freeList;

public:

    /**
     * @brief Constructs the pool and pre-allocates @p size nodes.
     * @param size Number of nodes to pre-allocate (default: DEFAULT_POOL_SIZE).
     */
    explicit NodePool(std::size_t size = DEFAULT_POOL_SIZE)
        : poolSize(size)
    {
        block = static_cast<PoolNode*>(::operator new(sizeof(PoolNode) * poolSize));

        for (std::size_t i = 0; i < poolSize; ++i)
        {
            PoolNode* node = new (&block[i]) PoolNode();
            node->fromHeap = false;

            TaggedPtr current = freeList.load(std::memory_order_relaxed);
            node->next.store(TaggedPtr(current.ptr(), 0), std::memory_order_relaxed);
            freeList.store(TaggedPtr(node, current.tag()), std::memory_order_relaxed);
        }

        assert(freeList.is_lock_free() && "TaggedPtr atomic is not lock-free on this platform");
    }

    /**
     * @brief Destructor — destroys all pre-allocated nodes and frees the block.
     */
    ~NodePool()
    {
        for (std::size_t i = 0; i < poolSize; ++i)
            block[i].~PoolNode();

        ::operator delete(block);
    }

    NodePool(const NodePool&) = delete;
    NodePool& operator=(const NodePool&) = delete;
    NodePool(NodePool&&) = delete;
    NodePool& operator=(NodePool&&) = delete;

    /**
     * @brief Acquires a node initialised with @p value.
     *
     * Pops from the free list via CAS. Falls back to operator new if exhausted.
     *
     * @param value The value to store in the acquired node.
     * @return A pointer to an initialised PoolNode.
     */
    PoolNode* acquire(T value)
    {
        while (true)
        {
            TaggedPtr current = freeList.load(std::memory_order_acquire);
            PoolNode* node = current.ptr();

            if (node == nullptr)
            {
                PoolNode* heapNode = new PoolNode(std::move(value));
                heapNode->fromHeap = true;
                heapNode->next.store(TaggedPtr{}, std::memory_order_relaxed);
                return heapNode;
            }

            TaggedPtr nextPtr = node->next.load(std::memory_order_acquire);
            TaggedPtr updated(nextPtr.ptr(), current.tag() + TAG_INCREMENT);

            if (freeList.compare_exchange_weak(
                current, updated,
                std::memory_order_release,
                std::memory_order_relaxed))
            {
                node->data = std::move(value);
                node->fromHeap = false;
                node->next.store(TaggedPtr{}, std::memory_order_relaxed);
                return node;
            }
        }
    }

    /**
     * @brief Acquires a default-constructed sentinel node from the pool.
     *
     * Used by LockFreeQueue to allocate the initial dummy node.
     *
     * @return A pointer to a default-constructed PoolNode.
     */
    PoolNode* acquireSentinel()
    {
        while (true)
        {
            TaggedPtr current = freeList.load(std::memory_order_acquire);
            PoolNode* node = current.ptr();

            if (node == nullptr)
            {
                PoolNode* heapNode = new PoolNode();
                heapNode->fromHeap = true;
                heapNode->next.store(TaggedPtr{}, std::memory_order_relaxed);
                return heapNode;
            }

            TaggedPtr nextPtr = node->next.load(std::memory_order_acquire);
            TaggedPtr updated(nextPtr.ptr(), current.tag() + TAG_INCREMENT);

            if (freeList.compare_exchange_weak(
                current, updated,
                std::memory_order_release,
                std::memory_order_relaxed))
            {
                node->fromHeap = false;
                node->next.store(TaggedPtr{}, std::memory_order_relaxed);
                return node;
            }
        }
    }

    /**
     * @brief Returns a node to the pool, or deletes it if it came from the heap.
     *
     * @param node The node to release. Must not be nullptr.
     */
    void release(PoolNode* node)
    {
        assert(node != nullptr);

        if (node->fromHeap)
        {
            delete node;
            return;
        }

        while (true)
        {
            TaggedPtr current = freeList.load(std::memory_order_acquire);
            node->next.store(TaggedPtr(current.ptr(), 0), std::memory_order_relaxed);

            TaggedPtr updated(node, current.tag() + TAG_INCREMENT);
            if (freeList.compare_exchange_weak(
                current, updated,
                std::memory_order_release,
                std::memory_order_relaxed))
            {
                return;
            }
        }
    }
};

#endif // !NODE_POOL_H