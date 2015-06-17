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

#include "config.h"

#if HAVE_NET_SNMP_NET_SNMP_CONFIG_H == 0
#error "Can't find NET-SNMP header files."
#else

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/utilities.h>
#include <net-snmp/net-snmp-includes.h>
#include "conf.h"

typedef	unsigned long long	u_longlong;

#define MAXDEVICES			1024

#define TYPE_UNKNOWN			0
#define TYPE_IDEDISK			1
#define TYPE_SCSIDISK			2
#define TYPE_DEVMAPPER			3

/*
 * From DEVIOSTAT.MIB, index in devIOStatEntry for OIDs of interest.
 */
#define OI_DEVIOSTATMAJOR		2
#define OI_DEVIOSTATMINOR		3
#define OI_DEVIOSTATNAME		4
#define OI_DEVIOSTATREADS		5
#define OI_DEVIOSTATREADMERGES		6
#define OI_DEVIOSTATREADSECTORS		7
#define OI_DEVIOSTATREADTICKS		8
#define OI_DEVIOSTATWRITES		9
#define OI_DEVIOSTATWRITEMERGES		10
#define OI_DEVIOSTATWRITESECTORS	11
#define OI_DEVIOSTATWRITETICKS		12
#define OI_DEVIOSTATTICKS		13
#define OI_DEVIOSTATAVEQ		14
#define OI_DEVIOSTATTYPE		15
#define MAXDEVIOSTATOIDS		(OI_DEVIOSTATTYPE + 1)

/*
 * Host resource OIDs from HOST-RESOURCES-MIB.
 */
static oid	hrsysuptime[] = { 1, 3, 6, 1, 2, 1, 25, 1, 1, 0 };
static oid	hrdevicetype[] = { 1, 3, 6, 1, 2, 1, 25, 3, 2, 1, 2 };
static oid	hrdeviceprocessor[] = { 1, 3, 6, 1, 2, 1, 25, 3, 1, 3 };

/*
 * DEVIOSTAT OIDs.  Wanted to make baseoid configurable (unless/until this becomes
 * standard) so they're built from the config option baseoid by makedeviostatoids().
 */
static oid	deviostatoid[MAXDEVIOSTATOIDS][MAX_OID_LEN];
static int	deviostatoidlen;

typedef struct {
  int		major;
  int		minor;
  char		name[32];
  u_int		reads;
  u_int		readmerges;
  u_longlong	readsectors;
  u_int		readticks;
  u_int		writes;
  u_int		writemerges;
  u_longlong	writesectors;
  u_int		writeticks;
  u_int		ticks;
  u_int		aveq;
  u_int		type;
} deviostat_t;

static int		showdevice = 1;
static int		showpartition = 0;
static int		extendedstats = 0;
static int		maxidx = 0;
static oid		baseoid[MAX_OID_LEN];
static size_t		baselen = MAX_OID_LEN;

static void		optproc(int argc, char * const * argv, int opt);
static int		processmajor(netsnmp_variable_list *vars, void *data);
static int		processminor(netsnmp_variable_list *vars, void *data);
static int		processname(netsnmp_variable_list *vars, void *data);
static int		processtype(netsnmp_variable_list *vars, void *data);
static int		getinteger(netsnmp_session *ss, oid *o, u_int oidlen);
static void		getstring(netsnmp_session *ss, int idx, oid *o, u_int oidlen, char *result);
static void		getstats(netsnmp_session *ss, int idx, deviostat_t *deviostat);
static void		walk(netsnmp_session *ss, oid *o, u_int oidlen,
			     int (*process)(netsnmp_variable_list *vars, void *data), void *data);
static int		showit(deviostat_t *deviostat, char *watchdev[MAXDEVICES], int watchidx);
static void		makedeviostatoids();
static int		oidcpy(oid *dstoid, oid *srcoid, u_int len);
static u_longlong	stoll(u_char *s, int len);
static void		usage();
static void		myexit(int s, netsnmp_session *ss);

int
main(int argc, char **argv)

{
  int			i;
  netsnmp_session	session;
  netsnmp_session	*ss;
  double		deltams;
  deviostat_t		deviostat[MAXDEVICES];
  deviostat_t		cdeviostat;
  int			showidx[MAXDEVICES];
  int			ndev = 0;
  int			interval = 0;
  int			count = -1;
  double		ios;
  u_longlong		sectors;
  double		ticks;
  double		busy;
  u_long		uptime;
  u_long		ouptime = 0;
  char			*watchdev[MAXDEVICES];
  int			watchidx = 0;

  switch (i = snmp_parse_args(argc, argv, &session, "C:", optproc)) {
  case NETSNMP_PARSE_ARGS_ERROR:
    exit(1);
  case NETSNMP_PARSE_ARGS_SUCCESS_EXIT:
    exit(0);
  case NETSNMP_PARSE_ARGS_ERROR_USAGE:
    usage();
  }
  while (i < argc && !isdigit(*argv[i])) {
    watchdev[watchidx++] = argv[i];
    i++;
  }
  if (i < argc) {
    interval = atoi(argv[i]);
    if (i < argc - 1)
      count = atoi(argv[i+1]);
  }

  cf_read(CONFFILE);
  if (snmp_parse_oid(cf_get("baseoid"), baseoid, &baselen) == NULL) {
    snmp_perror(cf_get("baseoid"));
    exit(1);
  }

  makedeviostatoids();

  if ((ss = snmp_open(&session))== NULL) {
    snmp_sess_perror(argv[0], &session);
    myexit(1, ss);
  }

  walk(ss, deviostatoid[OI_DEVIOSTATMAJOR], deviostatoidlen, processmajor, deviostat);
  walk(ss, deviostatoid[OI_DEVIOSTATMINOR], deviostatoidlen, processminor, deviostat);
  walk(ss, deviostatoid[OI_DEVIOSTATNAME], deviostatoidlen, processname, deviostat);
  walk(ss, deviostatoid[OI_DEVIOSTATTYPE], deviostatoidlen, processtype, deviostat);
  for (i = 1; i <= maxidx; i++) {
    if (showit(&deviostat[i], watchdev, watchidx)) {
      showidx[ndev++] = i;
      deviostat[i].reads = 0;
      deviostat[i].readmerges = 0;
      deviostat[i].readsectors = 0;
      deviostat[i].readticks = 0;
      deviostat[i].writes = 0;
      deviostat[i].writemerges = 0;
      deviostat[i].writesectors = 0;
      deviostat[i].writeticks = 0;
      deviostat[i].ticks = 0;
      deviostat[i].aveq = 0;
    }
  }

  while (1) {
    if (extendedstats)
      printf("\n%-7s %6s %6s %6s %6s %9s %9s %8s %8s %6s %6s %6s\n", "Device:", "rrqm/s", "wrqm/s", "r/s",
	     "w/s", "rkB/s", "wkB/s", "avgrq-sz", "avgqu-sz", "await", "svctm", "%util");
    else
      printf("\n%-7s %9s %9s %8s %10s %10s\n", "Device:", "tps", "kB_read/s", "kBwrtn/s", "kB_read", "kB_wrtn");

    uptime = getinteger(ss, hrsysuptime, sizeof(hrsysuptime) / sizeof(oid));
    deltams = 1000.0 * (uptime - ouptime) / HZ;
    ouptime = uptime;

    for (i = 0; i < ndev; i++) {
      getstats(ss, showidx[i], &cdeviostat);
      if (cdeviostat.reads + cdeviostat.writes == 0)
	continue;

      ios = cdeviostat.reads + cdeviostat.writes - deviostat[showidx[i]].reads - deviostat[showidx[i]].writes;
      sectors = cdeviostat.readsectors - deviostat[showidx[i]].readsectors +
		cdeviostat.writesectors - deviostat[showidx[i]].writesectors;
      ticks = cdeviostat.readticks + cdeviostat.writeticks -
	      deviostat[showidx[i]].readticks - deviostat[showidx[i]].writeticks;
      if ((busy = 100.0 * (cdeviostat.ticks - deviostat[showidx[i]].ticks) / deltams) > 100.0)
	busy = 100.0;

      if (extendedstats) {
	printf("%-7s %6.2f %6.2f %6.2f %6.2f %9.2f %9.2f %8.2f %8.2f %6.2f %6.2f %6.2f\n",
	       deviostat[showidx[i]].name,
	       1000.0 * (cdeviostat.readmerges - deviostat[showidx[i]].readmerges) / deltams,
	       1000.0 * (cdeviostat.writemerges - deviostat[showidx[i]].writemerges) / deltams,
	       1000.0 * (cdeviostat.reads - deviostat[showidx[i]].reads) / deltams,
	       1000.0 * (cdeviostat.writes - deviostat[showidx[i]].writes) / deltams,
	       1000.0 * (cdeviostat.readsectors - deviostat[showidx[i]].readsectors) / deltams / 2.0,
	       1000.0 * (cdeviostat.writesectors - deviostat[showidx[i]].writesectors) / deltams / 2.0,
	       ios ?  (double) sectors / ios : 0.0,
	       (cdeviostat.aveq - deviostat[showidx[i]].aveq) / deltams,
	       ios ? ticks / ios : 0.0,
	       ios ? (cdeviostat.ticks - deviostat[showidx[i]].ticks) / ios : 0.0,
	       busy);
      } else {
	printf("%-7s %9.2f %9.2f %8.2f %10llu %10llu\n",
	       deviostat[showidx[i]].name,
	       ios ? 1000.0 * ios / deltams : 0.0,
	       1000.0 * (cdeviostat.readsectors - deviostat[showidx[i]].readsectors) / deltams / 2.0,
	       1000.0 * (cdeviostat.writesectors - deviostat[showidx[i]].writesectors) / deltams / 2.0,
	       (cdeviostat.readsectors - deviostat[showidx[i]].readsectors) / 2,
	       (cdeviostat.writesectors - deviostat[showidx[i]].writesectors) / 2);
      }
      deviostat[showidx[i]].reads = cdeviostat.reads;
      deviostat[showidx[i]].readmerges = cdeviostat.readmerges;
      deviostat[showidx[i]].readsectors = cdeviostat.readsectors;
      deviostat[showidx[i]].readticks = cdeviostat.readticks;
      deviostat[showidx[i]].writes = cdeviostat.writes;
      deviostat[showidx[i]].writemerges = cdeviostat.writemerges;
      deviostat[showidx[i]].writesectors = cdeviostat.writesectors;
      deviostat[showidx[i]].writeticks = cdeviostat.writeticks;
      deviostat[showidx[i]].ticks = cdeviostat.ticks;
      deviostat[showidx[i]].aveq = cdeviostat.aveq;
    }

    if (--count == 0)
      break;

    if (interval)
      sleep(interval);
    else
      break;
  }

  myexit(0, ss);
}

static void
optproc(int argc, char * const * argv, int opt)

{
  extern char	*optarg;

  switch (opt) {
  case 'C':
    while (*optarg) {
      switch (*optarg++) {
      case 'h':
	usage();
      case 'p':
	showpartition = 1;
	break;
      case 'v':
	printf("snmpiostat version: %s\n", VERSION);
	exit(0);
      case 'x':
	extendedstats = 1;
	break;
      }
    }
    break;
  default:
    usage();
  }
}

static int
processmajor(netsnmp_variable_list *vars, void *data)

{
  deviostat_t	*deviostat = (deviostat_t *) data;
  int		i;

  if ((i = vars->name[vars->name_length - 1]) < MAXDEVICES) {
    if (i > maxidx)
      maxidx = i;
    deviostat[i].major = *vars->val.integer;
  }
  return 1;
}

static int
processminor(netsnmp_variable_list *vars, void *data)

{
  deviostat_t	*deviostat = (deviostat_t *) data;
  int		i;

  if ((i = vars->name[vars->name_length - 1]) < MAXDEVICES)
    deviostat[i].minor = *vars->val.integer;
  return 1;
}

static int
processname(netsnmp_variable_list *vars, void *data)

{
  deviostat_t	*deviostat = (deviostat_t *) data;
  int		i;

  if ((i = vars->name[vars->name_length - 1]) < MAXDEVICES) {
    strncpy(deviostat[i].name, (const char *) vars->val.string, vars->val_len);
    deviostat[i].name[vars->val_len] = '\0';
  }
  return 1;
}

static int
processtype(netsnmp_variable_list *vars, void *data)

{
  deviostat_t	*deviostat = (deviostat_t *) data;
  int		i;

  if ((i = vars->name[vars->name_length - 1]) < MAXDEVICES)
    deviostat[i].type = *vars->val.integer;
  return 1;
}

static int
getinteger(netsnmp_session *ss, oid *o, u_int oidlen)

{
  netsnmp_pdu		*pdu;
  netsnmp_pdu		*response;
  netsnmp_variable_list	*vars;
  int			s;
  oid			to[MAX_OID_LEN];
  int			tolen;
  int			result;

  pdu = snmp_pdu_create(SNMP_MSG_GET);
  snmp_add_null_var(pdu, o, oidlen);
  s = snmp_synch_response(ss, pdu, &response);
  if (s == STAT_SUCCESS) {
    if (response->errstat == SNMP_ERR_NOERROR) {
      result = *response->variables->val.integer;
      snmp_free_pdu(response);
      return result;
    }
    fprintf(stderr, "getstring: %s\n", snmp_errstring(response->errstat));
  } else if (s == STAT_TIMEOUT)
    fprintf(stderr, "getstring: Timeout: No response from %s\n", ss->peername);
  else
    snmp_sess_perror("getstring", ss);

  if (response)
    snmp_free_pdu(response);
  myexit(1, ss);
}

static void
getstring(netsnmp_session *ss, int idx, oid *o, u_int oidlen, char *result)

{
  netsnmp_pdu		*pdu;
  netsnmp_pdu		*response;
  netsnmp_variable_list	*vars;
  int			s;
  oid			to[MAX_OID_LEN];
  int			tolen;

  pdu = snmp_pdu_create(SNMP_MSG_GET);
  tolen = oidcpy(to, o, oidlen);
  to[tolen++] = idx;
  snmp_add_null_var(pdu, to, tolen);
  s = snmp_synch_response(ss, pdu, &response);
  if (s == STAT_SUCCESS) {
    if (response->errstat == SNMP_ERR_NOERROR) {
      strncpy(result, (const char *) response->variables->val.string, response->variables->val_len);
      result[response->variables->val_len] = '\0';
      snmp_free_pdu(response);
      return;
    }
    fprintf(stderr, "getstring: %s\n", snmp_errstring(response->errstat));
  } else if (s == STAT_TIMEOUT)
    fprintf(stderr, "getstring: Timeout: No response from %s\n", ss->peername);
  else
    snmp_sess_perror("getstring", ss);

  if (response)
    snmp_free_pdu(response);
  myexit(1, ss);
}

static void
getstats(netsnmp_session *ss, int idx, deviostat_t *deviostat)

{
  netsnmp_pdu		*pdu;
  netsnmp_pdu		*response;
  netsnmp_variable_list	*vars;
  int			s;
  int			i;

  pdu = snmp_pdu_create(SNMP_MSG_GET);
  for (i = OI_DEVIOSTATREADS; i <= OI_DEVIOSTATAVEQ; i++) {
    deviostatoid[i][deviostatoidlen] = idx;
    snmp_add_null_var(pdu, deviostatoid[i], deviostatoidlen + 1);
  }
  s = snmp_synch_response(ss, pdu, &response);
  if (s == STAT_SUCCESS) {
    if (response->errstat == SNMP_ERR_NOERROR) {
      for (vars = response->variables; vars; vars = vars->next_variable) {
	switch (vars->name[vars->name_length - 2]) {
	case OI_DEVIOSTATREADS:
	  deviostat->reads = *vars->val.integer;
	  break;
	case OI_DEVIOSTATREADMERGES:
	  deviostat->readmerges = *vars->val.integer;
	  break;
	case OI_DEVIOSTATREADSECTORS:
	  deviostat->readsectors = stoll(vars->val.string, vars->val_len);
	  break;
	case OI_DEVIOSTATREADTICKS:
	  deviostat->readticks = *vars->val.integer;
	  break;
	case OI_DEVIOSTATWRITES:
	  deviostat->writes = *vars->val.integer;
	  break;
	case OI_DEVIOSTATWRITEMERGES:
	  deviostat->writemerges = *vars->val.integer;
	  break;
	case OI_DEVIOSTATWRITESECTORS:
	  deviostat->writesectors = stoll(vars->val.string, vars->val_len);
	  break;
	case OI_DEVIOSTATWRITETICKS:
	  deviostat->writeticks = *vars->val.integer;
	  break;
	case OI_DEVIOSTATTICKS:
	  deviostat->ticks = *vars->val.integer;
	  break;
	case OI_DEVIOSTATAVEQ:
	  deviostat->aveq = *vars->val.integer;
	  break;
	}
      }
      snmp_free_pdu(response);
      return;
    }
    fprintf(stderr, "getstats: %s\n", snmp_errstring(response->errstat));
  } else if (s == STAT_TIMEOUT)
    fprintf(stderr, "getstats: Timeout: No response from %s\n", ss->peername);
  else
    snmp_sess_perror("getstats", ss);

  if (response)
    snmp_free_pdu(response);
  myexit(1, ss);
}

/*
 * Walk MIB from specified OID, call callback function process() with each result
 * and pointer to data.  Abort if callback returns FALSE.
 */
static void
walk(netsnmp_session *ss, oid *o, u_int oidlen, int (*process)(netsnmp_variable_list *vars, void *data), void *data)

{
  netsnmp_pdu		*pdu;
  netsnmp_pdu		*response;
  netsnmp_variable_list	*vars;
  int			s;
  int			count;
  oid			var[MAX_OID_LEN];
  int			varlen;
  oid			end[MAX_OID_LEN];
  int			endlen;

  /*
   * Initialize pdu.
   */
  varlen = oidcpy(var, o, oidlen);
  endlen = oidcpy(end, o, oidlen);
  end[endlen-1]++;
  while (1) {
    pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
    snmp_add_null_var(pdu, var, varlen);
    s = snmp_synch_response(ss, pdu, &response);
    if (s == STAT_SUCCESS) {
      if (response->errstat == SNMP_ERR_NOERROR) {
	for (vars = response->variables; vars; vars = vars->next_variable) {
	  if (snmp_oid_compare(end, endlen, vars->name, vars->name_length) <= 0) {
	    snmp_free_pdu(response);
	    return;
	  }
	  if (!process(vars, data)) {
	    snmp_free_pdu(response);
	    return;
	  }
	  if (vars->type == SNMP_ENDOFMIBVIEW ||
	      vars->type == SNMP_NOSUCHOBJECT ||
	      vars->type == SNMP_NOSUCHINSTANCE) {
	    snmp_free_pdu(response);
	    return;
	  }
	  varlen = oidcpy(var, vars->name, vars->name_length);
	}
	snmp_free_pdu(response);
      } else {				/* s != STAT_SUCCESS		*/
	if (response->errstat == SNMP_ERR_NOSUCHNAME) {
	  snmp_free_pdu(response);
	  return;			/* End of MIB			*/
	}
	fprintf(stderr, "walk: %s\n", snmp_errstring(response->errstat));
	if (response->errindex != 0) {
	  fprintf(stderr, "Failed object: ");
	  count = 1;
	  for (vars = response->variables; vars; vars = vars->next_variable, count++)
	    if (count++ == response->errindex)
	      break;
	  if (vars)
	    fprint_objid(stderr, vars->name, vars->name_length);
	  fprintf(stderr, "\n");
	}
	goto error;
      }
    } else if (s == STAT_TIMEOUT) {
      fprintf(stderr, "Timeout: No response from %s\n", ss->peername);
      goto error;
    } else {
      snmp_sess_perror("walk", ss);
      goto error;
    }
  }

  /* Something went wrong, cleanup and die	*/
 error:
  if (response)
    snmp_free_pdu(response);
  myexit(1, ss);
}

static int
showit(deviostat_t *deviostat, char *watchdev[MAXDEVICES], int watchidx)

{
  int	i;
  int	len;

  if (watchidx) {
    for (i = 0; i < watchidx; i++) {
      if (showpartition) {
	len = strlen(watchdev[i]);
	if (strncmp(watchdev[i], deviostat->name, len) == 0)
	  if (deviostat->name[len] == '\0' || isdigit(deviostat->name[len]))
	    return 1;
      } else if (strcmp(watchdev[i], deviostat->name) == 0)
	return 1;
    }
    return 0;
  }

  if (deviostat->type == TYPE_IDEDISK)
    return (!(deviostat->minor & 0x3F) && showdevice) || ((deviostat->minor & 0x3F) && showpartition);
  else if (deviostat->type == TYPE_SCSIDISK)
    return (!(deviostat->minor & 0x0F) && showdevice) || ((deviostat->minor & 0x0F) && showpartition);
  else if (deviostat->type == TYPE_DEVMAPPER)
    return 1;

  return 0;
}

/*
 * DEVIOSTAT.MIB oids of interest to this program are:
 *
 *    major		baseoid.1.1.2
 *    minor		baseoid.1.1.3
 *    name		baseoid.1.1.4
 *    reads		baseoid.1.1.5
 *    readmerges	baseoid.1.1.6
 *    readsectors	baseoid.1.1.7
 *    readticks		baseoid.1.1.8
 *    writes		baseoid.1.1.9
 *    writemerges	baseoid.1.1.10
 *    writesectors	baseoid.1.1.11
 *    writeticks	baseoid.1.1.12
 *    ticks		baseoid.1.1.13
 *    aveq		baseoid.1.1.14
 *    type		baseoid.1.1.15
 *
 * makedeviostatoids() creates these oids from the configured baseoid.
 */
static void
makedeviostatoids()

{
  int	i;

  for (i = OI_DEVIOSTATMAJOR; i < MAXDEVIOSTATOIDS; i++) {
    deviostatoidlen = oidcpy(deviostatoid[i], baseoid, baselen);
    deviostatoid[i][deviostatoidlen++] = 1;
    deviostatoid[i][deviostatoidlen++] = 1;
    deviostatoid[i][deviostatoidlen++] = i;
  }
}

static int
oidcpy(oid *dstoid, oid *srcoid, u_int len)

{
  int	i;

  for (i = 0; i < len; i++)
    dstoid[i] = srcoid[i];
  return len;
}

/*
 * Strings returned by snmp are not null terminated, can't use atoll() to convert to integer.
 */
static u_longlong
stoll(u_char *s, int len)

{
  int		i;
  u_longlong	r = 0;

  for (i = 0; i < len && s[i]; i++)
    r = r * 10 + s[i] - '0';
  return r;
}

static void
usage()

{
  fprintf(stderr, "Usage: snmpiostat [snmp-options] [-Ch] [-Cp] [-Cv] [-Cx]agent [device ...] [interval [count]]\n");
  myexit(1, NULL);
}

static void
myexit(int s, netsnmp_session *ss)

{
  if (ss)
    snmp_close(ss);
  SOCK_CLEANUP;
  exit(s);
}

#endif	/* HAVE_NET_SNMP_NET_SNMP_CONFIG_H	*/
