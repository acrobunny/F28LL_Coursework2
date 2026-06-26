/**************************************************************************
 * @file LockedQueue.h
 * @brief Author: Ewan Begg
 *
 * This class provides a simple interface for a thread-safe queue that allows
 * multiple producers and consumers to safely enqueue and dequeue elements.
 *
 *************************************************************************/

#ifndef LOCKED_QUEUE_H
#define LOCKED_QUEUE_H

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
 */
template <typename T>
class LockedQueue
{
private:

    /**
    * @brief Internal queue to hold elements.
    */
    std::queue<T>           queue;

    /**
    * @brief Mutex to protect access to the queue.
    */
    mutable std::mutex      mutex;

    /**
    * @brief Condition variable to wait for the queue to become non-empty.
    */
    std::condition_variable notEmpty;

    /**
    * @brief Condition variable to wait for the queue to have space.
    */
    std::condition_variable notFull;

    /**
    * @brief Maximum number of elements in the queue (0 = unbounded).
    */
    std::size_t             maxSize;

public:
    /**
     * @brief Constructs a LockedQueue with an optional maximum capacity.
     * @param maxSize Maximum number of elements (0 = unbounded).
     */
    explicit LockedQueue(std::size_t maxSize = 0) : maxSize(maxSize) {}

    /**
     * @brief Pushes an element onto the back of the queue.
     *        Blocks if the queue is at capacity.
     * @param value The value to enqueue.
     */
    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex);

        notFull.wait(lock, [this]() { return maxSize == 0 || queue.size() < maxSize; });

        queue.push(std::move(value));

        lock.unlock();

        notEmpty.notify_one();
    }

    /**
     * @brief Attempts to pop an element from the front without blocking.
     * @return The element if available, std::nullopt otherwise.
     */
    std::optional<T> tryPop()
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (queue.empty())
        {
            return std::nullopt;
        }

        T value = std::move(queue.front());
        queue.pop();
        notFull.notify_one();

        return value;
    }

    /**
     * @brief Blocks until an element is available, then pops it.
     * @return The dequeued element.
     */
    T waitAndPop()
    {
        std::unique_lock<std::mutex> lock(mutex);

        notEmpty.wait(lock, [this]() { return !queue.empty(); });
        T value = std::move(queue.front());
        queue.pop();
        lock.unlock();
        notFull.notify_one();

        return value;
    }

    /**
     * @brief Returns the current number of elements in the queue.
     */
    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }

    /**
     * @brief Returns true if the queue contains no elements.
     */
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
};

#endif // !LOCKED_QUEUE_H
