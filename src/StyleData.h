#include <stdint.h>

extern const unsigned char	*DrumEvtPtr;
extern const unsigned char	*BassEvtPtr;
extern const unsigned char	*GtrEvtPtr;

void			start_play(register unsigned char, register unsigned char);
void			stop_play(register unsigned char, register unsigned char);
int			isStyleQueued(void);
uint32_t		update_play_style(register unsigned char);
uint32_t		queueNextDrumMeas(void);
unsigned char	getMeasureTime(void);
void			setAccompEvtPtr(unsigned char);
void			play_beat(register unsigned char, register unsigned char);
uint32_t		change_style(register unsigned char, register char *, register unsigned char);
uint32_t		selectStyleCategory(register unsigned char);
uint32_t		selectStyle(register unsigned char, register unsigned char);
uint32_t		selectStyleByPtr(register void *, register unsigned char);
void			cycleStyleCategory(void);
unsigned char getStyleDefBass(void);
unsigned char getStyleDefKit(void);
unsigned char getStyleDefGtr(void);
void *		getStyleCategory(register void *);
const char * getStyleCategoryName(register void *);
void *		getCurrentStyleCategory(void);
void *		getStyle(register void *);
void *		get_song_style(register const char *);
uint32_t		set_song_style(register void *);
void *		getFirstStyle(register void *);
char *		getStyleName(register void *);
void *		getCurrentStyle(void);
void *		getPrevStyle(void);
unsigned char lockStyle(register unsigned char);
void			unlockStyle(register unsigned char);
unsigned char lockPlay(register unsigned char);
void			unlockPlay(register unsigned char);

#define VARIATION_INPLAY	0
#define VARIATION_USER		1
unsigned char getCurrVariationNum(register unsigned int);
void			updateStyleCategory(void);
unsigned int isVariation(register unsigned int);
uint32_t		selectStyleVariation(register unsigned char, register unsigned char);
void			resetPlayVariation(void);
unsigned char getStyleDefTempo(void);
void			loadStyle(void *, const char *);
void			freeAccompStyles(void);
void *		create_style_category(const char *);
uint32_t		playFillAndAdvance(register unsigned char);
unsigned char	lockVariation(register unsigned char);
void				unlockVariation(register unsigned char);

#define GTREVTFLAG_PITCHDOWN		0x20
#define GTREVTFLAG_PITCHUP			0x40
#define GTREVTFLAG_SCALESTEP		0x80
#define GTREVTFLAG_SCALESTEPMASK	0x07
#define GTREVTFLAG_STRINGNUMMASK	0x0F
#define GTREVTFLAG_STEPOFFMASK	0xA0
#define GTREVTFLAG_STEPOFF			0xA0
#define GTREVTFLAG_STEPOFFFLAG	0x20
#define GTREVTFLAG_STRINGOFFMASK	0xE0
#define GTREVTFLAG_STRINGOFF		0x60
#define GTREVTFLAG_NONNOTEMASK	0xF0
#define GTREVTFLAG_NONNOTEID		0x10

#define BASSEVTFLAG_OCTDOWN			0x08
#define BASSEVTFLAG_OCTUP				0x10
#define BASSEVTFLAG_SCALESTEPMASK	0x07
#define BASSEVTFLAG_NOTEOFF			0x80
#define BASSEVTFLAG_NOT_ON				0x80

