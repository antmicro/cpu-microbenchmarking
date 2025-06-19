#include <benchmark/benchmark.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

extern "C"
{
    typedef struct CPUState CPUState;
#include "exports.h"
#include "include/cpu-microbenchmark.h"
#include "include/debug.h"
}

enum TlibAction {
    INITIALIZE,
    START,
    RESET,
    QUIT,
};

class TlibThread {
public:
    TlibThread() {
        dbg_printf("Creating new TlibThread\n");
        tlib = std::thread([this]() {this->run();});
    }

    ~TlibThread() {
        dbg_printf("Destructing TlibThread\n");
        join();
    }

    void init(SharedMemory shared) {
        dbg_printf("Initializing TlibThread...\n");
        // Set up tlib shared state.
        {
            std::unique_lock lock(this->tlib_mutex);
            this->shared = shared;
        }
        send_action(INITIALIZE);
        dbg_printf("Successfully initialized TlibThread!\n");
    }

    void start(Scenario* scenario) {
        dbg_printf("Starting TlibThread...\n");
        // Set up tlib shared state.
        {
            std::unique_lock lock(this->tlib_mutex);
            this->scenario = scenario;
        }
        send_action(START, false);
        dbg_printf("Successfully started TlibThread!\n");
    }

    void reset() {
        dbg_printf("Resetting TlibThread...\n");
        send_action(RESET);
        dbg_printf("Successfully reset TlibThread!\n");
    }

    void wait_for_action() {
        dbg_printf("Waiting for action..\n");
        // Wait until signaled that execution is finished.
        std::unique_lock lock(this->tlib_mutex);
        cv.wait(lock, [this]{ return this->action_complete; });

        dbg_printf("Action is finished\n");

        // Reset flag.
        action_complete = false;        
    }

    void join() {
        send_action(QUIT);

        // Wait for thread to finish (should already be finished).
        tlib.join();
    }

private:
    std::thread tlib;
    std::mutex tlib_mutex;
    std::condition_variable cv;
    TlibAction next_action = QUIT;
    bool next_action_ready = false;
    bool action_complete = false;
    SharedMemory shared;
    Scenario* scenario;

    void send_action(TlibAction action, bool wait_for_finish = true) {
        // Set up tlib state for action receiving.
        {
            dbg_printf("sending action..\n");
            std::unique_lock lock(this->tlib_mutex);
            this->action_complete = false;
            this->next_action = action;
            this->next_action_ready = true;
        }
        dbg_printf("Waking tlib..\n");
        // Wake tlib thread.
        cv.notify_one();
        if (wait_for_finish) {
            // Wait for action to complete.
            wait_for_action();
        }
    }

    void run() {
        while(true) {
            dbg_printf("Action loop!\n");
            // Wait until signaled that there's an action to do.
            std::unique_lock action_lock(tlib_mutex);
            cv.wait(action_lock, [this]{ return next_action_ready; });
            dbg_printf("Received action! %i\n", next_action);

            switch(next_action) {
            case INITIALIZE:
                dbg_printf("INITIALIZE\n");
                // Initialize tlib.
                cpu_microbenchmark_setup(shared, false);
                break;
            case START: {
                dbg_printf("START\n");
                // Start tlib.
                cpu_microbenchmark_execute(scenario);
                break;
            }
            case RESET:
                dbg_printf("RESET\n");
                // Reset tlib state.
                cpu_microbenchmark_reset(shared);
                break;
            case QUIT:
                dbg_printf("QUIT\n");
                goto quit;
            default:
                printf("unknown action 0x%x\n", next_action);
                exit(1);
            }

            next_action_ready = false;
            action_complete = true;

            dbg_printf("Action done!\n");

            // Notify main thread that action is done.
            action_lock.unlock();
            cv.notify_one();

            dbg_printf("Main thread notified!\n");
        }
        quit:
        action_complete = true;
        dbg_printf("quit loop\n");
        // Notify main thread that loop has exited.
        cv.notify_one();
    }
};

static TlibThread* threads;
static SharedMemory shared;

static void SetupTlib(const benchmark::State &state)
{
    dbg_printf("SetupTlib\n");
    shared = setup_tlib_shared();

    int64_t tlib_thread_count = state.range(0);

    threads = new TlibThread[tlib_thread_count];
    for(int i = 0; i < tlib_thread_count; i++) {
        threads[i].init(shared);
    }
}

static void DisposeTlib(const benchmark::State &state)
{
    dbg_printf("DisposeTlib\n");
    tlib_dispose_shared(shared);
    delete [] threads;
}

// Do n batches of the benchmarks.
static int repetitions = 40;
// How many time units to run each batch for.
static int batch_min_time = 10;
// Which time unit to measure benchmarks in.
static benchmark::TimeUnit time_unit = benchmark::kSecond;
// How many threads to test with (at a maximum, it benchmarks with a range from 1..n).
static int max_threads = 8;

static uint64_t counterGuestAddress = 0x40f10000;
// Must be divisible by every number of threads the benchmark runs with.
// Easiest way to calculate a value is to multiply the prime factors,
// e.g. 2 * 3 * 5 * 7 for 1-8 threads.
static uint64_t countUpTo = 0xD20000;
static Scenario contended_lrsc = (Scenario) {
    memoryValues: { { guestAddress: counterGuestAddress, value: 0 } },
    registers: { 
        { id: 10 /* a0 */, value: counterGuestAddress },
        // The number of iterations per core must be calculated dynamically using thread count.
        { id: 11 /* a1 */, value: 0 },
        { id: 12 /* a2 */, value: 1 },
    },
    blockSize: 10000,
    code: {
        0x1005372f, // lr.d	a4,(a0)
        0x00c70733, // add	a4,a4,a2
        0x18e5372f, // sc.d	a4,a4,(a0)
        0xfe071ae3, // bnez	a4,0 <repeat>	c: R_RISCV_BRANCH	repeat
        0xfff58593, // add	a1,a1,-1
        0xfe0596e3, // bnez	a1,0 <repeat>	14: R_RISCV_BRANCH	repeat
    },
};

static void ContendedLRSC(benchmark::State &state)
{
    int64_t tlib_thread_count = state.range(0);
    uint64_t* counterHostAddress = (uint64_t*)tlib_guest_offset_to_host_ptr(counterGuestAddress);

    // Need to divide the work evenly, to make performance comparable between thread counts.
    RegisterInit* iterationCountPerCore = &contended_lrsc.registers[1];
    iterationCountPerCore->value = countUpTo / tlib_thread_count;

    for (auto _ : state)
    {
        for(int i = 0; i < tlib_thread_count; i++) {
            threads[i].start(&contended_lrsc);
        }
        for(int i = 0; i < tlib_thread_count; i++) {
            threads[i].wait_for_action();
        }
        state.PauseTiming();

        double progress = countUpTo != 0 ? (100 * (*counterHostAddress) / countUpTo) : 0;
        dbg_printf("\e[92m[%s]\e[0m Final LR/SC counter is at 0x%llx [%d%%]\n", __func__, *counterHostAddress, progress);
        assert(progress == 100);

        // Reset tlib state.
        tlib_reset_shared(shared);
        for(int i = 0; i < tlib_thread_count; i++) {
            threads[i].reset();
        }
        state.ResumeTiming();
    }
}
BENCHMARK(ContendedLRSC)->
Setup(SetupTlib)->
Teardown(DisposeTlib)->
ReportAggregatesOnly(false)-> // Report data for every iteration.
MeasureProcessCPUTime()-> // Necessary since it needs to measure all threads we spawn.
Unit(time_unit)->
Repetitions(repetitions)-> // How many batches.
MinTime(batch_min_time)-> // How long each batch should take. (can be replaced with set # of iterations).
DenseRange(1, max_threads)-> // Number of concurrent tlib threads.
UseRealTime(); // Use wall time rather than total CPU time to determine # of iterations.

static Scenario uncontended_lrsc = (Scenario) {
    registers: { 
        { id: 10 /* a0 */, value: 0 }, // Must be set before running.
        // The number of iterations per core must be calculated dynamically using thread count.
        { id: 11 /* a1 */, value: 0 }, // Must be set before running.
        { id: 12 /* a2 */, value: 1 },
    },
    blockSize: 10000,
    code: {
        0x1005372f, // lr.d	a4,(a0)
        0x00c70733, // add	a4,a4,a2
        0x18e5372f, // sc.d	a4,a4,(a0)
        0xfe071ae3, // bnez	a4,0 <repeat>	c: R_RISCV_BRANCH	repeat
        0xfff58593, // add	a1,a1,-1
        0xfe0596e3, // bnez	a1,0 <repeat>	14: R_RISCV_BRANCH	repeat
    },
};

static void UncontendedLRSC(benchmark::State &state)
{
    int64_t tlib_thread_count = state.range(0);

    // Initialize individual counters for each core.
    uint64_t counterGuestAddressStart = 0x40f10000;
    for (int i = 0; i < tlib_thread_count; i++)
    {
        MemoryInit* coreCounter = &uncontended_lrsc.memoryValues[i];
        coreCounter->guestAddress = counterGuestAddressStart + i * sizeof(uint64_t);
        coreCounter->value = 0;
    }

    // Need to divide the work evenly, to make performance comparable between thread counts.
    RegisterInit* iterationCountPerCore = &uncontended_lrsc.registers[1];
    uint64_t countPerCoreValue = countUpTo / tlib_thread_count;
    iterationCountPerCore->value = countPerCoreValue;

    // Create unique copy of scenario per core.
    Scenario scenarios[tlib_thread_count];
    for (int i = 0; i < tlib_thread_count; i++)
    {
        scenarios[i] = uncontended_lrsc;
    }

    for (auto _ : state)
    {
        for(int i = 0; i < tlib_thread_count; i++) {
            Scenario* scenario = &scenarios[i];
            // Set unique shared memory counter per core.
            scenario->registers[0].value = scenario->memoryValues[i].guestAddress;
            // Start tlib.
            threads[i].start(scenario);
        }
        for(int i = 0; i < tlib_thread_count; i++) {
            threads[i].wait_for_action();

            if (dbg) {
                Scenario* scenario = &scenarios[i];
                uint64_t counterGuestAddress = scenario->memoryValues[i].guestAddress;
                uint64_t* counterHostAddress = (uint64_t*)tlib_guest_offset_to_host_ptr(counterGuestAddress);
                uint64_t counter = *counterHostAddress;
                double progress = countPerCoreValue != 0 ? (100 * counter / countPerCoreValue) : 0;
                dbg_printf("\e[92m[%s]\e[0m Final LR/SC counter of core %d is at 0x%llx [%.0lf%%]\n", __func__, i, counter, progress);
                assert(progress == 100);
            }
        }
        state.PauseTiming();

        // Reset tlib state.
        tlib_reset_shared(shared);
        for(int i = 0; i < tlib_thread_count; i++) {
            threads[i].reset();
        }
        state.ResumeTiming();
    }
}
BENCHMARK(UncontendedLRSC)->
Setup(SetupTlib)->
Teardown(DisposeTlib)->
ReportAggregatesOnly(false)-> // Report data for every iteration.
MeasureProcessCPUTime()-> // Necessary since it needs to measure all threads we spawn.
Unit(time_unit)->
Repetitions(repetitions)-> // How many batches.
MinTime(batch_min_time)-> // How long each batch should take. (can be replaced with set # of iterations).
DenseRange(1, max_threads)-> // Number of concurrent tlib threads.
UseRealTime(); // Use wall time rather than total CPU time to determine # of iterations.

BENCHMARK_MAIN();
