/* hub_util.c - files, text and settings (see hub_util.h). */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_util.h"

char *hub_read_file(const char *name, ULONG max, ULONG *len)
{
  BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
  char *data;
  LONG size, got;
  if (fh == 0) return NULL;
  Seek(fh, 0, OFFSET_END);
  size = Seek(fh, 0, OFFSET_BEGINNING);
  if (size < 0 || (ULONG)size > max) {
    Close(fh);
    return NULL;
  }
  data = (char *)hub_malloc(size + 1);
  if (data == NULL) {
    Close(fh);
    return NULL;
  }
  got = size > 0 ? Read(fh, data, size) : 0;
  Close(fh);
  if (got != size) {
    hub_free(data);
    return NULL;
  }
  data[size] = 0;
  if (len != NULL) *len = (ULONG)size;
  return data;
}

int hub_write_file(const char *name, const void *data, ULONG len)
{
  char tmp[300];
  BPTR fh;
  int ok;
  snprintf(tmp, sizeof(tmp), "%s.tmp", name);
  hub_make_dirs_for(name);
  fh = Open((CONST_STRPTR)tmp, MODE_NEWFILE);
  if (fh == 0) return 0;
  ok = len == 0 || Write(fh, (APTR)data, len) == (LONG)len;
  Close(fh);
  if (!ok) {
    DeleteFile((CONST_STRPTR)tmp);
    return 0;
  }
  DeleteFile((CONST_STRPTR)name);
  return Rename((CONST_STRPTR)tmp, (CONST_STRPTR)name) != 0;
}

void hub_make_dirs_for(const char *name)
{
  char part[300];
  int i, start = 0;
  for (i = 0; name[i] && i < (int)sizeof(part) - 1; i++) {
    if (name[i] == ':') start = i + 1;
  }
  for (i = start; name[i] && i < (int)sizeof(part) - 1; i++) {
    if (name[i] == '/' && i > start) {
      BPTR lock;
      memcpy(part, name, i);
      part[i] = 0;
      lock = Lock((CONST_STRPTR)part, ACCESS_READ);
      if (lock == 0) lock = CreateDir((CONST_STRPTR)part);
      if (lock != 0) UnLock(lock);
    }
  }
}

void hub_to_latin(const char *utf8, char *out, int outlen)
{
  const unsigned char *s = (const unsigned char *)utf8;
  int o = 0;
  while (*s && o < outlen - 1) {
    unsigned long c = *s++;
    if (c >= 0xC0 && (*s & 0xC0) == 0x80) {
      if (c < 0xE0) {
        c = ((c & 0x1F) << 6) | (*s++ & 0x3F);
      } else if (c < 0xF0 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        s += 2;
      } else {
        while ((*s & 0xC0) == 0x80) s++;
        c = '?';
      }
    } else if (c >= 0x80) {
      /* not UTF-8: one byte of ISO-8859-2 (Polish texts of some ports, e.g.
       * AmiXcom) - its Polish letters to Unicode, the rest as Latin-1 */
      static const unsigned short l2[][2] = {
        {0xA1, 0x104}, {0xB1, 0x105}, {0xC6, 0x106}, {0xE6, 0x107}, {0xCA, 0x118},
        {0xEA, 0x119}, {0xA3, 0x141}, {0xB3, 0x142}, {0xD1, 0x143}, {0xF1, 0x144},
        {0xA6, 0x15A}, {0xB6, 0x15B}, {0xAC, 0x179}, {0xBC, 0x17A}, {0xAF, 0x17B},
        {0xBF, 0x17C}};
      int k;
      for (k = 0; k < (int)(sizeof(l2) / sizeof(l2[0])); k++) {
        if (l2[k][0] == c) {
          c = l2[k][1];
          break;
        }
      }
    }
    switch (c) {
      case 0x104: c = 'A'; break;  case 0x105: c = 'a'; break;   /* Ą ą */
      case 0x106: c = 'C'; break;  case 0x107: c = 'c'; break;   /* Ć ć */
      case 0x118: c = 'E'; break;  case 0x119: c = 'e'; break;   /* Ę ę */
      case 0x141: c = 'L'; break;  case 0x142: c = 'l'; break;   /* Ł ł */
      case 0x143: c = 'N'; break;  case 0x144: c = 'n'; break;   /* Ń ń */
      case 0x15A: c = 'S'; break;  case 0x15B: c = 's'; break;   /* Ś ś */
      case 0x179: c = 'Z'; break;  case 0x17A: c = 'z'; break;   /* Ź ź */
      case 0x17B: c = 'Z'; break;  case 0x17C: c = 'z'; break;   /* Ż ż */
      case 0x2013: case 0x2014: c = '-'; break;                  /* – — */
      case 0x2018: case 0x2019: c = '\''; break;
      case 0x201C: case 0x201D: case 0x201E: c = '"'; break;
      case 0x2026: c = '.'; break;
      case 0x2192: c = '>'; break;                               /* → */
      default: if (c > 0xFF) c = '?';
    }
    out[o++] = (char)c;
  }
  out[o] = 0;
}

/* --- settings ------------------------------------------------------------- */

void hub_prefs_load(HubPrefs *p, const char *file)
{
  char *text, *line, *next;
  memset(p, 0, sizeof(*p));
  text = hub_read_file(file, 65536, NULL);
  if (text == NULL) return;
  for (line = text; line && *line; line = next) {
    char a[64], b[32], c[256];
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    if (sscanf(line, "LANGUAGE %3s", a) == 1) {
      strcpy(p->language, a);
    } else if (strncmp(line, "GAMESDIR ", 9) == 0) {
      strncpy(p->gamesdir, line + 9, sizeof(p->gamesdir) - 1);
    } else if (strncmp(line, "APPSDIR ", 8) == 0) {
      strncpy(p->appsdir, line + 8, sizeof(p->appsdir) - 1);
    } else if (strncmp(line, "CATALOG ", 8) == 0) {
      strncpy(p->catalog, line + 8, sizeof(p->catalog) - 1);
    } else if (sscanf(line, "INSTALLED %31s %23s %255[^\n]", a, b, c) == 3) {
      hub_prefs_set_installed(p, a, b, c);
    }
  }
  hub_free(text);
}

int hub_prefs_save(const HubPrefs *p, const char *file)
{
  char buf[256 * 5 + HUB_MAX_INSTALLED * 330];
  int n = 0, i;
  if (p->language[0]) n += snprintf(buf + n, sizeof(buf) - n, "LANGUAGE %s\n", p->language);
  if (p->catalog[0]) n += snprintf(buf + n, sizeof(buf) - n, "CATALOG %s\n", p->catalog);
  if (p->gamesdir[0]) n += snprintf(buf + n, sizeof(buf) - n, "GAMESDIR %s\n", p->gamesdir);
  if (p->appsdir[0]) n += snprintf(buf + n, sizeof(buf) - n, "APPSDIR %s\n", p->appsdir);
  for (i = 0; i < p->ninst && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "INSTALLED %s %s %s\n", p->inst[i].key,
                  p->inst[i].version, p->inst[i].dir);
  }
  if (n > (int)sizeof(buf)) n = sizeof(buf);
  return hub_write_file(file, buf, n);
}

HubInstalled *hub_prefs_installed(HubPrefs *p, const char *key)
{
  int i;
  for (i = 0; i < p->ninst; i++) {
    if (strcmp(p->inst[i].key, key) == 0) return &p->inst[i];
  }
  return NULL;
}

void hub_prefs_set_installed(HubPrefs *p, const char *key, const char *version,
                             const char *dir)
{
  HubInstalled *in = hub_prefs_installed(p, key);
  if (in == NULL) {
    if (p->ninst >= HUB_MAX_INSTALLED) return;
    in = &p->inst[p->ninst++];
  }
  strncpy(in->key, key, sizeof(in->key) - 1);
  strncpy(in->version, version, sizeof(in->version) - 1);
  strncpy(in->dir, dir, sizeof(in->dir) - 1);
}

void hub_prefs_remove_installed(HubPrefs *p, const char *key)
{
  HubInstalled *in = hub_prefs_installed(p, key);
  if (in == NULL) return;
  *in = p->inst[--p->ninst];
}
