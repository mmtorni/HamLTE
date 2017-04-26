What can you really express with DCIs?
======================================
The three basic things DCIs can express are:
 * Description of what the base station is transmitting next
 * An instruction for the client to transmit data
 * Reference signal transmission request
 * Radio signal measurement instructions for the client
 * Radio signal quality feedback instructions for the client
 * Transmit power instructions for the client
 * If using more than one antenna, MIMO parameter negotiation


The interesting bits here are the actual instructions and description of transmission.

Much less interesting is how the frequency and time allocations for
transmissions are. They are a multi-layered combination of varying tables and
involve wide distribution of the data over the subcarriers. Transmission
allocations are best described by actual code.

Also quite uninteresting are the actual bit encodings of the formats. They vary
by system bandwidth, number of base station antennas, whether the duplex mode is TDD or TDD and even by subframe for TDD.


DCI format type 0
=================
The base station tells the client what to transmit next. It can either be a new data packet or an instruction to transmit more FEC bits.

Modulation and packet length are encoded as "MCS index", which is lookup up in a table. The table can be changed by mutual agreement (TODO: where exactly in higher layers).

The base station manages HARQ process ids for the client, and the client just sends contents of the field back when replying. This involves the "HARQ process id" and "New Data Indicator" fields.

"RV" field when zero means to send a new packet. When non-zero it is a request
to send more FEC bits. It's range is 0-3.

Also included is a Transmit Power Control field. This gives the client instructions to either increase or decrease transmit power by a small amount.

#TODO: DAI = Downlink Assignment Index (for TDD)
#TODO: CQI?! = C??? Quality Indicator request


DCI format type 1A
==================
#TODO: preamble\_index
#TODO: prach\_mask\_index


DCI format type 1C
==================
This really says the base station is going to send a System Information Block
with QPSK modulation.

Length of the packet is encoded as index to DCI format 1C transport block size
table and confusingly stored into field called "mcs".

The location is determined by:
Resource allocation type 2
N\_RB\_step, Ngap1 are determined by system bandwidth
Ngap2 is determined by system bandwidth when BW<10MHz
Otherwise Ngap2 is encoded in one bit


Links
=====
LTE Resource allocation types <http://www.sharetechnote.com/html/Handbook_LTE_RAType.html>

References
==========

