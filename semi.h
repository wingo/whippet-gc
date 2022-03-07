#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "precise-roots.h"

struct context {
  uintptr_t hp;
  uintptr_t limit;
  uintptr_t base;
  size_t size;
  struct handle *roots;
  long count;
};

static const uintptr_t ALIGNMENT = 8;

static uintptr_t align_up(uintptr_t addr, size_t align) {
  return (addr + align - 1) & ~(align-1);
}

#define GC_HEADER uintptr_t _gc_header

enum alloc_kind { NODE, DOUBLE_ARRAY };

typedef void (*field_visitor)(struct context *, void **ref);

static inline size_t node_size(void *obj) __attribute__((always_inline));
static inline size_t double_array_size(void *obj) __attribute__((always_inline));
static inline void visit_node_fields(struct context *cx, void *obj, field_visitor visit) __attribute__((always_inline));
static inline void visit_double_array_fields(struct context *cx, void *obj, field_visitor visit) __attribute__((always_inline));

static inline void clear_memory(uintptr_t addr, size_t size) {
  memset((char*)addr, 0, size);
}

static void collect(struct context *cx, size_t bytes) __attribute__((noinline));

static void process(struct context *cx, void **loc);

static void flip(struct context *cx) {
  uintptr_t split = cx->base + (cx->size >> 1);
  if (cx->hp <= split) {
    cx->hp = split;
    cx->limit = cx->base + cx->size;
  } else {
    cx->hp = cx->base;
    cx->limit = split;
  }
  cx->count++;
}  

static void* copy(struct context *cx, uintptr_t kind, void *obj) {
  size_t size;
  switch (kind) {
  case NODE:
    size = node_size(obj);
    break;
  case DOUBLE_ARRAY:
    size = double_array_size(obj);
    break;
  default:
    abort ();
  }
  void *new_obj = (void*)cx->hp;
  memcpy(new_obj, obj, size);
  *(uintptr_t*) obj = cx->hp;
  cx->hp += align_up (size, ALIGNMENT);
  return new_obj;
}

static uintptr_t scan(struct context *cx, uintptr_t grey) {
  void *obj = (void*)grey;
  uintptr_t kind = *(uintptr_t*) obj;
  switch (kind) {
  case NODE:
    visit_node_fields(cx, obj, process);
    return grey + align_up (node_size(obj), ALIGNMENT);
    break;
  case DOUBLE_ARRAY:
    visit_double_array_fields(cx, obj, process);
    return grey + align_up (double_array_size(obj), ALIGNMENT);
    break;
  default:
    abort ();
  }
}

static void* forward(struct context *cx, void *obj) {
  uintptr_t header_word = *(uintptr_t*)obj;
  switch (header_word) {
  case NODE:
  case DOUBLE_ARRAY:
    return copy(cx, header_word, obj);
  default:
    return (void*)header_word;
  }
}  

static void process(struct context *cx, void **loc) {
  void *obj = *loc;
  if (obj != NULL)
    *loc = forward(cx, obj);
}
static void collect(struct context *cx, size_t bytes) {
  // fprintf(stderr, "start collect #%ld:\n", cx->count);
  flip(cx);
  uintptr_t grey = cx->hp;
  for (struct handle *h = cx->roots; h; h = h->next)
    process(cx, &h->v);
  // fprintf(stderr, "pushed %zd bytes in roots\n", cx->hp - grey);
  while(grey < cx->hp)
    grey = scan(cx, grey);
  // fprintf(stderr, "%zd bytes copied\n", (cx->size>>1)-(cx->limit-cx->hp));

  if (cx->limit - cx->hp < bytes) {
    fprintf(stderr, "ran out of space, heap size %zu\n", cx->size);
    abort();
  }
}

static inline void* allocate(struct context *cx, enum alloc_kind kind,
                             size_t size) {
  while (1) {
    uintptr_t addr = cx->hp;
    uintptr_t new_hp = align_up (addr + size, ALIGNMENT);
    if (cx->limit < new_hp) {
      collect(cx, size);
      continue;
    }
    cx->hp = new_hp;
    void *ret = (void *)addr;
    uintptr_t *header_word = ret;
    *header_word = kind;
    if (kind == NODE)
      clear_memory(addr + sizeof(uintptr_t), size - sizeof(uintptr_t));
    return ret;
  }
}

static inline void init_field(void **addr, void *val) {
  *addr = val;
}
static inline void set_field(void **addr, void *val) {
  *addr = val;
}
static inline void* get_field(void **addr) {
  return *addr;
}

static inline void initialize_gc(struct context *cx, size_t size) {
  size = align_up(size, getpagesize());

  void *mem = mmap(NULL, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    perror("mmap failed");
    abort();
  }
  cx->hp = cx->base = (uintptr_t) mem;
  cx->size = size;
  cx->count = -1;
  flip(cx);
  cx->roots = NULL;
}

static inline void print_start_gc_stats(struct context *cx) {
}

static inline void print_end_gc_stats(struct context *cx) {
  printf("Completed %ld collections\n", cx->count);
  printf("Heap size is %zd\n", cx->size);
}