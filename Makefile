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

host_os		= @host_os@
prefix		= /usr/local
exec_prefix	= ${prefix}
datarootdir	= ${prefix}/share
CC		= gcc
INSTALLDIRS	= ${exec_prefix}/bin ${exec_prefix}/sbin ${prefix}/etc ${datarootdir}/man/man1 ${datarootdir}/man/man8
CPPFLAGS	= 
CFLAGS		= -g -O2 -DCONFFILE=\"${prefix}/etc/snmpiostat.conf\" -DVERSION=\"1.0.0\"
LDFLAGS		= 
LIBS		= -lnetsnmp 
INSTALL		= install

all:	snmpiostatagent snmpiostat

snmpiostatagent:	snmpiostatagent.o conf.o
	cc $(LDFLAGS) -o $@ snmpiostatagent.o conf.o $(LIBS)

snmpiostat:	snmpiostat.o conf.o
	cc $(LDFLAGS) -o $@ snmpiostat.o conf.o $(LIBS)

conf.o:		conf.h Makefile

snmpiostat.o:	conf.h config.h Makefile

snmpiostatagent.o:	conf.h config.h Makefile

install-agent:	snmpiostatagent install-dirs install-conf
	$(INSTALL) -m 755 snmpiostatagent ${exec_prefix}/sbin/snmpiostatagent
	$(INSTALL) -m 644 snmpiostatagent.8 ${datarootdir}/man/man8/snmpiostatagent.8

install-client:	snmpiostat install-dirs install-conf
	$(INSTALL) -m 755 snmpiostat ${exec_prefix}/bin/snmpiostat
	$(INSTALL) -m 644 snmpiostat.1 ${datarootdir}/man/man1/snmpiostat.1

install-conf:
	$(INSTALL) -m 644 snmpiostat.conf ${prefix}/etc/snmpiostat.conf

install-dirs:
	for d in $(INSTALLDIRS) ; do \
	  if [ ! -d $$d ]; then \
	    mkdir -p -m 755 $$d ; \
	  fi \
	done

clean:
	rm -f snmpiostatagent snmpiostat *.o *~ core

distclean: clean
	rm -f config.cache config.status config.log config.h Makefile
