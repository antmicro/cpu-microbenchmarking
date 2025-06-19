/*
 *
 *  (c) 2019-2022 Antmicro <www.antmicro.com>
 *  proprietary / confidential (aka "do not release")
 *
 */

#include "include/cpu-microbenchmark.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <libgen.h>

typedef struct CPUState CPUState;
#include <exports.h>
#include <callbacks.h>

#include "include/debug.h"

// #define DEBUG_INFO

#define STORE_TABLE_BITS 36  // 64 - 36 = 28 bits, 2^28 bytes = 256 MiB large store table

extern __thread void *cpu;

__thread int no_debug = 1;
__thread uint64_t limit = 0;
__thread int do_abort = 0;

__thread mapping *mappings;

void tlib_on_interrupt_begin(uint64_t exception_index) {
  dbg_printf("%s\n", __func__);
}

void tlib_on_interrupt_end(uint64_t exception_index) {
  dbg_printf("%s\n", __func__);
}

char * decode_level(int level) {
switch(level) {
  case -1: return "NOISY";
  case 0: return "DEBUG";
  case 1: return "INFO";
  case 2: return "WARNING";
  case 3: return "ERROR";
  default: return "(unknown_level)";
}
}

void tlib_log(int loglevel, char *s) {
  if (!dbg && loglevel < 3) return;
    dbg_printf("\e[93m[LOG/%s]\e[0m %s\n", decode_level(loglevel), s);
}

int register_mapping(uint64_t offset, uint64_t size, void *ptr) {
  static int init = 0;
  if (!init) {
    init = 1;
    for (int i = 0; i < MAX_MAPPINGS; i++) {
      mappings[i].pointer = NULL;
    }
  }
  for (int i = 0; i < MAX_MAPPINGS; i++) {
    if (mappings[i].pointer == NULL) {
      mappings[i].offset = offset;
      mappings[i].size = size;
      if (ptr == NULL) {
        mappings[i].pointer = malloc(size + 0x100000);
	      if (mappings[i].pointer == NULL) break;
        mappings[i].real_pointer = mappings[i].pointer;
      } else {
        mappings[i].pointer = ptr;
        mappings[i].real_pointer = NULL;
      }
      while (((uint64_t)mappings[i].pointer) % 0x10000 != 0) mappings[i].pointer++;
      memset(mappings[i].pointer, 0, size);
      return 1;
    }
  }
  tlib_abort("Mapping was not registered.");
  return 0;
}

void deregister_mappings() {
	for (int i = 0; i < MAX_MAPPINGS; i++) if (mappings[i].real_pointer != NULL) free(mappings[i].real_pointer);
  memset(mappings, 0, sizeof(mapping) * MAX_MAPPINGS);
}

int find_mapping(uint64_t offset) {
	for (int i = 0; i < MAX_MAPPINGS; i++) {
		if ((mappings[i].offset <= offset) && ((mappings[i].offset + mappings[i].size) > offset)) return i;
	}
	return -1;
}

int find_mapping_from_host_ptr(void *ptr) {
	uint64_t offset = (uint64_t)ptr;
	for (int i = 0; i < MAX_MAPPINGS; i++) {
		if (((uint64_t)mappings[i].pointer <= offset) && (((uint64_t)mappings[i].pointer + mappings[i].size) > offset)) return i;
	}
	return -1;
}

uint64_t tlib_host_ptr_to_guest_offset(void* ptr) {
	int mapping = find_mapping_from_host_ptr(ptr);
        if (mapping == -1) {
                dbg_printf("ERROR: mapping at host offset 0x%p not found.\n", ptr);
                tlib_abort("Mapping not found!");
                return 0;
        }
	return (uint64_t)ptr - (uint64_t)mappings[mapping].pointer;
}

void *tlib_guest_offset_to_host_ptr(uint64_t offset) {
  int mapping = find_mapping(offset);
	if (mapping == -1) {
		dbg_printf("ERROR: mapping at offset 0x%08lX not found.\n", offset);
		tlib_abort("Mapping not found!");
		return NULL;
	}
	void* result = (void*)((uint64_t)mappings[mapping].pointer - mappings[mapping].offset + offset);
	return result;
}

void tlib_abort(char *msg) {
	dbg_printf("\e[91m[ABORT]\e[0m with msg: %s\n", msg);
	do_abort = 1;
  exit(1);
}

extern uint64_t *get_reg_pointer_64(int reg_number);

void print_regs() {
  // RISCV PC register
  int pc_no = 32;
	dbg_printf("\e[92m[REGS]\e[0m @ 0x%08lX -> ", tlib_get_register_value(pc_no));
  dbg_printf("a0: 0x%08lX | ", tlib_get_register_value(10));
  dbg_printf("a1: 0x%08lX | ", tlib_get_register_value(11));
  dbg_printf("a2: 0x%08lX | ", tlib_get_register_value(12));
	dbg_printf("\n");
}

void W32(uint32_t offset, uint32_t val) {
         uint32_t *v = (uint32_t*)tlib_guest_offset_to_host_ptr(offset);
         if (v != NULL) *v = val;
}

uint32_t R32(uint32_t offset) {
        uint32_t *v = (uint32_t*)tlib_guest_offset_to_host_ptr(offset);
        return (v == NULL) ? 0 : *v;
}

void tlib_on_block_translation(uint64_t start, uint32_t size, uint32_t flags) {
#ifdef DEBUG_INFO
  dbg_printf("\e[93m[TRANSLATE]\e[0m We are translating block @ 0x%08lX, size = %d byte%s\n", start, size, size == 1 ? "" : "s");
#endif
}

__thread uint64_t insns_counter = 0;
__thread uint64_t global_counter = 0;
__thread uint64_t first_time = 0;
__thread uint64_t last_time = 0;

__thread float mips = 0.0;

uint64_t get_seconds() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void tlib_on_block_finished(uint64_t pc, uint32_t executed_instructions) {
  insns_counter += executed_instructions;
  uint64_t time_in_secs = get_seconds();
  if (last_time == 0) {
    last_time = time_in_secs;
    first_time = last_time;
  }

  if (time_in_secs != last_time) {
    last_time = time_in_secs;
    float delta =  1.0 * insns_counter / (1.0 * tlib_get_total_executed_instructions());
    mips = insns_counter/1000000/(1.0*(last_time - first_time));
    dbg_printf("%.2f MIPS | %.2f MIPS [sec=%d] [count=%lld vs %lld -- %.2f] [main loop counter = %lld]\n",insns_counter/1000000/(1.0*(last_time - first_time)), global_counter/1000000/(1.0*(last_time - first_time)), last_time-first_time, insns_counter, tlib_get_total_executed_instructions(), delta, global_counter);
  }

#ifdef DEBUG_INFO
	dbg_printf("\e[93m[EXEC]\e[0m We have finished executing block 0x%08lX {\e[92m%s\e[0m}, icount = %d instruction%s executed [total=%lld].\n", pc, get_symbol_at(symbols, pc, 1), executed_instructions, executed_instructions == 1 ? "" : "s",insns_counter);
#endif
}

__thread uint64_t bbegin_counter = 0;

uint32_t tlib_on_block_begin(uint64_t address, uint32_t size) {
  bbegin_counter += size;
	if (address == 0) {
		tlib_abort("We are at 0 and should not be.");
		return 0;
	}
	return 1;
}

void WSTRING(uint64_t addr, char *s) {
	char *nm = (char*)tlib_guest_offset_to_host_ptr(addr);
	strcpy(nm, s);
}

__thread char uart[2048];
__thread int uart_counter = 0;
void tlib_write_double_word_2(uint64_t addr, uint64_t val) {
  if (uart_counter == 0) uart[0]= 0;
  if (!no_debug)
  	dbg_printf("%s(%08X, %08X)\n",__func__, addr & 0xFFFFFFFF, val);
  if (addr == 0xf0001000) {
    if (val == '\r') return;
    uart_counter++;
    uart[uart_counter-1] = val & 0xff;
    if ((val & 0xff) == '\n') {
       uart[uart_counter-1] = 0;
       dbg_printf("uart0: %s\n", uart);
       if (limit == 0) if (strncmp(uart, 
              "[    0.000000] rcu: Adjusting geometry for rcu_fanout_leaf=16, nr_cpu_ids=", 
              strlen("[    0.000000] rcu: Adjusting geometry for rcu_fanout_leaf=16, nr_cpu_ids=")) == 0) {
          do_abort = 1;
       }
       uart_counter = 0;
    }
  }
}

void tlib_write_word_2(uint64_t addr, uint64_t val) {
  if (!no_debug)
	dbg_printf("%s(%08X, %08X)\n",__func__, addr & 0xFFFFFFFF, val);
}

void tlib_write_byte_2(uint64_t addr, uint64_t val) {
  if (!no_debug)
	dbg_printf("%s(%08X, %08X)\n",__func__, addr & 0xFFFFFFFF, val);
}

uint64_t tlib_read_byte_2(uint64_t addr) {
  if (!no_debug)
	dbg_printf("%s(%08X)\n",__func__, addr & 0xFFFFFFFF);
        return 0;
}

uint64_t tlib_read_word_2(uint64_t addr) {
  if (!no_debug)
	dbg_printf("%s(%08X)\n",__func__, addr & 0xFFFFFFFF);
        return 0;
}

uint64_t tlib_read_double_word_2(uint64_t addr) {
  if (!no_debug)
	dbg_printf("%s(%08X)\n",__func__, addr & 0xFFFFFFFF);
        return 0;
}

void load_binary(char *fname, void *addr) {
  char buffer[1024];
  FILE *f = fopen(fname, "r");
  if (f == NULL) {
    fprintf(stderr, "File %s not found!\n", fname);
    return;
  } else {
    dbg_printf("Loading file %s", fname);
  }
  int count = 0;
  do {
     count = fread(buffer, 1, 1024, f);
     dbg_printf("...", count);
     memcpy((void*)addr, (void*)buffer, count);
     addr = (void*)((uint64_t)addr + count);
  } while (count > 0);
  dbg_printf("\n");
}

extern void tlib_allow_feature(uint32_t features);
#define RISCV_ADDITIONAL_FEATURE_OFFSET     26
#define RISCV_FEATURE_ZICSR 4
#define RISCV_FEATURE_ZIFENCEI 5
extern void tlib_allow_additional_feature(uint32_t feature_encoding);

static __thread char* target_triple = "riscv64";
static __thread char* cpu_model = "rv64gc";

#ifdef RESIZABLE_STORE_TABLE
int32_t tlib_store_table_init(uintptr_t store_table_ptr, uint8_t store_table_bits) __attribute__((weak)); 
#else
int32_t tlib_store_table_init(uintptr_t store_table_ptr) __attribute__((weak)); 
#endif

#define STORE_TABLE_ALIGNMENT (1 << 28)
#define STORE_TABLE_SIZE_BYTES (256 * 1024 * 1024)  // 256 MiB
#define ATOMIC_MEMORY_STATE_SIZE_BYTES (25600 + STORE_TABLE_SIZE_BYTES)

SharedMemory setup_tlib_shared() 
{
  dbg_printf("setup_tlib_shared\n");

  void* atomic_ptr = calloc(1, ATOMIC_MEMORY_STATE_SIZE_BYTES);

  void* storeTablePointer = NULL;
  if (tlib_store_table_init) {
    storeTablePointer = aligned_alloc(STORE_TABLE_ALIGNMENT, STORE_TABLE_SIZE_BYTES);
  }

  mappings = (mapping*)calloc(MAX_MAPPINGS, sizeof(mapping));
  register_mapping(0x00000000, 256*1024*1024, NULL); // main memory
  register_mapping(0x40000000, 256*1024*1024, mappings[0].pointer);
  register_mapping(0xC0000000, 256*1024*1024, mappings[0].pointer);
  mappings[1].pointer = mappings[0].pointer;
  mappings[1].real_pointer = NULL; 
  mappings[2].pointer = mappings[0].pointer;
  mappings[2].real_pointer = NULL;
  
  return (SharedMemory) {.atomicState = atomic_ptr, .storeTable = storeTablePointer, .mappings = mappings};
}

__thread uint32_t atomic_id = -1;

typedef struct hst_entry hst_entry_t;
extern void initialize_store_table(hst_entry_t *store_table);

void tlib_reset_shared(SharedMemory shared) {
  // Reset mapped memory.
  for (int i = 0; i < MAX_MAPPINGS; i++) {
    mapping map = shared.mappings[i];
    if (map.pointer == NULL) continue;
    dbg_printf("resetting mapping %d at 0x%08lX of size %ld\n", i, map.offset, map.size);
    memset(map.pointer, 0, map.size);
  }
  // Reset store table.
  if (tlib_store_table_init) {
    memset(shared.storeTable, 0xff, STORE_TABLE_SIZE_BYTES);
  }
}

void register_mappings(mapping* new_mappings) {
  mappings = new_mappings;
  dbg_printf("using %p mappings\n", mappings);

  for (int i = 0; i < MAX_MAPPINGS; i++) {
    mapping map = mappings[i];
    if (map.pointer == NULL) continue;
    tlib_map_range(map.offset, map.size);
    dbg_printf("Registered mapping %d at 0x%08lX of size %ld\n", i, map.offset, map.size);
  }
}

void cpu_microbenchmark_reset(SharedMemory shared) 
{
  global_counter = 0;

  tlib_reset();

  // Recreate mappings. 
  register_mappings(shared.mappings);
}

void cpu_microbenchmark_dispose() {
  tlib_dispose();
}

void tlib_dispose_shared(SharedMemory shared) {
  for (int i = 0; i < MAX_MAPPINGS; i++) 
  {
    if (mappings[i].real_pointer == NULL) continue;

    free(mappings[i].real_pointer);
  }
}

void cpu_microbenchmark_setup(SharedMemory sharedMemory, bool hook_present) {

  tlib_init(cpu_model);

  register_mappings(sharedMemory.mappings);

  atomic_id = tlib_atomic_memory_state_init((uintptr_t)sharedMemory.atomicState, -1);
  if (tlib_store_table_init) {
#ifdef RESIZABLE_STORE_TABLE
    tlib_store_table_init((uintptr_t)sharedMemory.storeTable, STORE_TABLE_BITS);
#else
    tlib_store_table_init((uintptr_t)sharedMemory.storeTable);
#endif
  }

  dbg_printf("Hooks will be %s\n", hook_present ? "on" : "off");

	tlib_set_on_block_translation_enabled(hook_present);
  tlib_set_block_begin_hook_present(hook_present);
	tlib_set_block_finished_hook_present(hook_present);

  #define riscv_feature(c) (c - 'A')
  tlib_allow_feature(riscv_feature('A'));
  tlib_allow_feature(riscv_feature('M'));
  tlib_allow_feature(riscv_feature('I'));
  tlib_allow_feature(riscv_feature('S'));
  tlib_allow_feature(riscv_feature('C'));
  tlib_allow_additional_feature(RISCV_FEATURE_ZICSR);
  tlib_allow_additional_feature(RISCV_FEATURE_ZIFENCEI);
}

void tlib_unlock_dangling_locks(void) __attribute__((weak)); 

int cpu_microbenchmark_execute(Scenario* scenario) {
  // Initialize memory values.
  for (int i = 0; i < MAX_MEMORY_VALUES; i++)
  {
    MemoryInit memoryInit = scenario->memoryValues[i];
    if (memoryInit.guestAddress == 0) continue;

    tlib_write_quad_word(memoryInit.guestAddress, memoryInit.value, (uint64_t)cpu);
  }

  // Set up registers.
  for (int i = 0; i < MAX_REGISTERS; i++)
  {
    RegisterInit registerInit = scenario->registers[i];
    if (registerInit.id == 0) continue;

    tlib_set_register_value(registerInit.id, registerInit.value);
  }

  // Set up machine code.
  uint64_t code_bytes = MAX_INSTRUCTIONS * sizeof(uint32_t);
  // Need to place each core's code at different offsets.
  uint64_t code_offset = code_bytes * atomic_id;
  uint64_t guest_pc = 0x40f00000 + code_offset * atomic_id;
  uint32_t *assembly_pc_host_address = (uint32_t*)tlib_guest_offset_to_host_ptr(guest_pc);
  memcpy(assembly_pc_host_address, scenario->code, code_bytes);
	tlib_set_register_value(32, guest_pc); // 32 -- riscv pc

  uint32_t *pc_host_address = (uint32_t*)tlib_guest_offset_to_host_ptr(guest_pc);

  // todo: remove
  uint64_t iterationCount = 0x3000000;
  uint64_t* counterHostAddress = (uint64_t*)tlib_guest_offset_to_host_ptr(0x40f10000);

  clock_t start_time = clock() / (CLOCKS_PER_SEC / 1000);
  int last_sec = 0;

	while (!do_abort) {
		//dbg_printf("\n\n==========> executing instructions...\n");
    uint32_t old_pc = tlib_get_register_value(32);
    uint8_t *old_host_pc = (uint8_t*)tlib_guest_offset_to_host_ptr(old_pc);

    // Exit if we've reached noop.
    if (old_host_pc[0] == 0) {
      dbg_printf("[MAIN_LOOP] Stopping because of unimp instruction!\n");
      break;
   }

		int32_t result = tlib_execute(scenario->blockSize);
    int count  = tlib_get_executed_instructions();
    global_counter += count;

    if ((clock() / CLOCKS_PER_SEC) != last_sec) { 
       print_regs();
       dbg_printf("[MAIN_LOOP] Counted 0x%llx / 0x%llx so far [%d%%]\n", *counterHostAddress, iterationCount, iterationCount != 0 ? (100 * (*counterHostAddress) / iterationCount) : 0);
       last_sec = clock() / CLOCKS_PER_SEC;
    }
    
    if (!no_debug) {
      dbg_printf("[MAIN_LOOP] Executed %08X, pc now at %08X, count %d [all=%lld]\n", old_pc, tlib_get_register_value(32), count, global_counter);
      if (result != 0x10000)
        dbg_printf("[MAIN_LOOP] result is %d %08X\n", result, result);
    }
   if (limit > 0) do_abort = (limit <= global_counter);
  }


  dbg_printf("\n\n=======================\nExecuting finished!\n=======================\n\n");
	if (do_abort) {
    dbg_printf("Aborted.\n");
    exit(1);
  }
  
  if (tlib_unlock_dangling_locks)
    tlib_unlock_dangling_locks();

}
