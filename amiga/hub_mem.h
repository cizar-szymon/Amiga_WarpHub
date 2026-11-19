/* hub_mem - memory for the hub modules through exec (AllocVec), safe from
 * two processes at once: the window and the network process both allocate,
 * and the libnix malloc pool is not protected. */
#ifndef HUB_MEM_H
#define HUB_MEM_H

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

static __inline void *hub_malloc(ULONG size)
{
  ULONG *p = (ULONG *)AllocVec(size + 4, MEMF_ANY);
  if (p == NULL) return NULL;
  p[0] = size;
  return p + 1;
}

static __inline void *hub_calloc(ULONG n, ULONG size)
{
  ULONG *p = (ULONG *)AllocVec(n * size + 4, MEMF_ANY | MEMF_CLEAR);
  if (p == NULL) return NULL;
  p[0] = n * size;
  return p + 1;
}

static __inline void hub_free(void *mem)
{
  if (mem != NULL) FreeVec((ULONG *)mem - 1);
}

static __inline void *hub_realloc(void *mem, ULONG size)
{
  void *n;
  ULONG old;
  if (mem == NULL) return hub_malloc(size);
  old = ((ULONG *)mem)[-1];
  if (size <= old) return mem;
  n = hub_malloc(size);
  if (n == NULL) return NULL;
  memcpy(n, mem, old);
  hub_free(mem);
  return n;
}

#endif
