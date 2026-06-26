#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <thread>
#include <vector>
#include <atomic>

#include "LockedQueue.h"

struct TestFixture
{
    LockedQueue<int> queue;
    std::atomic<int> produced{ 0 };
    std::atomic<int> consumed{ 0 };

    TestFixture() : queue(0) {} // unbounded queue
};

// ============================================================
// Correctness tests
// ============================================================

TEST_CASE_METHOD(TestFixture, "LockedQueue - single-threaded push and pop", "[locked][correctness]")
{
    // TODO: push several elements, verify FIFO order via tryPop
    std::vector<int> input = { 1, 2, 3, 4, 5 };

    for (int value : input)
    {
        queue.push(value);
    }

    for (int expected : input)
    {
        auto result = queue.tryPop();
        REQUIRE(result.has_value());
        REQUIRE(result.value() == expected);
    }
}

TEST_CASE_METHOD(TestFixture, "LockedQueue - tryPop returns nullopt on empty queue", "[locked][correctness]")
{
    // TODO: verify tryPop on an empty queue returns std::nullopt
    REQUIRE(queue.tryPop() == std::nullopt);
}

TEST_CASE_METHOD(TestFixture, "LockedQueue - waitAndPop blocks until item is available", "[locked][correctness]")
{
    // TODO: producer thread pushes after a short delay, consumer blocks on waitAndPop
    std::thread producer([this]() 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        queue.push(42);
    });

    std::thread consumer([this]()
    {
        int value = queue.waitAndPop();
        REQUIRE(value == 42);
    });

    producer.join();
    consumer.join();
}

TEST_CASE_METHOD(TestFixture, "LockedQueue - size and empty reflect state correctly", "[locked][correctness]")
{
    // TODO: check size() and empty() before and after pushes/pops
    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);

    queue.push(1);

    REQUIRE(!queue.empty());
    REQUIRE(queue.size() == 1);
}

// ============================================================
// Concurrency / stress tests
// ============================================================

TEST_CASE_METHOD(TestFixture, "LockedQueue - multiple producers single consumer", "[locked][concurrency]")
{
    // TODO: N producer threads each push M items
    //       single consumer thread pops all N*M items
    //       assert no items are lost or duplicated

    const int numProducers = 4;
    const int itemsPerProducer = 1000;

    std::vector<std::thread> producers;
    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back([this, i, itemsPerProducer]()
        {
            for (int j = 0; j < itemsPerProducer; ++j)
                queue.push(i * itemsPerProducer + j); // unique value per item
        });
    }

    // Consumer
    std::thread consumer([this, numProducers, itemsPerProducer]()
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

TEST_CASE_METHOD(TestFixture, "LockedQueue - single producer multiple consumers", "[locked][concurrency]")
{
    // TODO: single producer pushes N*M items
    //       M consumer threads each pop N items
    //       assert total consumed == total produced

    const int numConsumers = 4;
    const int itemsPerConsumer = 1000;

    // Producer
    std::thread producer([this, numConsumers, itemsPerConsumer]()
    {
        for (int i = 0; i < numConsumers * itemsPerConsumer; ++i)
            queue.push(i);
    });

    std::vector<std::thread> consumers;
    for (int i = 0; i < numConsumers; ++i)
    {
        consumers.emplace_back([this, itemsPerConsumer]()
        {
            for (int j = 0; j < itemsPerConsumer; ++j)
            {
                queue.waitAndPop();
                ++consumed;
            }
        });
    }

    producer.join();
    for (auto& c : consumers) c.join();
    REQUIRE(consumed == numConsumers * itemsPerConsumer);
}

TEST_CASE_METHOD(TestFixture, "LockedQueue - multiple producers multiple consumers", "[locked][concurrency]")
{
    // TODO: N producers, N consumers, high item count
    //       assert total consumed == total produced
    constexpr int numProducers = 4;
    constexpr int numConsumers = 4;
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

    std::vector<std::thread> consumers;
    for (int i = 0; i < numConsumers; ++i)
    {
        consumers.emplace_back([this, itemsPerProducer, numProducers, numConsumers]()
        {
            for (int j = 0; j < itemsPerProducer * numProducers / numConsumers; ++j)
            {
                queue.waitAndPop();
                ++consumed;
            }
        });
    }

    for (auto& p : producers) p.join();
    for (auto& c : consumers) c.join();
    REQUIRE(consumed == numProducers * itemsPerProducer);
}

// ============================================================
// Throughput benchmarks
// ============================================================

TEST_CASE_METHOD(TestFixture, "LockedQueue - throughput benchmarks", "[locked][benchmark]")
{
    BENCHMARK("single-threaded push+pop 1000 items")
    {
        // TODO: push 1000 items then pop 1000 items, return final value to prevent optimisation
        for (int i = 0; i < 1000; ++i)
            queue.push(i);

        int lastValue = -1;
        for (int i = 0; i < 1000; ++i)
            lastValue = queue.waitAndPop();
        return lastValue;
    };

    BENCHMARK("2 producers 2 consumers throughput")
    {
        // TODO: launch 2 producer and 2 consumer threads, measure round-trip
        std::atomic<int> produced{ 0 };
        std::atomic<int> consumed{ 0 };

        const int itemsPerProducer = 1000;

        std::vector<std::thread> producers;
        for (int i = 0; i < 2; ++i)
        {
            producers.emplace_back([this, i, itemsPerProducer, &produced]()
                {
                    for (int j = 0; j < itemsPerProducer; ++j)
                    {
                        queue.push(i * itemsPerProducer + j);
                        ++produced;
                    }
                });
        }

        std::vector<std::thread> consumers;
        for (int i = 0; i < 2; ++i)
        {
            consumers.emplace_back([this, itemsPerProducer, &consumed]()
                {
                    for (int j = 0; j < itemsPerProducer; ++j)
                    {
                        queue.waitAndPop();
                        ++consumed;
                    }
                });
        }

        for (auto& p : producers) p.join();
        for (auto& c : consumers) c.join();

        return consumed.load();
    };

    BENCHMARK("4 producers 4 consumers throughput")
    {
        // TODO: launch 4 producer and 4 consumer threads, measure round-trip
        std::atomic<int> produced{ 0 };
        std::atomic<int> consumed{ 0 };

        const int itemsPerProducer = 1000;

        std::vector<std::thread> producers;
        for (int i = 0; i < 4; ++i)
        {
            producers.emplace_back([this, i, itemsPerProducer, &produced]()
                {
                    for (int j = 0; j < itemsPerProducer; ++j)
                    {
                        queue.push(i * itemsPerProducer + j);
                        ++produced;
                    }
                });
        }

        std::vector<std::thread> consumers;
        for (int i = 0; i < 4; ++i)
        {
            consumers.emplace_back([this, itemsPerProducer, &consumed]()
                {
                    for (int j = 0; j < itemsPerProducer; ++j)
                    {
                        queue.waitAndPop();
                        ++consumed;
                    }
                });
        }

        for (auto& p : producers) p.join();
        for (auto& c : consumers) c.join();

        return consumed.load();
    };
}
