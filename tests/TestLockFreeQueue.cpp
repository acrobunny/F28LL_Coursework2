#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <thread>
#include <vector>
#include <atomic>

#include "LockFreeQueue.h"

struct LockFreeFixture
{
    LockFreeQueue<int>   queue;
    std::atomic<int>     consumed{ 0 };
};

// ============================================================
// Correctness tests
// ============================================================

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - single-threaded push and pop", "[lockfree][correctness]")
{
    std::vector<int> input = { 1, 2, 3, 4, 5 };

    for (int value : input)
        queue.push(value);

    for (int expected : input)
    {
        auto result = queue.tryPop();
        REQUIRE(result.has_value());
        REQUIRE(result.value() == expected);
    }
}

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - tryPop returns nullopt on empty queue", "[lockfree][correctness]")
{
    REQUIRE(queue.tryPop() == std::nullopt);
}

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - empty reflects state correctly", "[lockfree][correctness]")
{
    REQUIRE(queue.empty());
    queue.push(1);
    REQUIRE(!queue.empty());
    queue.tryPop();
    REQUIRE(queue.empty());
}

// ============================================================
// Concurrency / stress tests
// ============================================================

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - multiple producers single consumer", "[lockfree][concurrency]")
{
    constexpr int numProducers = 4;
    constexpr int itemsPerProducer = 1000;

    std::vector<std::thread> producers;
    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back([this, i, itemsPerProducer]()
            {
                for (int j = 0; j < itemsPerProducer; ++j)
                    queue.push(i * itemsPerProducer + j);
            });
    }

    std::thread consumer([this, itemsPerProducer, numProducers]()
        {
            while (consumed < numProducers * itemsPerProducer)
            {
                if (auto val = queue.tryPop(); val.has_value())
                    ++consumed;
            }
        });

    for (auto& p : producers) p.join();
    consumer.join();

    REQUIRE(consumed == numProducers * itemsPerProducer);
}

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - single producer multiple consumers", "[lockfree][concurrency]")
{
    constexpr int numConsumers = 4;
    constexpr int itemsPerConsumer = 1000;

    std::thread producer([this, itemsPerConsumer, numConsumers]()
        {
            for (int i = 0; i < numConsumers * itemsPerConsumer; ++i)
                queue.push(i);
        });

    std::vector<std::thread> consumers;
    for (int i = 0; i < numConsumers; ++i)
    {
        consumers.emplace_back([this, itemsPerConsumer]()
            {
                int localCount = 0;
                while (localCount < itemsPerConsumer)
                {
                    if (auto val = queue.tryPop(); val.has_value())
                        ++localCount;
                }
                consumed += localCount;
            });
    }

    producer.join();
    for (auto& c : consumers) c.join();

    REQUIRE(consumed == numConsumers * itemsPerConsumer);
}

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - multiple producers multiple consumers", "[lockfree][concurrency]")
{
    constexpr int numProducers = 4;
    constexpr int numConsumers = 4;
    constexpr int itemsPerProducer = 1000;
    constexpr int total = numProducers * itemsPerProducer;
    constexpr int itemsPerConsumer = total / numConsumers;

    std::vector<std::thread> producers;
    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back([this, i, itemsPerConsumer, itemsPerProducer]()
            {
                for (int j = 0; j < itemsPerProducer; ++j)
                    queue.push(i * itemsPerProducer + j);
            });
    }

    std::vector<std::thread> consumers;
    for (int i = 0; i < numConsumers; ++i)
    {
        consumers.emplace_back([this, itemsPerConsumer]()
            {
                int localCount = 0;
                while (localCount < itemsPerConsumer)
                {
                    if (auto val = queue.tryPop(); val.has_value())
                        ++localCount;
                }
                consumed += localCount;
            });
    }

    for (auto& p : producers) p.join();
    for (auto& c : consumers) c.join();

    REQUIRE(consumed == total);
}

// ============================================================
// Throughput benchmarks
// ============================================================

namespace
{
    /**
     * @brief Runs a symmetric producer/consumer benchmark on a LockFreeQueue.
     *
     * Launches numThreads producers and numThreads consumers, each operating
     * on itemsPerProducer items. All threads are held on a start flag and
     * released simultaneously to maximise contention.
     *
     * @param queue          The queue under test.
     * @param numThreads     Number of producer threads (== number of consumer threads).
     * @param itemsPerThread Number of items each producer pushes.
     * @return Total number of items consumed.
     */
    int runThroughputBenchmark(LockFreeQueue<int>& queue, int numThreads, int itemsPerThread)
    {
        const int total = numThreads * itemsPerThread;
        std::atomic<int>  localConsumed{ 0 };
        std::atomic<bool> start{ false };

        std::vector<std::thread> producers;
        for (int i = 0; i < numThreads; ++i)
        {
            producers.emplace_back([&queue, &start, i, itemsPerThread]()
                {
                    while (!start) {}
                    for (int j = 0; j < itemsPerThread; ++j)
                        queue.push(i * itemsPerThread + j);
                });
        }

        const int itemsPerConsumer = total / numThreads;
        std::vector<std::thread> consumers;
        for (int i = 0; i < numThreads; ++i)
        {
            consumers.emplace_back([&queue, &localConsumed, &start, itemsPerConsumer]()
                {
                    while (!start) {}
                    int localCount = 0;
                    while (localCount < itemsPerConsumer)
                    {
                        if (auto val = queue.tryPop()) ++localCount;
                    }
                    localConsumed += localCount;
                });
        }

        start = true;
        for (auto& p : producers) p.join();
        for (auto& c : consumers) c.join();

        return localConsumed.load();
    }
}

TEST_CASE_METHOD(LockFreeFixture, "LockFreeQueue - throughput benchmarks", "[lockfree][benchmark]")
{
    BENCHMARK("single-threaded push+pop 1000 items")
    {
        for (int i = 0; i < 1000; ++i)
            queue.push(i);

        int lastValue = -1;
        for (int i = 0; i < 1000; ++i)
            if (auto val = queue.tryPop()) lastValue = *val;
        return lastValue;
    };

    BENCHMARK("2 producers 2 consumers throughput")
    {
        return runThroughputBenchmark(queue, 2, 1000);
    };

    BENCHMARK("4 producers 4 consumers throughput")
    {
        return runThroughputBenchmark(queue, 4, 1000);
    };

    BENCHMARK("8 producers 8 consumers throughput")
    {
        return runThroughputBenchmark(queue, 8, 1000);
    };

    BENCHMARK("16 producers 16 consumers throughput")
    {
        return runThroughputBenchmark(queue, 16, 1000);
    };
}