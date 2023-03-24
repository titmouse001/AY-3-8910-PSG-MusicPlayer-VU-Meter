#ifndef AY3891xRegisters_h
#define AY3891xRegisters_h

enum {
  // The AY38910 PSG sound chip contains sixteen registers
  PSG_REG_FINE_TONE_CONTROL_A = 0,  // R0
  PSG_REG_COARSE_TONE_CONTROL_A,
  PSG_REG_FINE_TONE_CONTROL_B,
  PSG_REG_COARSE_TONE_CONTROL_B,
  PSG_REG_FINE_TONE_CONTROL_C,
  PSG_REG_COARSE_TONE_CONTROL_C,
  PSG_REG_FREQ_NOISE,
  PSG_REG_ENABLE,
  PSG_REG_AMPLITUDE_A,
  PSG_REG_AMPLITUDE_B,
  PSG_REG_AMPLITUDE_C,
  PSG_REG_ENVELOPE_COARSE,
  PSG_REG_ENVELOPE_FINE,
  PSG_REG_ENVELOPE_CONTROL,
  PSG_REG_IOA,
  PSG_REG_IOB,                      // R15
  PSG_REG_TOTAL
};

//-------------------------------------------------------------------------------------------------
//  Operation                    Registers       Function
//--------------------------+---------------+------------------------------------------------------
// Tone Generator Control        R0 to R5        Program tone periods
// Noise Generator Control       R6              Program noise period
// Mixer Control                 R7              Enable tone and/or noise on selected channels
// Amplitude Control             R8 to R10       Select "fixed" or "envelope-variable" amplitudes
// Envelope Generator Control    R11 to R13      Program envelope period and select envelope pattern
// I/O Ports IOA & IOB           R14 to R15      Program x2 8-bit parallel Input Output Ports
//-------------------------------------------------------------------------------------------------

// The direction (input or output ) of the two general purpose I/O Ports
// (IOA and IOB) is determined by the state of bits Bit 7 and Bit 6 of R7.

// Good cut and paste ref here:
// https://worldofspectrum.org/ZXSpectrum128+3Manual/chapter8pt30.html

#endif
