#include <stdint.h>

void			freeSongSheets(void);
void *		isSongsheetActive(void);
uint32_t		songStart(void);
uint32_t		nextSongBeat(register unsigned char);
void			loadSongSheet(const char *);
uint32_t		selectSongSheet(register unsigned char, register unsigned char);
uint32_t		selectNextSongSheet(void);
void			positionSongGui(void);
void *		getNextSongSheet(register void *);
const char * getSongSheetName(register void *);
void			showSongList(void);

#define SONGFLAG_ADDNINE	0x0001
#define SONGFLAG_ADDSEVEN	0x0002
#define SONGFLAG_CHORD		0x0004
#define SONGFLAG_STYLE		0x0008
#define SONGFLAG_VARIATION	0x0010
#define SONGFLAG_TEMPO		0x0020
#define SONGFLAG_PAD			0x0040
#define SONGFLAG_FILL		0x0080
#define SONGFLAG_BASS		0x0100
#define SONGFLAG_GUITAR		0x0200
#define SONGFLAG_DRUMS		0x0400
#define SONGFLAG_TIMESIG	0x0800
#define SONGFLAG_SONGDONE	0x1000
#define SONGFLAG_SONGEND	0x2000
#define SONGFLAG_REPEAT		0x4000
#define SONGFLAG_REPEATEND	0x8000

extern GUILIST		SongsheetBox;
