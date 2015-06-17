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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>
#include <syslog.h>
#include <stdarg.h>
#if HAVE_LINUX_MAJOR_H == 1
#include <linux/major.h>
#endif
#include "conf.h"

typedef	unsigned long long	u_longlong;

#if HAVE__PROC_DISKSTATS == 1
#define PROCDISKSTATS		"/proc/diskstats"
#define HAVE_KSTAT		0	/* Don't use kstat if have /proc/diskstats	*/
#endif

#if HAVE__PROC_DEVICES == 1
#define DEVICES			"/proc/devices"
#define DEFAULT_DEVMAP_MAJOR	253
#endif

#define BUFLEN		1024
#define MAXOID		128
#define MAXDEVICES	1024
#define MAXDEVNAME	32

/* From DEVIOSTAT.MIB devIOStatType	*/
#define TYPE_UNKNOWN	0
#define TYPE_IDEDISK	1
#define TYPE_SCSIDISK	2
#define TYPE_DEVMAPPER	3

#define MIB_NUMENTRY	15	/* Must match number of fields in mib:devIOStatEntry	*/

#define OP_GET		0
#define OP_NEXT		1

#ifdef linux
#ifndef IDE_DISK_MAJOR
#define IDE_DISK_MAJOR(m) ((m) == IDE0_MAJOR || (m) == IDE1_MAJOR || \
			   (m) == IDE2_MAJOR || (m) == IDE3_MAJOR || \
			   (m) == IDE4_MAJOR || (m) == IDE5_MAJOR || \
			   (m) == IDE6_MAJOR || (m) == IDE7_MAJOR || \
			   (m) == IDE8_MAJOR || (m) == IDE9_MAJOR)
#endif	/* !IDE_DISK_MAJOR */

#ifndef SCSI_DISK_MAJOR
#ifndef SCSI_DISK8_MAJOR
#define SCSI_DISK8_MAJOR 128
#endif
#ifndef SCSI_DISK15_MAJOR
#define SCSI_DISK15_MAJOR 135
#endif
#define SCSI_DISK_MAJOR(m) ((m) == SCSI_DISK0_MAJOR || \
			   ((m) >= SCSI_DISK1_MAJOR && \
			    (m) <= SCSI_DISK7_MAJOR) || \
			   ((m) >= SCSI_DISK8_MAJOR && \
			    (m) <= SCSI_DISK15_MAJOR))
#endif	/* !SCSI_DISK_MAJOR */
#endif

typedef struct {
  u_int		o[MAXOID];
  int		len;
} oid_t;

typedef struct {
  u_int		major;
  u_int		minor;
  char		name[MAXDEVNAME];
  u_int		r_ios;
  u_int		r_merges;
  u_longlong	r_sectors;
  u_int		r_ticks;
  u_int		w_ios;
  u_int		w_merges;
  u_longlong	w_sectors;
  u_int		w_ticks;
  u_int		ticks;
  u_int		aveq;
  u_int		type;
} devio_t;

static int	debug = 0;
static int	syslogfacility = LOG_DAEMON;
static int	syslogging;				/* TRUE if using syslog, else using stdout	*/ 
static int	persist;
static oid_t	baseoid;				/* From config file, must match DEVIOSTAT.MIB	*/
static void	s2oid(char *s, oid_t *oid);
static char	*oid2s(oid_t *oid, char *s, int slen);
static void	dorequest(int op, char *soid);
static int	getnextoid(oid_t *oid, oid_t *nextoid, int ndev);
static void	showoidvalue(oid_t *oid, int ndev, devio_t *devio);
static int	oidmatch(oid_t *oid1, oid_t *oid2);
static oid_t	*oidcpy(oid_t *oiddst, oid_t *oidsrc);
static int	getdevio(devio_t *devio);
#if HAVE__PROC_DEVICES == 1
static int	dm_major;				/* Major number of dev mapper (multipath devs)	*/
static u_int	devmap_major();
#endif
static int	s2syslogfacility(char *s);
static void	logit(int level, char *fmt, ...);
static void	usage(char *prog);

main(int argc, char **argv)

{
  int		i;
  int		op;
  char		*soid = NULL;
  oid_t		oid;
  char		buf[BUFLEN];
  char		*s;

  while ((i = getopt(argc, argv, "dg:n:v")) != -1)
    switch (i) {
    case 'd':
      debug++;
      break;
    case 'g':
      op = OP_GET;
      soid = optarg;
      break;
    case 'n':
      op = OP_NEXT;
      soid = optarg;
      break;
    case 'v':
      printf("snmpiostatagent version: %s\n", VERSION);
      exit(0);
    default:
      usage(argv[0]);
    }

  cf_read(CONFFILE);
  s2oid(cf_get("baseoid"), &baseoid);
  if (s = cf_get("debug"))
    debug = atoi(s);
  if (s = cf_get("syslog"))
    syslogfacility = s2syslogfacility(s);

  if (isatty(fileno(stdin)))
    syslogging = 0;
  else {
    syslogging = 1;
    openlog("snmpiostat", LOG_PID, syslogfacility);
  }

  if (debug) {
    sprintf(buf, "Started with arguments: ");
    for (i = 1; i < argc; i++)
      sprintf(&buf[strlen(buf)], "%s ", argv[i]);
    logit(LOG_DEBUG, buf);
  }

#if HAVE__PROC_DEVICES == 1
  dm_major = devmap_major();
#endif

  if (soid) {					/* Not in persist mode		*/
    persist = 0;
    dorequest(op, soid);
  } else {					/* Persist mode			*/
    persist = 1;
    while (fgets(buf, sizeof(buf), stdin)) {
      if (strncmp(buf, "PING", 4) == 0) {
	printf("PONG\n");
	fflush(stdout);
	if (debug)
	  logit(LOG_DEBUG, "Received PING, sent PONG");
	continue;
      } else if (strncmp(buf, "getnext", 7) == 0)
	op = OP_NEXT;
      else if (strncmp(buf, "get", 3) == 0)
	op = OP_GET;
      else {
	logit(LOG_INFO, "Unknown operation: %s", buf);
	continue;
      }
      if (fgets(buf, sizeof(buf), stdin))
	dorequest(op, buf);
      else
	logit(LOG_INFO, "No OID specified in request");
    }
  }

  if (syslogging)
    closelog();
  exit(0);

}

static void
dorequest(int op, char *soid)

{
  devio_t	devio[MAXDEVICES];
  int		ndev;
  oid_t		oid;
  oid_t		nextoid;
  char		buf[BUFLEN];

  s2oid(soid, &oid);
  if (debug)
    logit(LOG_DEBUG, "dorequest(%s, %s)", op == OP_NEXT ? "getnext" : "get", oid2s(&oid, buf, BUFLEN));
  ndev = getdevio(devio);
  if (op == OP_NEXT) {
    if (getnextoid(&oid, &nextoid, ndev))
      showoidvalue(&nextoid, ndev, devio);
    else if (persist) {
      printf("NONE\n");
      fflush(stdout);
    }
  } else if (op == OP_GET) {
    showoidvalue(&oid, ndev, devio);
  }
}

static int
getdevio(devio_t *devio)

{
  int	n = 0;

#if HAVE__PROC_DISKSTATS == 1
  FILE	*f;
  char	buf[BUFLEN];

  if (!(f = fopen(PROCDISKSTATS, "r"))) {
    fprintf(stderr, "Can't get I/O statistics: %s: %s\n", PROCDISKSTATS, strerror(errno));
    exit(1);
  }
  while (n < MAXDEVICES && fgets(buf, sizeof(buf), f)) {
    if (sscanf(buf, "%u %u %s %u %u %llu %u %u %u %llu %u %*u %u %u",
	       &devio[n].major, &devio[n].minor, devio[n].name, &devio[n].r_ios, &devio[n].r_merges,
	       &devio[n].r_sectors, &devio[n].r_ticks, &devio[n].w_ios, &devio[n].w_merges,
	       &devio[n].w_sectors, &devio[n].w_ticks, &devio[n].ticks, &devio[n].aveq)) {
      n++;
    }
  }
  fclose(f);
#endif

  return n;
}

/*
 * Return next oid and TRUE, or FALSE if at end of oids (or bad oid).
 */
static int
getnextoid(oid_t *oid, oid_t *nextoid, int ndev)

{
  char	buf[BUFLEN];

  oidcpy(nextoid, oid);
  if (oidmatch(oid, &baseoid)) {
    nextoid->o[nextoid->len++] = 1;
    nextoid->o[nextoid->len++] = 1;
    nextoid->o[nextoid->len++] = 1;
    nextoid->o[nextoid->len++] = 1;
  } else if (oid->len < baseoid.len + 4) {
    nextoid->o[nextoid->len++] = 1;
  } else {
    if (oid->o[oid->len-1] < ndev) {
      nextoid->o[oid->len-1]++;
    } else if (oid->o[oid->len-2] < MIB_NUMENTRY) {
      nextoid->o[oid->len-2]++;
      nextoid->o[oid->len-1] = 1;
    } else {
      if (debug)
	logit(LOG_DEBUG, "Reached end of OIDs: %s", oid2s(oid, buf, BUFLEN));
      return 0;
    }
  }
  return 1;
}

static void
showoidvalue(oid_t *oid, int ndev, devio_t *devio)

{
  char	buf[BUFLEN];

  if (oid->o[baseoid.len+3] > ndev)
    return;
  switch (oid->o[baseoid.len+2]) {
  case 1:	/* Index of each device		*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), oid->o[baseoid.len+3]);
    break;
  case 2:	/* Major number of each device	*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].major);
    break;
  case 3:	/* Minor number of each device	*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].minor);
    break;
  case 4:	/* Major number of each device	*/
    printf("%s\nstring\n%s\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].name);
    break;
  case 5:	/* Reads			*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].r_ios);
    break;
  case 6:	/* Read merges			*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].r_merges);
    break;
  case 7:	/* Sectors read			*/
    printf("%s\nstring\n%llu\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].r_sectors);
    break;
  case 8:	/* Time reading			*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].r_ticks);
    break;
  case 9:	/* Writes			*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].w_ios);
    break;
  case 10:	/* Write merges			*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].w_merges);
    break;
  case 11:	/* Sectors written		*/
    printf("%s\nstring\n%llu\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].w_sectors);
    break;
  case 12:	/* Time writing			*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].w_ticks);
    break;
  case 13:	/* Time spent doing IO		*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].ticks);
    break;
  case 14:	/* Weighted time doing IO	*/
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].aveq);
    break;
  case 15:	/* Device type			*/
#ifdef linux
    if (IDE_DISK_MAJOR(devio[oid->o[baseoid.len+3]-1].major))
      devio[oid->o[baseoid.len+3]-1].type = TYPE_IDEDISK;
    else if (SCSI_DISK_MAJOR(devio[oid->o[baseoid.len+3]-1].major))
      devio[oid->o[baseoid.len+3]-1].type = TYPE_SCSIDISK;
    else if (devio[oid->o[baseoid.len+3]-1].major == dm_major)
      devio[oid->o[baseoid.len+3]-1].type = TYPE_DEVMAPPER;
    else
      devio[oid->o[baseoid.len+3]-1].type = 0;
#endif
    printf("%s\ninteger\n%d\n", oid2s(oid, buf, BUFLEN), devio[oid->o[baseoid.len+3]-1].type);
    break;
  default:	/* Unknown OID			*/
    logit(LOG_INFO, "Unknown OID: %s", oid2s(oid, buf, BUFLEN));
    if (persist)
      printf("NONE\n");
  }
  fflush(stdout);
}

/*
 * Convert string of form ".a.b.c" to oid_t.
 */
static void
s2oid(char *s, oid_t *oid)

{
  char	*ss;

  oid->len = 0;
  ss = s;
  for (oid->len = 0; *s; oid->len++) {
    if (*s++ != '.')
      break;
    if (oid->len >= MAXOID) {
      logit(LOG_WARNING, "s2oid(%s): OID too long", ss);
      break;
    }
    oid->o[oid->len] = 0;
    while (isdigit(*s)) {
      oid->o[oid->len] = oid->o[oid->len] * 10 + *s - '0';
      s++;
    }
  }
}

/*
 * Convert oid_t to string of form .a.b.c
 */
static char *
oid2s(oid_t *oid, char *s, int slen)

{
  int	i;
  int	j;
  u_int	len = 0;
  char	buf[10];

  for (i = 0; i < oid->len; i++) {
    s[len++] = '.';
    sprintf(buf, "%u", oid->o[i]);
    for (j = 0; buf[j] && len + j < slen - 2; j++)
      s[len++] = buf[j];
  }
  if (!len)
    s[len++] = '.';
  s[len] = '\0';
  return s;
}

/*
 * Compare oids, return TRUE if match, FALSE otherwise.
 */
static int
oidmatch(oid_t *oid1, oid_t *oid2)

{
  int	i;

  if (oid1->len != oid2->len)
    return 0;
  for (i = 0; i < oid1->len; i++) {
    if (oid1->o[i] != oid2->o[i])
      return 0;
  }
  return 1;
}

/*
 * Copy oidsrc to oiddst, return oiddst.
 */
static oid_t *
oidcpy(oid_t *oiddst, oid_t *oidsrc)

{
  int	i;

  for (i = 0; i < oidsrc->len; i++)
    oiddst->o[i] = oidsrc->o[i];
  oiddst->len = oidsrc->len;
  return oiddst;
}

#if HAVE__PROC_DEVICES == 1
static u_int
devmap_major()

{
  u_int	major = DEFAULT_DEVMAP_MAJOR;
  FILE	*f;
  char	buf[100];

  if (f = fopen(DEVICES, "r")) {
    while (fgets(buf, sizeof(buf), f)) {
      if (strstr(buf, "device-mapper")) {
	sscanf(buf, "%u", &major);
	break;
      }
    }
    fclose(f);
  }
  return major;
}
#endif

static int
s2syslogfacility(char *s)

{
  if (strcasecmp(s, "daemon") == 0)
    return LOG_DAEMON;
  if (strcasecmp(s, "local0") == 0)
    return LOG_LOCAL0;
  if (strcasecmp(s, "local1") == 0)
    return LOG_LOCAL1;
  if (strcasecmp(s, "local2") == 0)
    return LOG_LOCAL2;
  if (strcasecmp(s, "local3") == 0)
    return LOG_LOCAL3;
  if (strcasecmp(s, "local4") == 0)
    return LOG_LOCAL4;
  if (strcasecmp(s, "local5") == 0)
    return LOG_LOCAL5;
  if (strcasecmp(s, "local6") == 0)
    return LOG_LOCAL6;
  if (strcasecmp(s, "local7") == 0)
    return LOG_LOCAL7;
  if (strcasecmp(s, "user") == 0)
    return LOG_USER;
  return LOG_DAEMON;
}

static void
logit(int level, char *fmt, ...)

{
  va_list	ap;
  char		buf[1024];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  if (syslogging)
    syslog(level, "%s", buf);
  else
    printf("%s\n", buf);
}

static void
usage(char *prog)

{
  fprintf(stderr, "Usage: %s [-d] -g|-n oid\n", prog);
  exit(1);
}
