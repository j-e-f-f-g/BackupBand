#include <stdint.h>

extern unsigned char *	TempBuffer;
extern uint32_t			TempBufferSize;
extern const char			NoMemStr[];

void				setErrorStr(const char *);
void				setMemErrorStr(void);
const char *	getErrorStr(void);

void					format_syserr(char *, unsigned int);
uint32_t				get_exe_path(register char *);
uint32_t				get_home_path(register char *);
uint32_t				hash_string(register const unsigned char *);
void					storeLong(register uint32_t, register unsigned char *);
void					storeShort(register uint16_t, register unsigned char *);
uint32_t				getLong(register unsigned char *);
uint16_t				getShort(register unsigned char *);
void *				alloc_temp_buffer(register uint32_t);
void					free_temp_buffer(void);
int32_t				asciiToNum(unsigned char **);
unsigned char *	skip_spaces(register unsigned char *);
int32_t				skip_lines(unsigned char **);
unsigned char *	get_field_id(register unsigned char *, register const void *, register unsigned char *, register uint32_t);

#define FILEFIELD_BADID		255
#define FILEFIELD_MISSING	254
#define FILEFIELD_UNKNOWN	253
#define FILEFIELD_END		252
