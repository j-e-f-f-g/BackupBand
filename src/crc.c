// gcc -o crc crc.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/******************* hash_string() ********************
 * Gets a 32-bit hash for the nul-term string.
 */

static uint32_t hash_string(register const unsigned char * str)
{
	register uint32_t hash;
	register uint32_t len;

	hash = 0;
	while ((len = *str++))
	{
		hash += len;
		hash += (hash<<10);
		hash ^= (hash>>6);
	} 
	hash += (hash<<3);
	hash ^= (hash>>11);
	hash += (hash<<15); 

	return hash;
}

int main(int argc, char** argv)
{
	if (argc > 1)
	{
		register uint32_t		mask;

		mask = hash_string((unsigned char *)argv[1]);
		printf("0x%08X\r\n", mask);
	}

	return 0;
}


