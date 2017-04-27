# HamLTE
Open source 4G LTE (EUTRAN) software radio implementation for radio amateurs.

The project consists of:
 * Simplified LTE specifications suitable for radio amateur use
 * Adaptations of LTE to HAM bands (called HamLTE) and narrow bandwidths
 * Software radio implementation of HamLTE
 * Documentation on how to set up working HamLTE base stations

Simplified specifications for radio amateur use are necessary as the whole
EUTRAN radio interface specification is complex due to spectrum efficiency
optimizations. Also the official specifications interface to a specified core
network, which radio amateurs are not interested in. The necessary parts need
to be short-circuited to enable use without core network software.

LTE seems to suitable for use as a narrow bandwidth packet radio base station.
It provides many useful features and seems adaptable to sub-20kHz channels on
HF (\<30MHz) frequency bands by slowing the sample clock by a hundredfold.

