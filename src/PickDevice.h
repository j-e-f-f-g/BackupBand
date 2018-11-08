#ifndef PICKDEVICE_H
#define PICKDEVICE_H

#include <stdint.h>

// SOUNDDEVINFO's DevFlags
#define DEVFLAG_DEVTYPE_MASK	0x03	// DEVTYPE_NONE, DEVTYPE_AUDIO, DEVTYPE_MIDI, or DEVTYPE_SEQ
#define DEVFLAG_NODEVICE		0x04	// Set if input/output can be disabled
#define DEVFLAG_AUDIOTYPE		0x08	// Set if output/input can stream audio
#define DEVFLAG_MIDITYPE		0x10	// Set if output/input can stream MIDI
#define DEVFLAG_SOFTMIDI		0x20	// Set if MIDI input/output can be done via ALSA Seq API
#define DEVFLAG_JACK				0x40	// Set if audio input/output can be done via JACK
#define DEVFLAG_INPUTDEV		0x80	// Set if an input. Clear if an output

#define DEVTYPE_NONE		0		// User has disabled the device
#define DEVTYPE_AUDIO	1		// ALSA audio if DevHash non-zero, Jack if 0
#define DEVTYPE_MIDI		2		// ALSA raw MIDI
#define DEVTYPE_SEQ		3		// ALSA Seq API. DevHash=0 if no specific host connect, non-0 if host

#pragma pack(1)
struct SOUNDDEVINFO {
	union {
	void *			Handle;
	struct SOUNDDEVINFO * Original;
	};
	uint32_t			DevHash;
	unsigned char	Dev, SubDev;
	unsigned char	DevFlags;
	// Used to prevent different threads from both simultaneously
	// accessing this device.
	unsigned char	Lock;
	int				Card;
};
#pragma pack()

typedef void (*DEVRETFUNC)(register struct SOUNDDEVINFO *, register unsigned char);
typedef unsigned char (*DEVMATCHFUNC)(register struct SOUNDDEVINFO *);

#ifndef NO_SAMPLERATE_SUPPORT
unsigned char userSampleRate(register unsigned char);
#endif
unsigned char	setFrameSize(register unsigned char);
void			doPickSoundDevDlg(register struct SOUNDDEVINFO *, register DEVRETFUNC);
void			positionPickDevGui(void);
void *		findSeqClient(register DEVMATCHFUNC, register unsigned char);
snd_seq_t * openAlsaSeq(register const char *);
void			closeAlsaSeq(void);
struct SOUNDDEVINFO * get_temp_sounddev(void);
#endif
