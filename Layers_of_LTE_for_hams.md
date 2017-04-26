For LTE release 8 TDD frames (frame type 2)
===========================================

The eNodeB (base station) decides on how to allocate uplink/downlink bandwidth.

* This diagram is from http://rfmw.em.keysight.com/wireless/helpfiles/89600b/webhelp/subsystems/lte/content/lte_overview.htm

The three slot lengths are specified by the "Dw/GP/Up Len" -parameter

Downlink pilot | guard period | Uplink pilot

Downlink pilot: P-SS, also PDSCH if length of pilot > 1 slot
Guard period: PRACH format 4 begins in the guard period, otherwise no signal
Uplink pilot: can contain PRACH and SRS, with exception of PUCCH and PUSCH

P-SS = Primary Synchronization Signal

PRACH = Physical Random Access Channel
SRS = Signaling Reference Signal
PUCCH = Physical Uplink Control Channel
PUSCH = Physical Uplink Shared Channel

DwPTS = Downlink Pilot Time Slot, contains P-SS and PDSCH
UpPTS = Uplink Pilot Time Slot
GP = Guard Period

Narrowband
==========
* 180kHz slice of bandwidth, composed of:
  * 12x15kHz or 24x7.5kHz subcarriers

