/* Sweep constructor and storage allocation failures, checking all live owners. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void *__real_janus_alloc(uint64_t);
void *__real_janus_realloc(void *, uint64_t);
void __real_janus_free(void *);
static uint64_t attempts, blocks, created, dropped;
static int fail(void) {
    const char *target = getenv("MAP_FAIL_AT");
    return ++attempts == (target ? strtoull(target, NULL, 10) : 0);
}
void *__wrap_janus_alloc(uint64_t size) {
    if (fail()) return NULL;
    void *result = __real_janus_alloc(size);
    if (result) ++blocks;
    return result;
}
void *__wrap_janus_realloc(void *pointer, uint64_t size) {
    if (fail()) return NULL;
    void *result = __real_janus_realloc(pointer, size);
    if (result && !pointer) ++blocks;
    return result;
}
void __wrap_janus_free(void *pointer) {
    if (pointer) --blocks;
    __real_janus_free(pointer);
}
int32_t map_created(int32_t id) { created |= UINT64_C(1) << id; return id; }
void map_dropped(int32_t id) {
    uint64_t bit = UINT64_C(1) << id;
    if (dropped & bit) { fprintf(stderr, "double destruction\n"); _Exit(99); }
    dropped |= bit;
}
void map_verify(void) {
    if (blocks || created != dropped) {
        fprintf(stderr, "map cleanup failure: blocks=%llu created=%llu dropped=%llu\n",
                (unsigned long long)blocks, (unsigned long long)created,
                (unsigned long long)dropped);
        _Exit(99);
    }
    printf("cleanup ok attempts=%llu\n", (unsigned long long)attempts);
    fflush(stdout);
}
_Noreturn void __wrap_abort(void) { map_verify(); _Exit(134); }
