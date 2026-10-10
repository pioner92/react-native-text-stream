// Text stream buffer (TSB1): a single-writer, multi-reader append-only byte log
// in one memory block, readable by any consumer that knows this layout.
//
// [0..64)  header, little-endian
// [64..)   data, `capacity` bytes; [0, committed) is immutable once published
#pragma once

#include <stdint.h>
#include <string.h>

#define TSB_MAGIC 0x31425354u /* "TSB1" */
#define TSB_VERSION 1
#define TSB_HEADER_SIZE 64

enum {
  TSB_STATUS_OPEN = 0,
  TSB_STATUS_DONE = 1,
  TSB_STATUS_ERROR = 2,
  TSB_STATUS_CANCELLED = 3,
  TSB_STATUS_OVERFLOW = 4,
};

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint64_t committed; // written with release, read with acquire
  uint64_t capacity;
  uint32_t status;    // written with release after the final committed store
  uint32_t reserved0;
  uint8_t reserved[32];
} TSBHeader;

_Static_assert(sizeof(TSBHeader) == TSB_HEADER_SIZE, "TSB header must be 64 bytes");

static inline void tsb_init(void *base, uint64_t capacity) {
  TSBHeader *h = (TSBHeader *)base;
  memset(h, 0, sizeof(TSBHeader));
  h->magic = TSB_MAGIC;
  h->version = TSB_VERSION;
  h->headerSize = TSB_HEADER_SIZE;
  h->capacity = capacity;
}

/// Appends `len` bytes; single writer only. Returns 0 and sets OVERFLOW if they don't fit.
static inline int tsb_append(void *base, const void *src, uint64_t len) {
  TSBHeader *h = (TSBHeader *)base;
  uint64_t committed = __atomic_load_n(&h->committed, __ATOMIC_RELAXED);
  if (len > h->capacity - committed) {
    __atomic_store_n(&h->status, TSB_STATUS_OVERFLOW, __ATOMIC_RELEASE);
    return 0;
  }
  memcpy((uint8_t *)base + TSB_HEADER_SIZE + committed, src, (size_t)len);
  __atomic_store_n(&h->committed, committed + len, __ATOMIC_RELEASE);
  return 1;
}

static inline void tsb_set_status(void *base, uint32_t status) {
  TSBHeader *h = (TSBHeader *)base;
  uint32_t expected = TSB_STATUS_OPEN;
  // The first terminal status wins (an overflow is not overwritten by DONE).
  __atomic_compare_exchange_n(&h->status, &expected, status, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

static inline uint64_t tsb_committed(const void *base) {
  return __atomic_load_n(&((const TSBHeader *)base)->committed, __ATOMIC_ACQUIRE);
}

static inline uint32_t tsb_status(const void *base) {
  return __atomic_load_n(&((const TSBHeader *)base)->status, __ATOMIC_ACQUIRE);
}
