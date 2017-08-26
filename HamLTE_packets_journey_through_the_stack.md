LTE packets journey through the stack
=====================================
Before the IP packet starts its journey through the LTE stack, we must make
certain assumptions about delivery guarantees. The most important parameters
are visible packet loss, bit error tolerance, maximum packet latency, and
packet ordering and duplication.

Background
----------
As the IP protocol does not guarantee reliable transmission, does LTE need to
use a reliable transmission mode? The TCP protocol has a hidden assumption of a
maximum packet loss rate that the original inventors didn't write about. If
that packet loss rate assumption is violated, TCP throughput is limited to low
numbers.

What about packet ordering? The IP protocol does not guarantee that either. The
designers of LTE decided that packet ordering should be kept as well. LTE also
promises to not create duplicates of packets it sends.


LTE keeps its packet loss, ordering and non-duplication guarantees as long as a
client is connected to a base station. The moments when these are momentarily violated 
are at a handoff to another base station or if the radio link to the base
station is temporarily lost. The state of packets in flight are not shared
between base stations. Also the state is defined to be lost if the radio link
is even temporarily broken. This loss of state includes packets queued for
transmission and segments of packets already sent.


High level packet processing
----------------------------
The packet is massaged into many intermediate forms before those forms are
again step by step returned to their original condition.

Packet Data Convergence Protocol (PDCP) is the highest level in the LTE stack.
It compresses IP packet headers and protects the packets from eavesdropping and
tampering while they are in the public airspace.

The eavesdropping and tampering protection are provided by encryption and
cryptographical checksums.  Encryption is forbidden for radio amateurs and in
some regions of the world and thus it is defined optional.

The RLC protocol provides link characteristics expected of typical Internet
links on top of the wireless channel which has very different characteristics.

LTE also wants to provide an optimized bandwidth and latency for packets, so
the packets over-the-air representation must be adapted to current radio link
conditions. This includes adapting the ratio of forward error correction (FEC)
to data. Also PHY layer data transfer operates on a 1ms subframe cycle. That
cycle limits the size of a individual low level transmission. In addition, some
of the transformations are only defined on discrete block sizes so packets need
to be broken up into segments and padded to fit the block sizes.


Low level packet processing
---------------------------
Once the IP packet is handed off to the PDCP, it is first optionally compressed
with the RoHC procotol. The RoHC protocol compresses the most common headers to
almost nothing.  Then a cryptographic checksum is added and the packet is
optionally encrypted.  Then it's handed off to the RLC protocol
for transmission.

The RLC protocol gives delivery guarantees and is similar in function to
the TCP protocol. Unlike the TCP protocol, RLC preserves packet boundaries.

MAC and PHY layers do the work of most interest to radio amateurs. They
continuously keep track of radio link conditions and try their best to make
most out of the radio channel capacity for individual links. The PHY layer
encodes overhead signaling into as few bits as possible while still providing
performance guarantees to both clients and the whole radio network.

The first time segmentation happens is in RLC. The segment size changes every
1ms subframe and can be zero if the subframe is filled with other traffic or is
used for uplink in TDD mode. As such, the segments are created live based on
decided bandwidth allocation to this client.

The bandwidth allocation for individual packets or segments needs to be
decided. A simple implementation just sends data in the order it is
received. However this causes uncontrolled latency when the base station is
sending to several clients at the same time. A fairer implementation tries to
minimize latency for all.  In complex implementations these decisions are
called scheduling and can be a software package on its own. See the separate
section for more discussion.

The segments are then further broken up in the PHY layer into pieces called
code blocks. The code blocks are individually coded with FEC and protected with
CRC. The blocks are then modulated into complex symbols. After all the complex
symbols for all transmissions in the subframe are created, they are then
distributed over the OFDM symbols within the subframe.

These OFDM symbols are then transformed to the time domain with some additional
transformations.




allocated places in OFDM symbols. The used modulation and location of the
blocks in the subframe is encoded in the beginning of the subframe.

Now t

The receiver is informed of the modulation and the allocated places in OFDM
symbols by small headers in the beginning of the subframe.


IP PDCP RLC MAC PHY 

The packet starts it's journey at the PDCP 
First it's cryptographically 
First it's packed into a PDCP frame. 

Then it is queued by the RLC layer for transmission with the reliable
Acknowledged Mode protocol. The RLC layer waits until the MAC layer below signals how many
bytes can be sent. If the whole packet can be sent, it frames it with an RLC
header and sends it off.  If the packet is longer than the bytes available,
only a segment of the packet that fits is sent.

As the Acknowledged Mode protocol promises to deliver packets
reliably, in-order and without duplicates, it needs to keep track of the
transmission until it's acknowledged by the receiving RLC layer. Reliability
also means it needs to re-send lost segments and packets.  On retransmissions,
the number of available bytes can be different, so a different segment size
than on the first transmission is probable.



Digression: iFFT and Cyclic Prefix
----------------------------------
Now the subframe consisting of several OFDM symbols are almost ready for
transmission. The individual OFDM symbols are converted into the time domain by
an inverse Fast discrete Fourier Transform (iFFT). Then for each symbol a
portion from the end of the signal is copied and prepended in front of the
symbol. This forms the Cyclic Prefix which gives some resistance to radio wave
reflections off objects.

IP packet on the PHY layer
--------------------------
Here we assume there's no segmentation involved and nothing but a single IP packet is transmitted and header compression and encryption are disabled. IP packet transmission is called "U-plane" or DRB.

PDCCH contains a DCI informing of the MAC layer packet size called transport block size. The MAC layer pads the packet to fit a transport block size.

PDCCH: DCI format 1 giving transport block size. Transport block size does not cover CRC or FEC bits
PDSCH contains:
  (MAC special padding) (MAC header) (RLC header) (PDCP header) (IP header) (IP data) (MAC padding) (CRC bits) (FEC bits)

PDCP header:
  (0b1=data indicator) (7-bit or 12-bit Sequence number) (data)


PDCP random notes
-----------------
The first bit of a PDCP packet is a control=0/data=1 indicator.

Signaling or RRC/NAS packets have special format.
They have a 5-bit serial number and then the data followed by a cryptographic hash. S means Sequence number. R means reserved.
Signaling packet = 0bRRRSSSSS (RRC/NAS data) (32-bit message authentication code)

_TODO: How are signaling packets differentiated from other packets? Perhaps implicit in MAC LCID field?_

Data packets have then a 7/12/15-bit serial number and then the IP packet. The serial number is LSB aligned in the 1/2-byte header. S means Sequence number. R means reserved.
Data packet = 0b1SSSSSSS (data)
Data packet = 0b1RRRSSSS 0xSS (data)
Data packet = 0b1SSSSSSS 0xSS (data)

There's two types of control packets, RoHC feedback and PDCP status report. R means reserved. Y means starting sequence number for the status reports.
RoHC feedback packet = 0x9R (RoHC data)
PDCP status report = 0x8Y 0xYY (optional status bitmap)

Status bitmap contains reception confirmations for packets. 0xYYY is the starting offset. 1=received, 0=not received. Not received implies retransmissions must be done.


RLC Acknowledged Mode headers
-----------------------------
RLC headers with IP packet without segmentation. This means Acknowledged Mode transmission.
S means Sequence Number. P=1 means status report is requested.

Data packet = 0b10P000SS 0xSS (data)

Also it's possible to multiplex many small packets. L means length field. E
means there's yet another length field. The last data packet does not have a
length field and can be longer than 2047 bytes.

F=1 means that the first data portion is the last segment of earlier "Fragmented data packet". F=0 means that the first data begins a new data packet.
F=1 means that the last data field (without the length field) is start of a segmented data packet. F=0 the last data packet is a full packet.

With a single packet:
FF=00 ⇒ Full packet
FF=01 ⇒ Start of segmented data packet
FF=11 ⇒ Middle of large packet
FF=10 ⇒ Last portion of a packet


Multiplexed data packet = 0b10PFf1SS 0xSS n\*(0bE 0bLLLLLLLLLLL) (padding bits to byte boundary) n\*(data) (data)

Also fragmentation of a large packet is possible. S means Sequence Number. P=1 means status report is requested. FF=01 beginning of packet, FF=11 middle of packet. FF=10 end of packet

Fragmented data packet = 0b10PFF0SS 0xSS (data)

Control packets both report lost packets and segments and acknowledge them. N means serial number of missing packet, A means serial number of acknowledgement. s and e means byte offsets of lost segments. e=-1 means to end of data packet.

Control packet = 0b0000  (0bAAAAAAAAAA E1) (0bNNNNNNNNNN E1 E2) (0bNNNNNNNNNN E1 E2) \*(0bsssssssssssssss 0beeeeeeeeeeeeeee) _TODO: DESCRIBE THIS BETTER_

RLC Acknowledged Mode retransmissions
-------------------------------------
If any of the above packets need to be retransmitted, it might be the case that the original packet cannot be sent in full this time due to a current transmission length limit. Then it needs to be resegmented. If these resegmented packets need to be sent yet again, they might need another resegmentation. Unfortunately the Sequence Numbers are already fixed. For this purpose there are resegmentation versions of the above packets. This gets slightly tricky.

F=1 means these bytes are the final bytes of the packet. F=0 means beginning or middle parts. s means starting offset of data.

Resegmented data packet = 0b11P000SS 0xSS 0bFsssssss 0xss (data)

Resegmented multiplexed data packet 


RLC Unacknowledged Mode headers
-------------------------------
Multiplexing and segmentation happen as in Acknowledged Mode, only the fixed header is different.

Unacknowledged data packet = 0bFF0SSSSS (data)
Unacknowledged data packet = 0b000FF0SS 0xSS (data)

For multiplexing the first two bytes are like this, otherwise it's like RLC AM
Unacknowledged multiplexed data packet = 0bFF0SSSSS (...see Acknowledged Mode...)
Unacknowledged data packet = 0b000FF0SS 0xSS (...see Acknowledged Mode...)

There are no control packets for Unacknowledged mode.

MAC headers
===========
Subpackets are represented in the following format:
Subpacket name = Subheader specification || element specification

MAC subheader = 0bR01 LCID 5 0b0 length 7
MAC subheader = 0bR01 LCID 5 0b1 length 15
MAC subheader = 0bR11 LCID 5     length 16
Last MAC subheader = 0bR00 LCID 5 (implicit length: remaining packet)

MAC Logical Channel
-------------------
Logical Channel defines whether to use RLC Acknowledged Mode or Unacknowledged
Mode, and QoS parameters. It also defines where the packets will end up.
LCID=1 is always RRC protocol "Signaling Radio Bearer 1"
LCID=2 is NAS traffic\*1
LCID=3 is best-effort Internet traffic\*2

\*1 although the base station can specify NAS traffic to go through LCID=1, this option is rarely used.
\*2 This needs to be configured through RRC 


MAC Downlink messages
---------------------
One byte padding = 0bRRE11111 || nil
Two byte padding = 0bRRE11111 0bRRE11111 || nil
If 1 or 2 bytes of padding are needed, use one of these in front. Otherwise use zero padding at end of packet.

RRC Message = LCID=0b00000 || data...
Logical channel message RRC over SRB1 = LCID=0b00001 || data...
Logical channel message NAS over SRB2 = LCID=0b00010 || data...
General Internet traffic over DRB = LCID=0b00011 || data...
HamLTE Unauthenticated mode data packet = LCID=0b01010 || data...

Discontinuous Reception Command = 0bRRE11110 || nil
Long Discontinuous Reception Command = 0bRRE11010 || nil
UE Contention Resolution Identity = 0bRRE11100 || identity 48
Timing Advance Command = 0bRRE11100 || tagId 2 timingAdvance 6

LTE specifies more types, but they are not used in HamLTE.

MAC Uplink messages 
-------------------

Special Padding = 0bRRE11111 || nil
If 1 or 2 bytes of padding are needed, use two of these in front. Otherwise use zero padding at end of packet.

RRC CCCH Message = LCID=0 || data...
CCCH Message = LCID=0b01010 || data...
Logical channel message RRC over SRB1 = LCID=0b00001 || data...
Logical channel message NAS over SRB2 = LCID=0b00010 || data...
General Internet traffic over DRB = LCID=0b00011 || data...
HamLTE Unauthenticated mode data packet = LCID=0b01010 || data...

Truncated Buffer Status Report = LCID=0b11100 || LCGID 2 bufferSizeIndex 6
Short Buffer Status Report = LCID=0b11101 || LCGID 2 bufferSizeIndex 6
Long Buffer Status Report = LCID=0b11110|| 4\*(bufferSizeIndex 6)

bufferSizeIndex is looked up in a table to determine buffer size.

C-RNTI = || rnti 16
Power Headroom Report = || 0bRR powerHeadroom 6

(Extended Power Headroom Report = || TODO)
(MCH Scheduling Information = || TODO)
(Extended MCH Scheduling Information = || TODO)

powerHeadroom is specified by an equation. It reports an amount in dB.



MAC Random Access Response
--------------------------
Backoff indicator is optional and must be the first message. Index into [0,10,20,30,40,60,80,120,160,240,320,480,960,960,960,960]. The last three indexes are reserved and must not be sent.

Backoff Indicator = 0b00RR backoffIndicator 4 || nil

MAC subheader = 0b11 randomAccessPreambleId 6
Last MAC subheader = 0b01 randomAccessPreambleId 6

Random Access Response = 0b#1 randomAccessPreambleId 6 || 0bR timingAdvance 11 ulGrant 20 temporaryCRNTI 16

timingAdvance is looked up in a table.
temporaryCRNTI is the RNTI used by the client during random access.

PHY Uplink messages
===================
Scheduling Request

Links
=====
Pretty good RLC LTE description
  <http://www.sharetechnote.com/html/RLC_LTE.html>
