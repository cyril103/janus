/* Test-only allocation accounting and deterministic failure injection.
 * Linked with GNU --wrap; the production runtime has no test switches. */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef union { max_align_t alignment; uint64_t size; } Header;
void *__real_janus_alloc(uint64_t);
void *__real_janus_realloc(void *, uint64_t);
void __real_janus_free(void *);
static uint64_t live, peak, allocations, blocks, requested, eligible, target, drops, expected;
static int armed;
static uint64_t nodes;
void vector_node_created(void) { ++nodes; }

void vector_arm(uint64_t count) {
    const char *failure = getenv("VECTOR_FAIL_AT");
    target = failure ? strtoull(failure, NULL, 10) : 0;
    eligible = 0;
    nodes = 0;
    expected = count;
    armed = 1;
}
int32_t vector_mode(void) {
    const char *mode = getenv("VECTOR_MODE");
    return mode ? atoi(mode) : 0;
}
void vector_expect_more(void) { ++expected; }
void vector_drop(void) { ++drops; }
static int fail(uint64_t bytes, int resizing) {
    /* Shared's pointer slot and reference counter are eight bytes. Only
     * explicit alloc[T] sites reach this check; class/closure allocations
     * retain the runtime's fatal OOM contract. Zero-byte storage is skipped. */
    if (armed && (resizing || bytes == sizeof(uint64_t))) {
        ++eligible;
        if (target && eligible == target) return 1;
    }
    return 0;
}
static void account(uint64_t bytes) {
    ++allocations;
    requested += bytes;
    live += bytes;
    if (live > peak) peak = live;
}
void *__wrap_janus_alloc(uint64_t bytes) {
    Header *header = __real_janus_alloc(sizeof(Header) + bytes);
    if (!header) return NULL;
    header->size = bytes;
    ++blocks;
    account(bytes);
    return header + 1;
}
/* Only explicit alloc[T] sites in the test IR call this function. */
void *vector_storage_alloc(uint64_t bytes) {
    if (fail(bytes, 0)) return NULL;
    return __wrap_janus_alloc(bytes);
}
void *__wrap_janus_realloc(void *pointer, uint64_t bytes) {
    if (fail(bytes, 1)) return NULL;
    Header *old = pointer ? (Header *)pointer - 1 : NULL;
    uint64_t previous = old ? old->size : 0;
    Header *header = __real_janus_realloc(old, sizeof(Header) + bytes);
    if (!header) return NULL;
    if (!pointer) ++blocks;
    live -= previous;
    header->size = bytes;
    account(bytes);
    return header + 1;
}
void __wrap_janus_free(void *pointer) {
    if (!pointer) return;
    Header *header = (Header *)pointer - 1;
    --blocks;
    live -= header->size;
    __real_janus_free(header);
}
void vector_report(void) {
    printf("allocations=%" PRIu64 " requested_bytes=%" PRIu64
           " peak_live_bytes=%" PRIu64 " live_bytes=%" PRIu64 "\n",
           allocations, requested, peak, live);
}
void vector_verify(void) {
    if (live || blocks || drops != expected) {
        fprintf(stderr, "vector cleanup failure: live=%" PRIu64 " blocks=%" PRIu64 " drops=%" PRIu64
                " expected=%" PRIu64 "\n", live, blocks, drops, expected);
        _Exit(99);
    }
    printf("cleanup ok eligible=%" PRIu64 " nodes=%" PRIu64 "\n", eligible, nodes);
    fflush(stdout);
}
_Noreturn void __wrap_abort(void) {
    vector_verify();
    _Exit(134);
}
