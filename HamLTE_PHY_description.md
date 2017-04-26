LTE PHY description
===================
I'll start with some background on what's happening on the radio link. Then
we'll delve into a general description of the LTE PHY communication.

As with other parts of my LTE documentation, I'm avoiding the multitude of
abbreviations that the 3GPP specifications use. Even mentioning them here will
encourage people to use them when communicating with others, which I believe
will hinder understanding the protocol.

Background
----------
What is happening in a radio link PHY layer?

The important motivation is transmission of data. It's also desired that the
transmission of data succeeds, so the errors appearing during transmission need
to be low enough. For LTE the desirable error ratio is defined to be close to
zero for higher layer packets. Thus we need to provide error-free, reliable
transmission.

For optimal error-free transmission speeds, signal-to-noise ratio (SNR) needs
to be known. This affects how much data transmission is possible. The SNR
varies over time and frequency due to natural atmospheric phenomena. In mobile
transmissions the signal path changes quickly due to movement and speed of
movement.

As knowing the SNR is impossible, it needs to be estimated. Estimates can be
reliable only if they are recent. This is possible if both sides keep
transmitting frequently. If that is not the case, special arrangements are
required to assure enough transmissions. In LTE, this is achieved by special
reference signal transmissions. Also the problem is that the transmitter of
data needs the SNR estimate, so the estimate must be relayed back to the
source.

The SNR is also affected by the transmitter power. The SNR increases when the
transmitter power is increased. Sending at unnecessarily high SNR increases
energy consumption but produces no benefits.

There's also the more complicated situation in the base station where the SNR
for other clients can be reduced by one client transmitting at too high power.
This is a side-effect of the base station receiving signals from many clients
simultaneously. The main component responsible for this is the
analog-to-digital converter (ADC). Similar effect also exists for purely analog
systems.

LTE PHY base station to client link
-----------------------------------
If there was no concerns regarding the SNR, we could just transmit data all the
time with maximum capacity and no retransmissions. If the SNR was fixed, we
could measure it once and then transmit at the maximum capacity. As the SNR is
not fixed for mobile communications, there needs to be regular measurements so
we can achieve continuous reliable transmissions with optimized error-free data
rates.

One way to achieve a reliable, optimized link is with:
 * Reference signal transmissions for easy SNR estimation
 * Many choices of modulation types
 * Addition of variable amounts of forward error correction (FEC) bits
 * Sending more FEC bits if the transmission didn't go through

And as we are receiving from many clients simultaneously, one more control is needed:
 * Setting individual clients transmitter power
 
LTE also adds a few more optimizations
 * Scrambling transmissions with pseudo-random sequences to make them resemble
   white noise for other base stations
 * Spreading of individual transmissions over the whole system bandwidth
 * Power saving states for idle clients (optional)

Adding all of these together results in a PHY that at first glance seems like
an incomprehensible complex tangle of tables and abbreviations. However, all
the subprocesses are individually understable and it's only how they are put
together that results in many details. In addition there's no point in
optimizing for SNR if the optimizing process itself consumes sizable bandwidth.
That's why the 3GPP group has tried to minimize the number of bits sent over
the air in all situations. Unfortunately this results in even more details.
Once the details are separated out in code that is put aside, the remaining
system is both understable and flexible.

I have done my best to separate the unnecessary details from the interesting
bits of LTE implementation to make it easy for radio amateur and other
hobbyists to understand and tinker with modern packet radio systems.


Layer mapping and precoding
---------------------------
def layer\_mapping\_and\_precoding(block):
  assert ANTENNA\_PORTS == 1
  return block.reshape((1,)+block.shape)

Control channel resource mapping
--------------------------------
The control channel can be viewed as a QPSK modulated bitstream. Then one REG
is one byte and the offsets become much easier to calculate. In the control
channel, BPSK symbols are just QPSK with bits repeated twice. REGs are groups
of four QPSK symbols. REG is four non-reference-signal complex symbols in a row.

  if ANTENNA\_PORTS == 4:
    qpsk\_bytes\_in\_cch_symbol = np.array([NOF\_PRB*2, NOF\_PRB*2, NOF\_PRB*3])
  else:
    qpsk\_bytes\_in\_cch_symbol = np.array([NOF\_PRB*2, NOF\_PRB*3, NOF\_PRB*3])

  def pcfich\_byte\_offsets():
    assert SUBCARRIERS_PER_RESOURCE_BLOCK == 12
    k = np.array([0,1,2,3], dtype='uint') * NOF_PRB // 2

    return (k + CELL_ID) % (2*NOF_PRB)

  def phich\_byte\_offsets(nof\_mapping\_units):
    assert CYCLIC_PREFIX == 'normal'
    assert PHICH_DURATION == 'normal'
    assert ANTENNA_PORTS in (1,2,4)
    pcfich = pcfich_byte_offsets()
    free_offsets0 = np.setdiff1d(np.arange(qpsk_bytes_in_cch_symbol[0]), pcfich, assume_unique=True)
    free_bytes = qpsk_bytes_in_symbol.copy()
    free_bytes[0] -= len(free_offsets0)
    mp = np.repeat(np.arange(nof_mapping_units), 3)
    i = np.resize(np.arange(3) * free_bytes[0] // 3, len(mp))
    mp += i
    mp += CELL_ID * free_bytes[0] // free_bytes[1]
    mp %= free_bytes[0]

    # Skip over pcfich bytes
    return free_offsets0[mp]

    

    

Downlink control channel details
--------------------------------
The downlink control channel has an amount of space encoded in the PCFICH codeword.
The control channel is filled with DCI messages. A single DCI message is packed
into a "PDCCH" unit, which can be of length 72/144/288/576 bits.
  assert all(len(packed\_dci)\*8 in (72,144,288,576) for packed\_dci in dcis\_to\_be\_transmitted)

Downlink control channel appears in the beginning of every subframe. It's the first 1-4 symbols, but Reference Signals, PHICH and PCFICH get allocated first.
The PDCCH units are concatenated together, with padding appended to fill the whole allocated control channel
  block = np.concatenate(dcis\_to\_be\_transmitted)
  block.resize(pdcch\_total\_size\_in\_bytes)
That block is then XORed with scrambling sequence((sf << 9) + cell.id).
  block ^= scrambling((sf << 9) + cell.id, len(block))
That block is then modulated with QPSK.
  block = modulate(block, bits\_per\_symbol=2)
If using several layers, that block is then layer mapped and precoded.
  blocks = layer\_mapping\_and\_precoding(block)
Then it is mapped to resource elements in quadruplets of symbols (quadruplet of
symbols is one byte with QPSK modulation) permuted by the sub\_block\_interleaver.
  np.reshape((blocks.shape[0], blocks.shape[1]//4, 4)) #form symbol quadruplets (==byte sized units)
  for port in range(ANTENNA\_PORTS):
    subblock\_interleaver(blocks[port])  #permute symbol quadruplets (bytes)
  block = np.roll(block, -(cell.id%a.shape[0]), axis=1)  #cell-specific cyclic shift
  sf\_symbols += re\_mapping(blocks, pdcch\_allocation)


Physical broadcast channel details
----------------------------------
It appears only in subframe 1.
  if sf != 1:
    return
The channel is allocated as 6 resource blocks in the middle of bandwidth.
  pbch\_allocation = np.array([None]) #TODO
That block is XORed with a range of scrambling sequence(cell.id), but the scrambling sequence only restarts every four frames.
  scramble = scrambling(cell.id, 4\*len(block))  # Generate four quarters
  scramble = scramble.reshape((4, len(block)))  # Divide into four quarters
  block ^= scramble[sfn%4]  # Scramble with proper quarter
Modulate with QPSK
  block = modulate(block, bits\_per\_symbol=2)
If using several layers, that block is then layer mapped and precoded.
  blocks = layer\_mapping\_and\_precoding(block)
Then it is mapped linearly to resource elements allocated to PBCH. Note that
the PBCH allocation does not include the resource elements that would be
reserved for Reference Signals for antenna ports 0-3.
  re\_mapping(sf\_symbols, blocks, pbch\_allocation)
  
Physical control format indicator channel
-----------------------------------------
(Note: Assumes it's not a MBSFN subframe and no positioning reference signals are sent)
  def cfi\_value():
    if sf\_config[sf] == 'D':  #Any normal subframe for FDD and downlink subframes for TDD
      return CONTROL\_CHANNEL\_LENGTH - (NOF\_PRB <= 10)
    elif sf\_config[sf] == 'U':
      return None
    elif sf\_config[sf] == 'S':
      assert False #TODO

Turn control channel length to CFI value. Do nothing if there's no PCFICH.
  cfi = cfi\_value()
  if cfi is None:
    return
Convert CFI value to 32-bit token
  assert False #TODO: CFI tokens
  block = "XXXX"

That block is XORed with a range of scrambling sequence(cell.id), but the scrambling sequence only restarts every four frames.
  assert len(block) == 4
  block ^= scrambling(((sf+1)*(2*cell.id+1) << 9) + cell.id, len(block))
Modulate with QPSK
  block = modulate(block, bits\_per\_symbol=2)
If using several layers, that block is then layer mapped and precoded.
  blocks = layer\_mapping\_and\_precoding(block)
  np.reshape((blocks.shape[0], blocks.shape[1]//4, 4)) #form symbol quadruplets (==byte sized units)
Mapping to resource elements groups
  k\_offset = SUBCARRIERS\_PER\_RESOURCE\_BLOCK//2 \* (cell.id % (2\*NOF\_PRB))
  k = np.array(range(4)) \* NOF\_PRB // 2 * SUBCARRIERS\_PER\_RESOURCE\_BLOCK//2
  k += k\_offset
  #TODO: Convert resource element group indices to resource elements
  assert False

Physical hybrid ARQ indicator channel details
---------------------------------------------
  phich\_subframe\_factors = np.array(list(
    "21---21---"
    "01--101--1"
    "00-1000-10"
    "10---00011"
    "00--000011"
    "00-0000010"
    "11---11--1".replace('-', '0')), dtype='int').reshape((7, 10))
  phich\_orthogonal\_sequences\_length4 = np.array([
    [+1,+1,+1,+1],
    [+1,-1,+1,-1],
    [+1,+1,-1,-1],
    [+1,-1,-1,+1],
    [+1j,+1j,+1j,+1j],
    [+1j,-1j,+1j,-1j],
    [+1j,+1j,-1j,-1j],
    [+1j,-1j,-1j,+1j]], dtype='c8')
  phich\_orthogonal\_sequences\_length2 = np.array([
    [+1,+1],
    [+1,-1],
    [+1j,+1j],
    [+1j,-1j]], dtype='c8')
  def phich\_groups():
    cyclic\_prefix\_factor = 2 if CYCLIC\_PREFIX == 'extended' else 1
    if mode == 'TDD':
      subframe\_factor = phich\_subframe\_factors[TDD_FRAME_CONFIGURATION][sf]

    return int(ceil(PHICH\_NG * NOF\_PRB/8.0)) * cyclic\_prefix\_factor * subframe\_factor

  bpsk0 = np.array((1+1j)/math.sqrt(2), dtype='c8')
  sqrt1\_2 = np.sqrt(.5).astype('f4')
  bpsk1 = -bpsk0

  def get\_phich\_codes():
    c = scrambling(((sf+1)\*(2\*cell.id+1) << 9) + cell.id, 2)
    c = (1-2*np.unpackbits(c)[:12].view('int8')).astype('f4') # +1,-1
    codes = np.resize(phich\_orthogonal\_sequences\_length4.transpose(), (12, 8))
    return ((codes * c).transpose() * sqrt1\_2).copy('C')

  def make_phich(ack_groups):
    buf = np.zeros((len(ack_groups), 12), dtype='c8')
    bpsk = {None:0, 0:bpsk0, 1:bpsk1}
    for out, group in zip(out, ack_groups):
      for sym, ack in zip(get_phich_codes(), group):
        out += sym * (1-2*ack)
    return buf.ravel()

  def make\_phich\_group(acks):
    """Encode complex symbols from up to 8 acks. Input is one PHICH group. ack in (0,1,None)
       0=NACK, 1=ACK, None=no symbol"""
    # According to "6.9.1 Modulation"
    c = scrambling(((sf+1)\*(2\*cell.id+1) << 9) + cell.id, 2)
    c = 1-2*np.unpackbits(c)[:12].view('int8') # +1,-1
    bpsk = {None:0, 0:bpsk0, 1:bpsk1}
    if CYCLIC_PREFIX == 'normal':
      assert len(acks) <= 8
      assert np.all((acks == 0) | (acks == 1))
      out = np.zeros(12, dtype='c8')
      for i in range(len(acks)):
        ack = acks[i]
        ack = bpsk[ack]  # BPSK modulate
        z = c * ack  # Repeat 12 times for normal cyclic prefix and scramble
        z *= np.resize(phich\_orthogonal\_sequences\_length4[i], z.shape)
        out += z
      return out
    elif CYCLIC_PREFIX == 'extended':
      assert len(acks) <= 4
      assert False  #TODO: Extended cyclic prefix


Add simple bitwise repetition redundancy
  assert np.all(0 <= block <= 1) # Input is an unpacked bit array
  assert len(block) == phich\_groups()\*8
  subfactor = 4 if CYCLIC\_PREFIX == 'normal' else 2
  block = np.repeat(block, subfactor)
  block = np.packbits(block)
Scramble input
  block ^= scrambling(((sf+1)\*(2\*cell.id+1) << 9) + cell.id, len(block))
Modulate with BPSK
  block = modulate(block, bits\_per\_symbol=1)
Multiply with an orthogonal sequence
  if subfactor == 2:
    block *= np.resize(phich\_orthogonal\_sequences\_length2, shape=block.shape)
  elif subfactor == 4:
    block *= np.resize(phich\_orthogonal\_sequences\_length4, shape=block.shape)
Resource group alignment, layer mapping and precoding
  block = block
  if CYCLIC\_PREFIX == 'extended':
    assert False  #TODO: Extended cyclic prefix for PHICH
  else:
    block = block
Layer mapping and precoding for 1 or 2 antenna ports:
  if ANTENNA\_PORTS in (1,2):
    blocks = layer\_mapping\_and\_precoding(block)
Layer mapping and precoding for 4 antenna ports:
  if ANTENNA\_PORTS == 4:
    assert False  #TODO: 4 antenna ports for PHICH layer mapping and precoding
Mapping to resource elements
  if CYCLIC\_PREFIX == 'extended':
    assert False  #TODO: Extended cyclic prefix for PHICH resource element mapping
  else:


Cell-specific reference signals
-------------------------------
MAX_PRB = 110
def get_crs_offsets(port=0):
  assert ANTENNA_PORTS in (0,1)
  assert SUBCARRIERS_PER_RESOURCE_BLOCK == 12
  if port in (0,1):
    used_symbols = np.array([0, SYMBOLS_PER_SLOT-3, SYMBOLS_PER_SLOT, 2*SYMBOLS_PER_SLOT-3], dtype='int')
  elif port in (2,3):
    used_symbols = np.array([0, SYMBOLS_PER_SLOT], dtype='int')

  out = np.zeros((len(used_symbols), NOF_PRB*2), dtype='int')
  out[:] = np.arange(0, 12*NOF_PRB, 6)

  everyother = 0\*used_symbols
  everyother[1::2] = 1
  v = (port%2) ^ everyother
  v = (3\*v + CELL\_ID) % 6
  v += used_symbols * NOF_PRB * SUBCARRIERS_PER_RESOURCE_BLOCK
  out += v.transpose()

def make_crs_data():
  if port in (0,1):
    used_symbols = np.array([0, SYMBOLS_PER_SLOT-3, SYMBOLS_PER_SLOT, 2*SYMBOLS_PER_SLOT-3], dtype='int')
  elif port in (2,3):
    used_symbols = np.array([0, SYMBOLS_PER_SLOT], dtype='int')
  buf = np.array((len(used_symbols), NOF_PRB*2*2//4), dtype='int32')
  for out, sym in zip(buf, used_symbols):
    c_init = 8 + (sf * 14) + sym
    c_init *= (2*CELL_ID + 1)
    c_init <<= 10
    c_init += 2*CELL_ID + (CYCLIC_PREFIX == 'normal')
    sym_sequence = scrambling(c_init, 4*MAX_PRB//8)
    sym_sequence = np.unpackbits(sym_sequence)
    out[:] = sym_sequence[2*MAX_PRB-2*NOF_PRB:][:4*NOF_PRB]

  return (1-2*buf).astype('f4').view('c8') * sqrt1_2


  return buf

PDSCH details
-------------
Data transmission looks like this on the PHY layer:

The packet is called a transport block. It can be only one of specific sizes, so there's often padding involved.

Data is added with FEC and the resulting stuff is called a softbuffer.

PHY layer sends as much of the softbuffer as fits in the PDSCH allocation.




PDCCH details
-------------


PHY messages
------------
System Information

RNTI=0xffff modulation=QPSK transportBlockSize/=i\_mcs || SystemInformation..

Paging and System Information Change
RNTI=0xfffe modulation/=i\_mcs transportBlockSize/=i\_mcs || PCCH-Message..

Random Access Response
RNTI=0xfffd modulation/=i\_mcs transportBlockSize/=i\_mcs || nil

Uplink allocation for TDD, DCI format 0
RNTI=clientRNTI = 0b1 hoppingFlag 1 nULHop 1-2 rbAlloc 5-13 modulationAndCodingIndex 5 newDataIndicator 1 transmitPowerControl 2 dmrsCyclicShiftIndex 3 downlinkAssignmentIndexTDD 2 csiRequest 1-2 || nil

dmrsCyclicShift = [0,6,3,4,2,8,10,9]

Uplink allocation for FDD, DCI format 0
RNTI=clientRNTI = 0b1 hoppingFlag 1 nULHop 1-2 rbAlloc 5-13 modulationAndCodingIndex 5 newDataIndicator 1 transmitPowerControl 2 dmrsCyclicShiftIndex 3 csiRequest 1-2 || nil

Downlink control channel messages
---------------------------------
For HamLTE, only DCI format 1A and 1C are used, thus only resource allocation type 2 is used.

Resource Allocation is a group of fields in following messages. It's defined as:
resourceAllocationType2 = 0b1 \\resourceAllocationType2Distributed
resourceAllocationType2 = 0b0 \\resourceAllocationType2Localized
resourceAllocationType2Distributed = nGap 0-1 rbAlloc 3-9

  n\_gap = n\_gap\_table[nGap]
  vrbCount = 1 + rbAlloc // NOF\_PRB
  vrbOffset = rbAlloc % NOF\_PRB

DCI1C System Information = \\resourceAllocationType2Distributed tbsIndex 5 0xffff || BCCH-DL-SCH-Message
DCI1C Paging = \\resourceAllocationType2Distributed tbsIndex 5 0xfffe || PCCH-Message..

  tranportBlockSize1Ctable = [5, 7, 9, 15, 17, 18, 22, 26, 28, 32, 35, 37, 41, 42, 49, 61, 69, 75, 79, 87, 97, 105, 113, 125, 133, 141, 153, 161, 173, 185, 201, 217] # In bytes

DCI1A Random Access Request = 0b1 sizeof(\\resourceAllocationType2)\*(0b1) preambleIndex 6 prachMaskIndex 4 (paddingToLengthof1A) \\padding1A clientRnti 16 || nil
DCI1A MAC data packet = \\resourceAllocationType2 modulationAndCodingIndex 5 harqProcess 3-4 newDataIndicator 0-1 redundancyVersion 2 0bR transmitPowerControlSpecial 1 clientRnti 16 || data...





DCI details
-----------
RNTI 1-60 are used for client selected temporary RandomAccess-RNTIs, and avoided for others (unicast)
RNTI 61-65523 client RNTIs allocated by the base station (unicast)

RNTI 0 is forbidden
RNTI 65524-65532 are reserved
RNTI 65533 Multicast (broadcast)
RNTI 65534 Paging (broadcast)
RNTI 65535 SystemInformation (broadcast)

In addition to the below formats, different DCI formats are differentiated by
the bit lengths of the DCI packet. The differences must be ensured by adding
padding. These are best collected in a table as the rules are somewhat complicated.



Uplink allocation, unicast only, PUSCH hopping
DCI format 0 raType0 = 0b0 hoppingFlag 0-1 nULHop 0-2 rbAlloc 3-13 modulationAndCodingOrRedundancyVersionIndex 5 newDataIndicator 1 transmitPowerControl 2 dmrsCyclicShiftAndOccIndex 3 ulIndexTDD 0-2 downlinkAssignmentIndexTDD 0-2 csiRequest 1-2 srsRequest 0-1 raTypeFDD 0-1 rntiUnicastAndCrc 16

There's several things happening in DCI format 0. There's the:
Resource Block allocation (PHY layer): hoppingFlag nULHop rbAlloc
Modulation (PHY Layer): modulationAndCodingOrRedundancyVersionIndex
Coding Index (PHY Layer FEC): modulationAndCodingOrRedundancyVersionIndex
Redundancy Version (PHY layer FEC): modulationAndCodingOrRedundancyVersionIndex, (newDataIndicator?)
Transmit Power Control Adjustment (PHY layer): transmitPowerControl
Cyclic shift for Uplink Reference Signals (PHY layer): dmrsCyclicShiftAndOccIndex
TDD allocation (PHY layer): ulIndexTDD downlinkAssignmentIndexTDD
Channel Quality Monitoring: csiRequest srsRequest 



modulationAndCodingOrRedundancyVersionIndex < 29 ==> redundancyVersion=0
modulationAndCodingOrRedundancyVersionIndex > 29 ==> redundancyVersion=1-3
modulationAndCodingOrRedundancyVersionIndex < 29 ==> i\_mcs
modulationAndCodingOrRedundancyVersionIndex > 29 ==> use previous i\_mcs

Broadcast only (System Information, Paging, Random Access Response), always raType=2
DCI format 1C, Broadcast and Unicast
DCI format 1C = (nGap 0-1 rbAlloc 3-9) transportBlockSizeIndex 5 rntiBroadcastAndUnicastCrc 16
DCI format 1C = n\*(ulDlConfiguration 3) rntiBroadcastAndUnicastCrc 16

DCI format 1A unicast = 0b1 (distributedAllocationFlag 1 nGap 0-1 rbAlloc 5-13) modulationAndCodingIndex 5 harqProcess 3-4 redundancyVersion 2 transmitPowerControl 2 rntiUnicastAndCrc 16

DCI format 1A broadcast = 0b1 (distributedAllocationFlag 1 nGap 0-1 rbAlloc 5-13) modulationAndCodingIndex 5 harqProcess 3-4 newDataIndicator 0-1 redundancyVersion 2 reserved 1 transmitPowerControlSpecial 1 rntiBroadcastAndCrc 16

DCI format 1A Random Access Request = 0b11 rbAllocAllOnes 5-13 preambleIndex 6 prachMaskIndex 4 zeroPadding 

DCI format 1 is more general and more complicated. It depends on the parameters NOF\_PRB, Carrier Aggregation, TDD/FDD mode, TDD configuration, 

DCI format 1 raType0 = 0b0 (rbAlloc 6-25) modulationAndCodingIndex 5 harqProcess 3-4 redundancyVersion 2 transmitPowerControl 2 rntiAndCrc
DCI format 1 raType1 = 0b1 (subset 0-2 shift 0-1 rbAlloc 0-22) modulationAndCodingIndex 5 harqProcess 3-4 redundancyVersion 2 transmitPowerControl 2
DCI format 1 raType 2 = 0b1 (localizedOrDistributed 1 nGap 0-1 rbAlloc 5-13) modulationAndCodingIndex 5 harqProcess 3-4 redundancyVersion 2 transmitPowerControl 2

Unicast only
DCI format 1


Links
=====
Long overview of DCI information contents
  <http://www.sharetechnote.com/html/DCI.html>

LTE Resource Allocation types
  <http://www.sharetechnote.com/html/Handbook_LTE_RAType.html>
