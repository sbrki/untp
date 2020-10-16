# untp

Barebones NTP client implementation. Supports NTP versions up to NTPv4 (RFC 5905). 

Does a one-shot time offset calculation, no fancy statistics or intersection algorithms (yet).

# Usage
```
❯ ./untp pool.ntp.org
received 48 bytes
stratum: 0
rtt: 0 sec 44812356 ns (0.044812 s)
client and server time are offset by: 0 sec and 497418929 ns (0.497419 s)
server transmit time: Fri Oct 16 03:25:07 2020
 and 504412351 ns (0.504412 s)
 ```