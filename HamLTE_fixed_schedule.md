HamLTE fixed scheduling
=======================
For HamLTE, we are using a fixed schedule for System Information.

SIB1 is always transmitted in subframe 0
SIB2 and 9 are also always transmitted in subframe 0

This means two DCI format 1C system information allocations are needed.

For data packets DCI format 1A is used


There's also the consideration of RRC. COTS hardware will want to negotiate a
connection to the base station with RRC protocol. The first few transactions to
negotiate encryption are done specially and then it continues as regular data.


After these basic services are provided, COTS hardware can be used with HamLTE.
