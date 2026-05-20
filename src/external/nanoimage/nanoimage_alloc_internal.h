#ifndef NANOIMAGE_ALLOC_INTERNAL_H_
#define NANOIMAGE_ALLOC_INTERNAL_H_

#include <stddef.h>

void *ni_stbi_malloc(size_t size);
void *ni_stbi_realloc(void *ptr, size_t size);
void ni_stbi_free(void *ptr);

#endif
