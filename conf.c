/*
    Copyright 2015 by Clemson University

    This file is part of snmpiostat.

    Snmpiostat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    snmpiostat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public License
    along with snmpiostat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "conf.h"

#define MAXNAME	32
#define MAXVAL	256
#define BUFLEN	(MAXNAME+MAXVAL+10)

#define DEF_BASEOID	".1.3.6.1.3.2"	/* Default baseoid	*/

typedef struct _cfoption {
  char			name[MAXNAME];
  char			val[MAXVAL];
  struct _cfoption	*next;
} CFOPTION;

static CFOPTION	*cf_options = NULL;

/*
 * Read config file, return TRUE if successful, FALSE if not (errno set).  Builds list of
 * name/value pairs (queryable with cf_get()).
 */
int
cf_read(char *fn)

{
  FILE		*f;
  char		buf[BUFLEN];
  char		*name;
  char		*val;
  CFOPTION	*cfo;

  if ((f = fopen(fn, "r")) == NULL)
    return 0;
  cf_free();
  while (fgets(buf, BUFLEN, f)) {
    if (*buf == '#' || *buf == '\n')
      continue;
    if (name = strtok(buf, " \t\n")) {
      if (val = strtok(NULL, " \t\n")) {
	if (!(cfo = (CFOPTION *) malloc(sizeof(CFOPTION))))
	  return 0;
	strncpy(cfo->name, name, MAXNAME);
	strncpy(cfo->val, val, MAXVAL);
	cfo->next = cf_options;
	cf_options = cfo;
      }
    }
  }
  fclose(f);
}

/*
 * Get config value for name.  If not found check if have default value,
 * else return NULL.
 */
char *
cf_get(char *name)

{
  CFOPTION	*cfo;

  for (cfo = cf_options; cfo; cfo = cfo->next)
    if (strcmp(name, cfo->name) == 0)
      return cfo->val;

  /* If didn't find value see if have default	*/
  if (strcmp(name, "baseoid") == 0)
    return DEF_BASEOID;

  return NULL;
}

/*
 * Free list of config options created by cf_read().
 */
void
cf_free()

{
  CFOPTION	*cfo;

  while (cf_options) {
    cfo = cf_options;
    cf_options = cf_options->next;
    free(cfo);
  }
}
