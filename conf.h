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

extern 	int	cf_read(char *fn);
extern char	*cf_get(char *name);
extern void	cf_free();
