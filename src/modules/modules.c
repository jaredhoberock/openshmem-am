/* (c) 2011 University of Houston.  All rights reserved. */


#include <stdio.h>
#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#include "trace.h"
#include "modules.h"

/*
 * read in the config file and set up tables
 *
 */

#include "uthash.h"

typedef struct {
  char *name;                   /* module = key */

  int lineno;                   /* line in the config file */
  char *impl;                   /* name of implementation */

  UT_hash_handle hh;

} module_table_t;

static module_table_t *mtp = NULL;

/*
 * get rid of newline
 */
#define CHOMP(L) (L)[strlen(L) - 1] = '\0'

static void
create_module_table_from_config(char *cfg_file)
{
  FILE *fp;
  char *modname;
  char *modimpl;
  char *hash;
  int lineno = 0;
  const char *delims = "\t ";
  char line[PATH_MAX];
  module_table_t *mp = NULL;

  fp = fopen(cfg_file, "r");
  if (fp == (FILE *) NULL) {
    return;
  }

  __shmem_trace(SHMEM_LOG_MODULES,
		"about to read modules from %s",
		cfg_file
		);

  while (fgets(line, sizeof(line), fp) != NULL) {
    lineno += 1;

    /*
     * trim blank lines and comments
     */
    CHOMP(line);
    hash = strchr(line, '#');
    if (hash != NULL) {
      *hash = '\0';
    }

    modname = strtok(line, delims);
    if (modname == NULL) {
      continue;
    }

    modimpl = strtok(NULL, delims);
    if (modimpl == NULL) {
      __shmem_trace(SHMEM_LOG_MODULES,
		    "no implementation for module \"%s\" in \"%s\" at line %d, "
		    "will use default",
		    modname,
		    cfg_file,
		    lineno
		    );
      continue;
    }

    mp = (module_table_t *) malloc(sizeof(*mp));
    if (mp == (module_table_t *) NULL) {
      __shmem_trace(SHMEM_LOG_FATAL,
		    "internal error: unable to allocate memory for module table"
		    );
      /* NOT REACHED */
    }
    mp->lineno = lineno;
    mp->name = strdup(modname);
    mp->impl = strdup(modimpl);

    HASH_ADD_PTR(mtp, name, mp);

    __shmem_trace(SHMEM_LOG_MODULES,
		  "module %s, implementation %s at line %d in %s",
		  modname,
		  modimpl,
		  lineno,
		  cfg_file
		  );
  }

  (void) fclose(fp);
}

static void
free_module_table(void)
{
  module_table_t *current;
  module_table_t *tmp;

  HASH_ITER(hh, mtp, current, tmp) {
    HASH_DEL(mtp, current);
    free(current);
  }
}

/*
 * retrieve implementation for a given module
 *
 */
char *
__shmem_modules_get_implementation(char *mod)
{
  module_table_t *match;

  HASH_FIND_PTR(mtp, mod, match);
  if (match == (module_table_t *) NULL) {
    HASH_FIND_PTR(mtp, "default", match);
    if (match == (module_table_t *) NULL) {
      return NULL;
    }
  }

  return match->impl;
}

/*
 * initialize modules: read from global config file if it exists
 *
 */

void
__shmem_modules_init(void)
{
  char path_to_cfg[PATH_MAX];

  snprintf(path_to_cfg, PATH_MAX,
	   "%s/config",
	   INSTALLED_MODULES_DIR
	   );

  create_module_table_from_config(path_to_cfg);
}

/*
 * shut modules down: clean up hash table
 *
 */

void
__shmem_modules_finalize(void)
{
  free_module_table();
}

/*
 * TODO: currently keeping .so file open during run,
 * should really clean up
 */

int
__shmem_modules_load(const char *group, char *name, module_info_t *mip)
{
  void *mh;
  module_info_t *rh;
  char path_to_so[PATH_MAX];

  if (group == (char *) NULL) {
    return -1;
  }
  if (name == (char *) NULL) {
    return -1;
  }
  if (mip == (module_info_t *) NULL) {
    return -1;
  }

  snprintf(path_to_so, PATH_MAX, "%s/%s-%s.so",
           INSTALLED_MODULES_DIR, group, name);

  mh = dlopen(path_to_so, RTLD_LAZY);
  if (mh == NULL) {
    __shmem_trace(SHMEM_LOG_AUTH,
		  "internal error: couldn't open shared library \"%s\" (%s)",
		  path_to_so,
		  dlerror()
		  );
    return -1;
  }

  rh = (module_info_t *) dlsym(mh, "module_info");
  if (rh == NULL) {
    __shmem_trace(SHMEM_LOG_AUTH,
		  "internal error: couldn't find module_info symbol in \"%s\" (%s)",
		  path_to_so,
		  dlerror()
		  );
    return -1;
  }

  (void) memcpy(mip, rh, sizeof(*mip));

  /* (void) dlclose(mh); */

  return 0;
}
