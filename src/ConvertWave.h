// X Windows and cairo for GUI
#include "GuiCtls.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>

typedef struct
{
	void (*DrawFunc)(void);
	void (*MouseFunc)(register GUIMSG *);
	void (*KeyFunc)(register GUIMSG *);
	void (*ResizeFunc)(void);
} GUIFUNCS;

#define USER_CLICKED_OK 1
#define USER_CLICKED_CANCEL 2

