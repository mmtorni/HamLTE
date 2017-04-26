Synchronization model
=====================
To synchronize to a radio transmission in an OFDM transmission with Cyclic
Prefix, there's only three variables needed to model in a software-defined
radio.

 * Frequency Offset
 * Timing or Sampling Offset

The frequency offset can be corrected by multiplying the complex samples by the
complement of the frequency offset.

Timing offset, also known as sampling offset can be divided into an integer and
fractional part.

The integer part can be handled by skipping or adding an integer amount of
samples between OFDM symbols.

The fractional timing offset is slightly trickier. A fractional delay over the
whole frequency band is most easily implemented in the frequency domain (after
the FFT). It is corrected by multiplying all the complex symbols by a complex
number which adds or substracts phase.

Finding the offsets
===================
For finding the offsets there are many creative ways available. There're many
signal properties to deduce it from. Here's a few:
 * Cyclic prefix
 * Synchronization symbols
 * Symbol length
 * Subframe length


