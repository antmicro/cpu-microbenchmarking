
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

extern __thread uint32_t atomic_id;

int dbg = 0;
void dbg_printf(const char *fmt, ...) {
    if (!dbg) return;
    if (atomic_id != -1)
        printf("core %i: ", atomic_id);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
