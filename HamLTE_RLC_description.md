HamLTE RLC protocol layer
=========================

There's three types of packet transfer. All packet sizes must be multiple of 8 bits.
RLC buffers whole packets and only forms smaller pieces when the MAC layer
informs how many bytes can be sent.


   | Transfer mode        | Headers      | Segm't | Order | ARQ | Logical channels       | Upper layer
---+----------------------+--------------+--------+-------+-----+------------------------+-------------
TM | Transparent mode     | No headers   | No     | No    | No  | BCCH, DL/UL CCCH, PCCH | 
AM | Acknowledged mode    | RLC headers  | Yes    | Yes   | Yes | DL/UL DCCH, DL/UL DTCH | PDCP, also control in PDCP
UM | Unacknowledged mode  | RLC headers  | Yes    | Yes   | No  | DL/UL DTCH, MCCHa MTCHa| PDCP

a:      No reordering for these channels
Segm't: RLC provides segmentation to fit MAC transport blocks and reassembles when received
Order:  On reception, packets must be delivered strictly in order and without duplicates
ARQ:    Detected lost segments must be re-requested. If no ARQ is used, then
        discard packet if not all segments have been received in a timely manner

Aside: Upper layer protocols
=====================
CCCH is only used for initial connection establishment. Further RRC communication goes on top of PDCP/DTCH/DL-SCH
On top of PDCP there's IPv4, IPv6, RRC.

Aside: RRC protocol
===================
Uses RLC AM over CCCH for initial connection establishment. Further communication is over PDCP.

Links
=====
Physical, logical and transport channel names
  <http://www.radio-electronics.com/info/cellulartelecomms/lte-long-term-evolution/physical-logical-transport-channels.php>
RLC AM and UM headers
  <http://www.3glteinfo.com/lte-rlc-pdu-headers-for-am-um-tm-detail-overview/>
