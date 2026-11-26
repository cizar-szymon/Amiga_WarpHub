/* hub_catalog.c - reads catalog/catalog.txt (see hub_catalog.h). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hub_mem.h"
#include "hub_catalog.h"

int hub_lang(const char *name)
{
  if (name == NULL) return HUB_EN;
  if ((name[0] == 'p' || name[0] == 'P') && (name[1] == 'l' || name[1] == 'L')) return HUB_PL;
  if ((name[0] == 'd' || name[0] == 'D') && (name[1] == 'e' || name[1] == 'E')) return HUB_DE;
  return HUB_EN;
}

const char *hub_text(const char *const *texts, int lang)
{
  if (lang >= 0 && lang < HUB_LANGS && texts[lang] != NULL) return texts[lang];
  return texts[HUB_EN] != NULL ? texts[HUB_EN] : "";
}

static int name_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '+' || c == '-' ||
         c == '&';                     /* Syndicate Wars: maps/map030.b&b */
}

int hub_path_ok(const char *p)
{
  const char *part = p;
  if (p == NULL || *p == 0) return 0;
  for (;;) {
    const char *end = part;
    while (*end != 0 && *end != '/') end++;
    if (end == part || part[0] == ' ' || end[-1] == ' ') return 0;
    if (end - part == 2 && part[0] == '.' && part[1] == '.') return 0;
    {
      const char *q;
      for (q = part; q < end; q++) {
        if (*q == ' ') {
          if (q[1] == ' ') return 0;
        } else if (!name_char(*q)) {
          return 0;
        }
      }
    }
    if (*end == 0) return 1;
    part = end + 1;
  }
}

/* next word of *s (split at one space), NUL-terminated in place */
static char *word(char **s)
{
  char *w = *s, *e;
  if (w == NULL) return NULL;
  e = strchr(w, ' ');
  if (e != NULL) {
    *e = 0;
    *s = e + 1;
  } else {
    *s = NULL;
  }
  return w;
}

void hub_catalog_free(HubCatalog *cat)
{
  int i;
  if (cat == NULL) return;
  for (i = 0; i < cat->ngames; i++) hub_free(cat->games[i].files);
  hub_free(cat->games);
  hub_free(cat->text);
  hub_free(cat);
}

HubGame *hub_catalog_find(HubCatalog *cat, const char *key)
{
  int i;
  for (i = 0; i < cat->ngames; i++) {
    if (strcmp(cat->games[i].key, key) == 0) return &cat->games[i];
  }
  return NULL;
}

int hub_game_has_tag(const HubGame *g, const char *tag)
{
  const char *p = g->tags;
  int n = (int)strlen(tag);
  while (p != NULL && *p) {
    const char *e = strchr(p, ',');
    int len = e != NULL ? (int)(e - p) : (int)strlen(p);
    if (len == n && strncmp(p, tag, n) == 0) return 1;
    p = e != NULL ? e + 1 : NULL;
  }
  return 0;
}

HubFile *hub_game_file(HubGame *g, const char *path)
{
  int i;
  if (path == NULL) return NULL;
  for (i = 0; i < g->nfiles; i++) {
    if (strcmp(g->files[i].path, path) == 0) return &g->files[i];
  }
  return NULL;
}

HubCatalog *hub_catalog_parse(char *text, char *err, int errlen)
{
  HubCatalog *cat;
  HubGame *g = NULL;
  char *line, *next;
  int lines = 0, games = 0, lineno = 0, cap_files = 0;

  cat = (HubCatalog *)hub_calloc(1, sizeof(HubCatalog));
  if (cat == NULL) {
    hub_free(text);
    snprintf(err, errlen, "No memory.");
    return NULL;
  }
  cat->text = text;
  if (strncmp(text, "WARPHUB-CATALOG 1", 17) != 0) {
    snprintf(err, errlen, "Not a WARP HUB catalog (format 1).");
    hub_catalog_free(cat);
    return NULL;
  }
  for (line = text; *line; line++) {
    if (*line == '\n') lines++;
    if (strncmp(line, "\nGAME ", 6) == 0) games++;
  }
  cat->games = (HubGame *)hub_calloc(games + 1, sizeof(HubGame));
  if (cat->games == NULL) {
    snprintf(err, errlen, "No memory.");
    hub_catalog_free(cat);
    return NULL;
  }
  for (line = text; line != NULL && *line; line = next) {
    char *rest, *cmd;
    next = strchr(line, '\n');
    if (next != NULL) *next++ = 0;
    if (next != NULL && next - line >= 2 && next[-2] == '\r') next[-2] = 0;
    lineno++;
    if (line[0] == '#' || line[0] == 0) continue;
    rest = line;
    cmd = word(&rest);
    if (strcmp(cmd, "GAME") == 0 && rest != NULL) {
      if (g != NULL || cat->ngames >= games) goto bad;
      g = &cat->games[cat->ngames];
      g->key = rest;
      cap_files = 0;
      {
        /* count FILE lines up to END for one allocation */
        char *p = next;
        while (p != NULL && *p && strncmp(p, "END", 3) != 0) {
          if (strncmp(p, "FILE ", 5) == 0 || strncmp(p, "FILE2 ", 6) == 0) cap_files++;
          p = strchr(p, '\n');
          if (p != NULL) p++;
        }
      }
      g->files = (HubFile *)hub_calloc(cap_files + 1, sizeof(HubFile));
      if (g->files == NULL) {
        snprintf(err, errlen, "No memory.");
        hub_catalog_free(cat);
        return NULL;
      }
    } else if (g == NULL) {
      if (strcmp(cmd, "WARPHUB-CATALOG") != 0) goto bad;
    } else if (strcmp(cmd, "END") == 0) {
      cat->ngames++;
      g = NULL;
    } else if ((strcmp(cmd, "NAME") == 0 || strcmp(cmd, "DESC") == 0 ||
                strcmp(cmd, "NOTES") == 0) && rest != NULL) {
      char *lang = word(&rest);
      const char **to = cmd[0] == 'N' && cmd[1] == 'A' ? g->name :
                        cmd[0] == 'D' ? g->desc : g->notes;
      if (rest == NULL) goto bad;
      to[hub_lang(lang)] = rest;
    } else if (strcmp(cmd, "REPO") == 0 && rest != NULL) {
      g->repo = rest;
    } else if (strcmp(cmd, "BRANCH") == 0 && rest != NULL) {
      g->branch = rest;
    } else if (strcmp(cmd, "REQ") == 0 && rest != NULL) {
      char *lang = word(&rest);
      int l = hub_lang(lang);
      if (rest == NULL) goto bad;
      if (g->nreq[l] < HUB_MAX_REQ) g->req[l][g->nreq[l]++] = rest;
    } else if (strcmp(cmd, "TAGS") == 0 && rest != NULL) {
      g->tags = rest;
    } else if (strcmp(cmd, "FLAT") == 0) {
      g->flat = 1;
    } else if (strcmp(cmd, "SOURCE") == 0 && rest != NULL) {
      g->local = strcmp(rest, "local") == 0;
      g->priv = strcmp(rest, "private") == 0;
    } else if (strcmp(cmd, "VERSION") == 0 && rest != NULL) {
      g->version = rest;
    } else if (strcmp(cmd, "ZIP") == 0 && rest != NULL) {
      g->zip = word(&rest);
      if (rest == NULL) goto bad;
      g->zip_size = strtoul(rest, NULL, 10);
    } else if (strcmp(cmd, "SIZE") == 0 && rest != NULL) {
      g->size = strtoul(rest, NULL, 10);
    } else if (strcmp(cmd, "IMAGE") == 0 && rest != NULL) {
      char *size = word(&rest), *sha = word(&rest);
      if (rest == NULL || strlen(sha) != 64 || !hub_path_ok(rest)) goto bad;
      g->image_size = strtoul(size, NULL, 10);
      g->image_sha = sha;
      g->image = rest;
    } else if (strcmp(cmd, "README") == 0 && rest != NULL) {
      if (g->readmes < HUB_MAX_README && hub_path_ok(rest)) g->readme[g->readmes++] = rest;
    } else if ((strcmp(cmd, "FILE") == 0 || strcmp(cmd, "FILE2") == 0) && rest != NULL) {
      /* FILE2: a path with '&' (0.18), which 0.17 and older refuse in a FILE line */
      HubFile *f;
      char *size = word(&rest), *sha = word(&rest), *flag = word(&rest);
      if (rest == NULL || g->nfiles >= cap_files || strlen(sha) != 64 ||
          flag == NULL || flag[0] == 0) {
        goto bad;
      }
      if (!hub_path_ok(rest)) continue;   /* a name this version does not take: skipped, not the whole catalog */
      f = &g->files[g->nfiles++];
      f->size = strtoul(size, NULL, 10);
      f->sha = sha;
      /* N, K; anything else (X, or a flag of a newer catalog): not installed */
      f->flag = (flag[0] == 'N' || flag[0] == 'K') ? flag[0] : 'X';
      f->path = rest;
    }
    /* unknown lines: newer format items, skipped */
  }
  if (g != NULL) goto bad;
  {
    int i;
    for (i = 0; i < cat->ngames; i++) {
      HubGame *x = &cat->games[i];
      if (x->repo == NULL || x->branch == NULL || x->version == NULL ||
          x->name[HUB_EN] == NULL || x->nfiles == 0) {
        snprintf(err, errlen, "Catalog: game %s incomplete.", x->key);
        hub_catalog_free(cat);
        return NULL;
      }
    }
  }
  {
    /* the same game twice (the private catalog after the public one): the
     * later one takes the place of the earlier one */
    int i, j;
    for (j = 1; j < cat->ngames; j++) {
      for (i = 0; i < j; i++) {
        if (strcmp(cat->games[i].key, cat->games[j].key) == 0) {
          hub_free(cat->games[i].files);
          cat->games[i] = cat->games[j];
          memmove(&cat->games[j], &cat->games[j + 1],
                  (cat->ngames - j - 1) * sizeof(HubGame));
          cat->ngames--;
          j--;
          break;
        }
      }
    }
  }
  (void)lines;
  return cat;

bad:
  snprintf(err, errlen, "Catalog: bad line %d.", lineno);
  hub_catalog_free(cat);
  return NULL;
}

int hub_guide_files(HubGame *g, HubFile **out, int max)
{
  int i, n = 0;
  for (i = 0; i < g->nfiles && max > 0; i++) {
    const char *s = strchr(g->files[i].path, '/');
    if (s != NULL && strcmp(s, "/doc/Install.txt") == 0) {
      out[0] = &g->files[i];
      return 1;
    }
  }
  for (i = 0; i < g->readmes && n < max; i++) {
    HubFile *f = hub_game_file(g, g->readme[i]);
    if (f != NULL) out[n++] = f;
  }
  return n;
}
