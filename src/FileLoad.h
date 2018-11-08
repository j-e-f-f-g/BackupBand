#include <stdint.h>
#include <pthread.h>

#define FILEFLAG_NOEXIST	0x80
#define FILEFLAG_NO_NUL		0x40

void				abort_load(void);
uint32_t			get_style_datapath(register char *);
void *			serviceLoadThread(void);
int				startLoadThread(void);
uint32_t		 	load_text_file(const char *, unsigned char);
void				format_text_err(register const char *, register uint32_t, register unsigned char *);
void				format_file_err(register const char *, register int, register unsigned char);
uint32_t			getInstrumentPath(register char *);

extern unsigned short	WhatToLoadFlag;
extern unsigned char		IgnoreFlag;
extern const char *		NamePtr;
extern unsigned char		DataPathFlag;

// loadDataSets()
#define LOADFLAG_WAVES			0x01
#define LOADFLAG_INPROGRESS	0x01
#define LOADFLAG_STYLES			0x02
#define LOADFLAG_SONGS			0x04
#define LOADFLAG_ASSIGNS		0x08
#define LOADFLAG_DRUMS			0x10
#define LOADFLAG_BASSES			0x20
#define LOADFLAG_GTRS			0x40
#define LOADFLAG_PAD_AND_SOLO	0x80
#define LOADFLAG_INSTRUMENTS (LOADFLAG_DRUMS|LOADFLAG_BASSES|LOADFLAG_GTRS|LOADFLAG_PAD_AND_SOLO)
