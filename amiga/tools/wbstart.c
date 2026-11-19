/* WBStart <file> - start a program as if its icon were double-clicked
 * (workbench.library OpenWorkbenchObjectA, V44). For tests through AmiAgent:
 * the program gets a WBStartup message and the stack of its icon. */
#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/wb.h>
#include <stdio.h>

struct Library *WorkbenchBase = NULL;

int main(int argc, char **argv)
{
  int ok;
  if (argc < 2) {
    printf("Usage: WBStart <file>\n");
    return RETURN_FAIL;
  }
  WorkbenchBase = OpenLibrary((CONST_STRPTR)"workbench.library", 44);
  if (WorkbenchBase == NULL) {
    printf("workbench.library 44 needed\n");
    return RETURN_FAIL;
  }
  ok = OpenWorkbenchObjectA((STRPTR)argv[1], NULL);
  printf("%s: %s\n", argv[1], ok ? "started" : "failed");
  CloseLibrary(WorkbenchBase);
  return ok ? RETURN_OK : RETURN_FAIL;
}
