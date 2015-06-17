# snmpiostat
snmpiostat includes an agent (snmpiostatagent) that retrieves device IO stats and makes
them available to SNMP management tools, and a client (snmpiostat) that reads this data
and displays it in a format similar to iostat(1).

It was inspired by Mark Round's Cacti-iostat-templates package.  snmpiostatagent
differs from the agent included in Cacti-iostat-templates in that it retrieves the
raw device IO statistics directly from the OS (/proc/diskstats on linux, if/when I do
a solaris port it will use libkstat(3)), as opposed to parsing the output of iostat
as the Cacti-iostat-templates agent does.  This allows SNMP management tools to control
the interval over which data is collected, as opposed to having to live with whatever
interval iostat is run via cron by the Caci-iostat-templates package.

This package also includes the application snmpiostat, which uses SNMP to retrieve data
from snmpiostatagent and display it in a format similar to iostat(1).
