#    Copyright 2015 by Clemson University
#
#    This file is part of snmpiostat.
#
#    Snmpiostat is free software: you can redistribute it and/or modify
#    it under the terms of the Lesser GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    snmpiostat is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    Lesser GNU General Public License for more details.
#
#    You should have received a copy of the Lesser GNU General Public License
#    along with snmpiostat.  If not, see <http://www.gnu.org/licenses/>.

AC_INIT(snmpiostat, 1.0.0, [rschwa2@clemson.edu], [snmpiostat.tar])
AC_CONFIG_HEADERS([config.h])
AC_CHECK_FILES([/proc/devices])
AC_CHECK_FILES([/proc/diskstats])
AC_REVISION([$Revision$])
AC_PROG_CC
AC_LANG(C)

AC_ARG_WITH([netsnmp],
	    [AS_HELP_STRING([--with-netsnmp=path : path to NET-SNMP package])],
	    [with_netsnmp=yes; netsnmp=$withval],
	    [])
if test "$with_netsnmp" = yes; then
  CPPFLAGS="$CPPFLAGS -I$netsnmp/include"
  LDFLAGS="$LDFLAGS -Wl,-R$netsnmp/lib -L$netsnmp/lib"
fi

AC_CHECK_HEADERS(linux/major.h)
AC_CHECK_HEADERS(net-snmp/net-snmp-config.h)
AC_CHECK_LIB([netsnmp], [snmp_parse_args])
AC_CHECK_LIB([kstat], [kstat_open])
AC_CONFIG_FILES(Makefile)
AC_OUTPUT
