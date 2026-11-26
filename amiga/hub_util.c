/* hub_util.c - files, text and settings (see hub_util.h). */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
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
    } else if (strncmp(line, "PRIVATE ", 8) == 0) {
      strncpy(p->priv, line + 8, sizeof(p->priv) - 1);
    } else if (strncmp(line, "PRIVATE-CERT ", 13) == 0) {
      strncpy(p->priv_cert, line + 13, sizeof(p->priv_cert) - 1);
    } else if (strncmp(line, "PRIVATE-PASS ", 13) == 0) {
      strncpy(p->priv_pass, line + 13, sizeof(p->priv_pass) - 1);
    } else if (strncmp(line, "CATALOG ", 8) == 0) {
      strncpy(p->catalog, line + 8, sizeof(p->catalog) - 1);
    } else if (strncmp(line, "FAVORITES ", 10) == 0) {
      strncpy(p->favorites, line + 10, sizeof(p->favorites) - 1);
    } else if (strncmp(line, "TIMEZONE ", 9) == 0) {
      p->tz_min = atol(line + 9 + (line[9] == '+'));
      p->tz_set = 1;
    } else if (strncmp(line, "DST ", 4) == 0) {
      p->dst_off = strncmp(line + 4, "OFF", 3) == 0;
    } else if (sscanf(line, "FILTER %15s", a) == 1) {
      strcpy(p->filter, a);
    } else if (sscanf(line, "GONE %31s %23s %255[^\n]", a, b, c) == 3) {
      hub_prefs_set_gone(p, a, b, c);
    } else if (sscanf(line, "INSTALLED %31s %23s %255[^\n]", a, b, c) == 3) {
      hub_prefs_set_installed(p, a, b, c);
    }
  }
  hub_free(text);
}

int hub_prefs_save(const HubPrefs *p, const char *file)
{
  char buf[256 * 5 + 320 + HUB_MAX_INSTALLED * 330 * 2 + 1024 + 64];
  int n = 0, i;
  if (p->language[0]) n += snprintf(buf + n, sizeof(buf) - n, "LANGUAGE %s\n", p->language);
  if (p->catalog[0]) n += snprintf(buf + n, sizeof(buf) - n, "CATALOG %s\n", p->catalog);
  if (p->gamesdir[0]) n += snprintf(buf + n, sizeof(buf) - n, "GAMESDIR %s\n", p->gamesdir);
  if (p->appsdir[0]) n += snprintf(buf + n, sizeof(buf) - n, "APPSDIR %s\n", p->appsdir);
  if (p->priv[0]) n += snprintf(buf + n, sizeof(buf) - n, "PRIVATE %s\n", p->priv);
  if (p->priv_cert[0]) n += snprintf(buf + n, sizeof(buf) - n, "PRIVATE-CERT %s\n", p->priv_cert);
  if (p->priv_pass[0]) n += snprintf(buf + n, sizeof(buf) - n, "PRIVATE-PASS %s\n", p->priv_pass);
  if (p->favorites[0]) n += snprintf(buf + n, sizeof(buf) - n, "FAVORITES %s\n", p->favorites);
  if (p->filter[0]) n += snprintf(buf + n, sizeof(buf) - n, "FILTER %s\n", p->filter);
  if (p->tz_set) {
    n += snprintf(buf + n, sizeof(buf) - n, "TIMEZONE %+ld\nDST %s\n", (long)p->tz_min,
                  p->dst_off ? "OFF" : "AUTO");
  }
  for (i = 0; i < p->ninst && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "INSTALLED %s %s %s\n", p->inst[i].key,
                  p->inst[i].version, p->inst[i].dir);
  }
  for (i = 0; i < p->ngone && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "GONE %s %s %s\n", p->gone[i].key,
                  p->gone[i].version, p->gone[i].dir);
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

HubInstalled *hub_prefs_gone(HubPrefs *p, const char *key)
{
  int i;
  for (i = 0; i < p->ngone; i++) {
    if (strcmp(p->gone[i].key, key) == 0) return &p->gone[i];
  }
  return NULL;
}

void hub_prefs_set_gone(HubPrefs *p, const char *key, const char *version, const char *dir)
{
  HubInstalled *in = hub_prefs_gone(p, key);
  if (in == NULL) {
    if (p->ngone >= HUB_MAX_INSTALLED) return;
    in = &p->gone[p->ngone++];
  }
  memset(in, 0, sizeof(*in));
  strncpy(in->key, key, sizeof(in->key) - 1);
  strncpy(in->version, version, sizeof(in->version) - 1);
  strncpy(in->dir, dir, sizeof(in->dir) - 1);
}

void hub_prefs_set_installed(HubPrefs *p, const char *key, const char *version,
                             const char *dir)
{
  HubInstalled *in = hub_prefs_gone(p, key);
  if (in != NULL) *in = p->gone[--p->ngone];   /* there again */
  in = hub_prefs_installed(p, key);
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

/* where key is in the list "a,b,c" (its first letter), NULL when not */
static const char *fav_find(const char *list, const char *key)
{
  int n = (int)strlen(key);
  const char *q = list;
  while (n > 0 && *q) {
    const char *e = strchr(q, ',');
    int len = e != NULL ? (int)(e - q) : (int)strlen(q);
    if (len == n && strncmp(q, key, n) == 0) return q;
    if (e == NULL) break;
    q = e + 1;
  }
  return NULL;
}

int hub_prefs_favorite(const HubPrefs *p, const char *key)
{
  return fav_find(p->favorites, key) != NULL;
}

int hub_prefs_toggle_favorite(HubPrefs *p, const char *key)
{
  char *list = p->favorites;
  const char *at = fav_find(list, key);
  int n = (int)strlen(key), len = (int)strlen(list);
  if (at != NULL) {
    char *s = list + (at - list), *e = s + n;
    if (*e == ',') e++;                        /* "a,KEY,b" -> "a,b" */
    else if (s > list) s--;                    /* "a,KEY" -> "a" */
    memmove(s, e, strlen(e) + 1);
    return 0;
  }
  if (n == 0 || strchr(key, ',') != NULL || len + n + 2 > (int)sizeof(p->favorites)) return 0;
  if (len > 0) list[len++] = ',';
  strcpy(list + len, key);
  return 1;
}

/* --- remembered files ------------------------------------------------------ */

static int file_stamp(const char *name, ULONG *size, struct DateStamp *ds)
{
  BPTR lock = Lock((CONST_STRPTR)name, ACCESS_READ);
  struct FileInfoBlock *fib;
  int ok = 0;
  if (lock == 0) return 0;
  fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
  if (fib != NULL) {
    if (Examine(lock, fib) && fib->fib_DirEntryType < 0) {
      *size = (ULONG)fib->fib_Size;
      *ds = fib->fib_Date;
      ok = 1;
    }
    FreeDosObject(DOS_FIB, fib);
  }
  UnLock(lock);
  return ok;
}

static HubStamp *stamp_find(HubStamps *t, const char *name)
{
  int i;
  for (i = 0; i < t->n; i++) {
    if (strcmp(t->e[i].name, name) == 0) return &t->e[i];
  }
  return NULL;
}

static HubStamp *stamp_add(HubStamps *t, const char *name)
{
  HubStamp *e;
  if (t->n == t->cap) {
    int cap = t->cap ? t->cap * 2 : 64, i;
    HubStamp *ne = (HubStamp *)hub_malloc(cap * sizeof(HubStamp));
    if (ne == NULL) return NULL;
    for (i = 0; i < t->n; i++) ne[i] = t->e[i];
    hub_free(t->e);
    t->e = ne;
    t->cap = cap;
  }
  e = &t->e[t->n];
  e->name = (char *)hub_malloc(strlen(name) + 1);
  if (e->name == NULL) return NULL;
  strcpy(e->name, name);
  e->value[0] = 0;
  t->n++;
  return e;
}

void hub_stamps_load(HubStamps *t, const char *file)
{
  char *text = hub_read_file(file, 1UL << 20, NULL), *line, *next;
  if (text == NULL) return;
  for (line = text; line != NULL && *line; line = next) {
    unsigned long size;
    long days, minute, tick;
    char value[68];
    int used = 0;
    HubStamp *e;
    next = strchr(line, '\n');
    if (next != NULL) *next++ = 0;
    if (sscanf(line, "%lu %ld %ld %ld %67s %n", &size, &days, &minute, &tick, value, &used) < 5 ||
        used == 0 || line[used] == 0 || stamp_find(t, line + used) != NULL) {
      continue;
    }
    e = stamp_add(t, line + used);
    if (e == NULL) break;
    e->size = size;
    e->days = days;
    e->minute = minute;
    e->tick = tick;
    strcpy(e->value, value);
  }
  hub_free(text);
}

int hub_stamps_save(HubStamps *t, const char *file)
{
  char *buf;
  ULONG cap = 64, len = 0;
  int i, ok;
  if (!t->dirty) return 1;
  for (i = 0; i < t->n; i++) cap += strlen(t->e[i].name) + 120;
  buf = (char *)hub_malloc(cap);
  if (buf == NULL) return 0;
  for (i = 0; i < t->n; i++) {
    HubStamp *e = &t->e[i];
    len += snprintf(buf + len, cap - len, "%lu %ld %ld %ld %s %s\n", (unsigned long)e->size,
                    (long)e->days, (long)e->minute, (long)e->tick, e->value, e->name);
  }
  ok = hub_write_file(file, buf, len);
  hub_free(buf);
  if (ok) t->dirty = 0;
  return ok;
}

void hub_stamps_free(HubStamps *t)
{
  int i;
  for (i = 0; i < t->n; i++) hub_free(t->e[i].name);
  hub_free(t->e);
  t->e = NULL;
  t->n = t->cap = 0;
  t->dirty = 0;
}

const char *hub_stamps_get(HubStamps *t, const char *name, ULONG *size)
{
  HubStamp *e = stamp_find(t, name);
  struct DateStamp ds;
  ULONG now;
  if (e == NULL || !file_stamp(name, &now, &ds)) return NULL;
  if (now != e->size || ds.ds_Days != e->days || ds.ds_Minute != e->minute ||
      ds.ds_Tick != e->tick) {
    return NULL;
  }
  if (size != NULL) *size = now;
  return e->value;
}

void hub_stamps_set(HubStamps *t, const char *name, const char *value)
{
  HubStamp *e;
  struct DateStamp ds;
  ULONG size;
  if (strlen(value) >= sizeof(e->value) || strchr(value, ' ') != NULL ||
      strchr(name, '\n') != NULL || !file_stamp(name, &size, &ds)) {
    return;
  }
  e = stamp_find(t, name);
  if (e == NULL) e = stamp_add(t, name);
  if (e == NULL) return;
  e->size = size;
  e->days = ds.ds_Days;
  e->minute = ds.ds_Minute;
  e->tick = ds.ds_Tick;
  strcpy(e->value, value);
  t->dirty = 1;
}

ULONG hub_fnv(const char *data, ULONG len)
{
  ULONG h = 2166136261UL, i;
  for (i = 0; i < len; i++) {
    h ^= (UBYTE)data[i];
    h *= 16777619UL;
  }
  return h;
}

ULONG hub_ms(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return (ULONG)ds.ds_Days * 86400000UL + (ULONG)ds.ds_Minute * 60000UL + (ULONG)ds.ds_Tick * 20UL;
}
