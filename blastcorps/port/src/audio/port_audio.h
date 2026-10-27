/* The platform's audio: audio tasks run on the microcode interpreter
 * (aspmain.c) against the game's RDRAM, and the buffers the game hands the
 * AI go to the output (bc.exe: SDL; bc_headless: --wav).  Plain C types only:
 * os_hw.c (game side, <ultra64.h>) and the host files both include this. */
#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

struct HostOpts;
/* plat_start: options (--wav, --audio-capture, --no-audio) and the live hooks */
void port_audio_init(const struct HostOpts *o);
/* osSpTaskStartGo, M_AUDTASK: physical addresses */
void port_audio_task(unsigned ucode_data, unsigned ucode_data_size, unsigned data_ptr, unsigned data_size);
/* osAiSetNextBuffer got a buffer (physical address, bytes; the DAC rate in
 * VI clocks per sample and the VI clock give the sample rate); `accepted`:
 * the AI's FIFO took it (a full FIFO drops it, as the hardware does) */
void port_audio_ai_buffer(unsigned addr, unsigned bytes, unsigned dacrate, unsigned vi_clock, int accepted);
/* at exit: finish the WAV file, print statistics */
void port_audio_close(void);

#endif
