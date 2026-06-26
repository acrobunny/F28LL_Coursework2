#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <thread>
#include <vector>
#include <atomic>

#include "LockedQueue.h"

// ============================================================
// Correctness tests
// ============================================================

TEST_CASE("LockedQueue - single-threaded push and pop", "[locked][correctness]")
{
    // TODO: push several elements, verify FIFO order via tryPop
}

TEST_CASE("LockedQueue - tryPop returns nullopt on empty queue", "[locked][correctness]")
{
    // TODO: verify tryPop on an empty queue returns std::nullopt
}

TEST_CASE("LockedQueue - waitAndPop blocks until item is available", "[locked][correctness]")
{
    // TODO: producer thread pushes after a short delay, consumer blocks on waitAndPop
}

TEST_CASE("LockedQueue - size and empty reflect state correctly", "[locked][correctness]")
{
    // TODO: check size() and empty() before and after pushes/pops
}

// ============================================================
// Concurrency / stress tests
// ============================================================

TEST_CASE("LockedQueue - multiple producers single consumer", "[locked][concurrency]")
{
    // TODO: N producer threads each push M items
    //       single consumer thread pops all N*M items
    //       assert no items are lost or duplicated
}

TEST_CASE("LockedQueue - single producer multiple consumers", "[locked][concurrency]")
{
    // TODO: single producer pushes N*M items
    //       M consumer threads each pop N items
    //       assert total consumed == total produced
}

TEST_CASE("LockedQueue - multiple producers multiple consumers", "[locked][concurrency]")
{
    // TODO: N producers, N consumers, high item count
    //       assert total consumed == total produced
}

// ============================================================
// Throughput benchmarks
// ============================================================

TEST_CASE("LockedQueue - throughput benchmarks", "[locked][benchmark]")
{
    BENCHMARK("single-threaded push+pop 1000 items")
    {
        // TODO: push 1000 items then pop 1000 items, return final value to prevent optimisation
    };

    BENCHMARK("2 producers 2 consumers throughput")
    {
        // TODO: launch 2 producer and 2 consumer threads, measure round-trip
    };

    BENCHMARK("4 producers 4 consumers throughput")
    {
        // TODO: launch 4 producer and 4 consumer threads, measure round-trip
    };
}
