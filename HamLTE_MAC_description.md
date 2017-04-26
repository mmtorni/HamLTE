HamLTE MAC description
======================
TODO: Remove all acronyms
NOTE: for uplink transmissions, DL HARQ /PHICH process ids are shared, that is they are not UE specific
TODO: For dowlink transmissions, are HARQ process ids shared?

HARQ happens in the MAC layer. HARQ does give feedback to upper layers though.

MAC can transport the following upper layer messages

        | TxType    |      | Type    | PHY channels DL | PHY channels UL | Headers? | LCID
        +-----------+------+---------+-----------------+-----------------+----------+-------
SI-RNTI | Broadcast | BCCH | Control | BCH, DL-SCH     | -               | No       | 
P-RNTI  | Paging    | PCCH | Control | PCH             |                 | No       |
C-RNTI  | Common    | CCCH | Control | DL-SCH          | UL-SCH          | Yes      |
C-RNTI  | Unicast   | DCCH | Control | DL-SCH          | UL-SCH          | Yes      |
        | Multicast | MCCH | Control | MCH             | -               | Yes      |
        + ----------+------+---------+-----------------+-----------------+----------+-------
C-RNTI  | Unicast   | DTCH | Data    | DL-SCH          | UL-SCH          | Yes      |
        | Multicast | MTCH | Data    | MCH             | -               | Yes      |

Headers?: If no headers are transmitted, then the packet is unmodified by the MAC layer.
          If headers are transmitted, then the MAC may send zero or more control elements
          followed by zero or more data packets.

One transport block per subframe per HARQ process can be sent.  Only in the
case of spatial multiplexing can two transport blocks per HARQ process be sent.

Transmission with headers
-------------------------
The MAC constructs one data packet that must exactly fill a PHY transport block.
The MAC transport block is of the following format:
0-2\*(padding headers) (n+m)\*(MAC subheaders) m\*(MAC control elements) n\*(data packets) 3-15\*(zero padding)

MAC subheaders are 1-3 byte headers. They indicate the contents and length of following data packets.
The 1 byte headers have an implied data length field. The 2-3 byte have a data field length of 7,15, or 16 bits.



If 1-2 bytes of padding are required, that is done with the padding headers in front. Otherwise zero padding at the end is used.

MAC layer generates and absorbs the following messages:

RA-RNTI | Unicast   |      | Control | DL-SCH          | -
M-RNTI  | ?         |      | Control | -               | -

PHY layer generates and absorbs the following messages:

TPC-PUCCH-RNTI
TPC-PUSCH-RNTI

Random notes
------------
class ServingCell:
  broadcastHarqProcess
  dlHarqProcesses[8-16]
  
