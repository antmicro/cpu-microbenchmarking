#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint64_t offset;
	uint64_t size;
	void *pointer;
	void *real_pointer;
} mapping;

#define MAX_MAPPINGS 128

typedef struct SharedMemory {
    void* atomicState;
    void* storeTable;
    mapping* mappings;
} SharedMemory;
  
#define MAX_INSTRUCTIONS 128
#define MAX_MEMORY_VALUES 8
#define MAX_REGISTERS 32

typedef struct MemoryInit {
	uint64_t guestAddress;
	uint64_t value;
} MemoryInit;

typedef struct RegisterInit {
	int id;
	uint64_t value;
} RegisterInit;

// A setup of code to run in tlib during a benchmark.
typedef struct Scenario {
	MemoryInit memoryValues[MAX_MEMORY_VALUES];

	RegisterInit registers[MAX_REGISTERS];

	uint32_t blockSize;
	uint32_t code[MAX_INSTRUCTIONS];
} Scenario;

SharedMemory setup_tlib_shared();
void cpu_microbenchmark_setup(SharedMemory sharedMemory, bool hook_present);

void tlib_dispose_shared(SharedMemory shared);
void tlib_reset_shared(SharedMemory shared);
void cpu_microbenchmark_reset(SharedMemory shared);

int cpu_microbenchmark_execute(Scenario* scenario);

void deregister_mappings();
void *tlib_guest_offset_to_host_ptr(uint64_t offset);
