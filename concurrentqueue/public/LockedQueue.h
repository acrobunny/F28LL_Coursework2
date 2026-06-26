#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <cstddef>

/**
 * @brief A thread-safe FIFO queue using mutex-based locking synchronisation.
 *
 * Supports multiple concurrent producers and consumers.
 *
 * @tparam T The element type stored in the queue.
 */
template <typename T>
class LockedQueue
{
public:
    /**
     * @brief Constructs a LockedQueue with an optional maximum capacity.
     * @param maxSize Maximum number of elements (0 = unbounded).
     */
    explicit LockedQueue(std::size_t maxSize = 0);

    /**
     * @brief Pushes an element onto the back of the queue.
     *        Blocks if the queue is at capacity.
     * @param value The value to enqueue.
     */
    void push(T value);

    /**
     * @brief Attempts to pop an element from the front without blocking.
     * @return The element if available, std::nullopt otherwise.
     */
    std::optional<T> tryPop();

    /**
     * @brief Blocks until an element is available, then pops it.
     * @return The dequeued element.
     */
    T waitAndPop();

    /**
     * @brief Returns the current number of elements in the queue.
     */
    std::size_t size() const;

    /**
     * @brief Returns true if the queue contains no elements.
     */
    bool empty() const;

private:
    std::queue<T>           queue;
    mutable std::mutex      mutex;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    std::size_t             maxSize;
};
