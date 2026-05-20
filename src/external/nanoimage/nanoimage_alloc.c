#include "nanoimage.h"

#include <stdlib.h>

typedef struct {
  size_t requested_size;
  max_align_t _align;
} ni_allocation_header;

typedef struct {
  ni_allocator allocator;
  size_t current_allocation;
} ni_allocator_state;

static void *ni_default_malloc(size_t size, void *user_data) {
  (void)user_data;
  return malloc(size);
}

static void *ni_default_realloc(void *ptr, size_t size, void *user_data) {
  (void)user_data;
  return realloc(ptr, size);
}

static void ni_default_free(void *ptr, void *user_data) {
  (void)user_data;
  free(ptr);
}

static ni_allocator_state g_allocator = {{
    ni_default_malloc,
    ni_default_realloc,
    ni_default_free,
    NULL,
    NI_DEFAULT_MAX_ALLOCATION,
    NI_DEFAULT_MAX_ALLOCATION,
},
                                         0u};

static size_t ni_requested_to_actual_size(size_t requested) {
  return sizeof(ni_allocation_header) + ((requested == 0u) ? 1u : requested);
}

static int ni_allocation_within_limits(size_t old_size, size_t new_size) {
  size_t remaining;
  size_t new_total;

  if (new_size > g_allocator.allocator.max_allocation) {
    return 0;
  }

  if (old_size > g_allocator.current_allocation) {
    return 0;
  }

  remaining = g_allocator.current_allocation - old_size;
  if (remaining > (SIZE_MAX - new_size)) {
    return 0;
  }
  new_total = remaining + new_size;
  if (new_total > g_allocator.allocator.max_total_allocation) {
    return 0;
  }

  return 1;
}

void ni_set_allocator(const ni_allocator *allocator) {
  if ((allocator == NULL) || (allocator->malloc_fn == NULL) ||
      (allocator->realloc_fn == NULL) || (allocator->free_fn == NULL)) {
    return;
  }

  g_allocator.allocator = *allocator;
  if (g_allocator.allocator.max_allocation == 0u) {
    g_allocator.allocator.max_allocation = NI_DEFAULT_MAX_ALLOCATION;
  }
  if (g_allocator.allocator.max_total_allocation == 0u) {
    g_allocator.allocator.max_total_allocation =
        g_allocator.allocator.max_allocation;
  }
}

void ni_reset_allocator(void) {
  g_allocator.allocator.malloc_fn = ni_default_malloc;
  g_allocator.allocator.realloc_fn = ni_default_realloc;
  g_allocator.allocator.free_fn = ni_default_free;
  g_allocator.allocator.user_data = NULL;
  g_allocator.allocator.max_allocation = NI_DEFAULT_MAX_ALLOCATION;
  g_allocator.allocator.max_total_allocation = NI_DEFAULT_MAX_ALLOCATION;
}

void *ni_stbi_malloc(size_t size) {
  ni_allocation_header *header;
  void *base;
  size_t actual_size;

  if (!ni_allocation_within_limits(0u, size)) {
    return NULL;
  }
  actual_size = ni_requested_to_actual_size(size);
  if (actual_size < size) {
    return NULL;
  }

  base = g_allocator.allocator.malloc_fn(actual_size, g_allocator.allocator.user_data);
  if (base == NULL) {
    return NULL;
  }

  header = (ni_allocation_header *)base;
  header->requested_size = size;
  g_allocator.current_allocation += size;
  return (void *)(header + 1);
}

void *ni_stbi_realloc(void *ptr, size_t size) {
  ni_allocation_header *header;
  void *base;
  size_t old_size;
  size_t actual_size;

  if (ptr == NULL) {
    return ni_stbi_malloc(size);
  }

  header = ((ni_allocation_header *)ptr) - 1;
  old_size = header->requested_size;
  if (!ni_allocation_within_limits(old_size, size)) {
    return NULL;
  }

  actual_size = ni_requested_to_actual_size(size);
  if (actual_size < size) {
    return NULL;
  }

  base = g_allocator.allocator.realloc_fn((void *)header, actual_size,
                                          g_allocator.allocator.user_data);
  if (base == NULL) {
    return NULL;
  }

  header = (ni_allocation_header *)base;
  header->requested_size = size;
  g_allocator.current_allocation = g_allocator.current_allocation - old_size + size;
  return (void *)(header + 1);
}

void ni_stbi_free(void *ptr) {
  ni_allocation_header *header;

  if (ptr == NULL) {
    return;
  }

  header = ((ni_allocation_header *)ptr) - 1;
  if (header->requested_size <= g_allocator.current_allocation) {
    g_allocator.current_allocation -= header->requested_size;
  } else {
    g_allocator.current_allocation = 0u;
  }
  g_allocator.allocator.free_fn((void *)header, g_allocator.allocator.user_data);
}

void ni_image_free(ni_image *image) {
  if (image == NULL) {
    return;
  }
  ni_stbi_free(image->data);
  image->data = NULL;
  image->data_size = 0;
  image->width = 0;
  image->height = 0;
  image->channels = 0;
  image->bit_depth = 0;
}

void ni_buffer_free(ni_buffer *buffer) {
  if (buffer == NULL) {
    return;
  }
  ni_stbi_free(buffer->data);
  buffer->data = NULL;
  buffer->size = 0u;
}
