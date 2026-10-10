#include "rg_ring.h"

/* Producer-written statistics are read by other threads for display only. */
#define RG_RING_STORE(field, value) __atomic_store_n(&(field), (value), __ATOMIC_RELAXED)
#define RG_RING_LOAD(field)         __atomic_load_n(&(field), __ATOMIC_RELAXED)

bool rg_ring_init(RgRing* r, int32_t* storage, uint32_t capacity) {
    if(!storage || capacity < 2 || (capacity & (capacity - 1)) != 0) return false;
    r->buf = storage;
    r->mask = capacity - 1;
    r->head = 0;
    r->tail = 0;
    r->pushed = 0;
    r->dropped = 0;
    r->gaps = 0;
    r->first_gap = 0;
    r->peak = 0;
    r->in_gap = false;
    return true;
}

bool rg_ring_push(RgRing* r, int32_t value) {
    uint32_t head = r->head;
    uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
    uint32_t used = head - tail;
    if(used > r->mask) {
        if(!r->in_gap) {
            r->in_gap = true;
            if(r->gaps == 0) RG_RING_STORE(r->first_gap, r->pushed);
            RG_RING_STORE(r->gaps, r->gaps + 1);
        }
        RG_RING_STORE(r->dropped, r->dropped + 1);
        return false;
    }
    r->buf[head & r->mask] = value;
    /* Publish the slot only after it is written. */
    __atomic_store_n(&r->head, head + 1, __ATOMIC_RELEASE);
    r->in_gap = false;
    RG_RING_STORE(r->pushed, r->pushed + 1);
    if(used + 1 > r->peak) RG_RING_STORE(r->peak, used + 1);
    return true;
}

size_t rg_ring_pop(RgRing* r, int32_t* out, size_t max) {
    uint32_t tail = r->tail;
    uint32_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
    uint32_t n = head - tail;
    if(n > max) n = (uint32_t)max;
    for(uint32_t i = 0; i < n; i++)
        out[i] = r->buf[(tail + i) & r->mask];
    /* Hand the slots back only after they are copied out. */
    __atomic_store_n(&r->tail, tail + n, __ATOMIC_RELEASE);
    return n;
}

uint32_t rg_ring_used(const RgRing* r) {
    uint32_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

void rg_ring_stats(const RgRing* r, RgRingStats* out) {
    out->pushed = RG_RING_LOAD(r->pushed);
    out->dropped = RG_RING_LOAD(r->dropped);
    out->gaps = RG_RING_LOAD(r->gaps);
    out->first_gap = RG_RING_LOAD(r->first_gap);
    out->peak = RG_RING_LOAD(r->peak);
    out->used = rg_ring_used(r);
}

uint32_t rg_ring_capacity(const RgRing* r) {
    return r->mask + 1;
}

uint32_t rg_ring_capacity_for(size_t bytes, uint32_t min_cap, uint32_t max_cap) {
    size_t fit = bytes / sizeof(int32_t);
    uint32_t cap = max_cap;
    while(cap >= min_cap && cap > fit)
        cap >>= 1;
    return (cap >= min_cap && cap > 0) ? cap : 0;
}

int32_t rg_ring_sample(bool level, uint32_t duration_us) {
    int32_t d = duration_us > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)duration_us;
    return level ? d : -d;
}
