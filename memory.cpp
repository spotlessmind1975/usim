//
//
//	memory.c
//
//	(C) R.P.Bellis 2021
//
//

#include <cstdio>
#include <cstdlib>
#include "memory.h"

static Byte fread_hex_byte(FILE *fp)
{
	char str[3];
	long l;

	str[0] = fgetc(fp);
	str[1] = fgetc(fp);
	str[2] = '\0';

	l = strtol(str, NULL, 16);
	return (Byte)(l & 0xff);
}

static Byte fread_byte(FILE *fp)
{
	return fgetc(fp);
}

static Word fread_hex_word(FILE *fp)
{
	Word ret;

	ret = fread_hex_byte(fp);
	ret <<= 8;
	ret |= fread_hex_byte(fp);

	return ret;
}

void GenericMemory::load_intelhex(const char *filename, Word base)
{
	FILE *fp;
	int done = 0;

	fp = fopen(filename, "r");
	if (!fp)
	{
		perror("filename");
		exit(EXIT_FAILURE);
	}

	while (!done)
	{
		Byte n, t;
		Word addr;
		Byte b;

		(void)fgetc(fp);
		n = fread_hex_byte(fp);
		addr = fread_hex_word(fp);
		t = fread_hex_byte(fp);
		// printf( "%2.2x %4.4x %2.2x\n", n, addr, t );
		if (t == 0x00)
		{
			while (n--)
			{
				b = fread_hex_byte(fp);
				// printf( " ### [%4.4x]: %2.2x\n", addr, b );
				if ((addr >= base) && (addr < ((DWord)base + size)))
				{
					// printf( ">### [%4.4x]: %2.2x\n", addr - base, b );
					memory[addr - base] = b;
				}
				++addr;
			}
		}
		else if (t == 0x01)
		{
			done = 1;
		}
		// Read and discard checksum byte
		(void)fread_hex_byte(fp);
		if (fgetc(fp) == '\r')
			(void)fgetc(fp);
	}
}

void GenericMemory::load_binary(const char *filename, Word base)
{
	FILE *fp;

	fp = fopen(filename, "rb");
	if (!fp)
	{
		perror("filename");
		exit(EXIT_FAILURE);
	}

	Word addr = base;
	while (!feof(fp))
	{
		Byte b = fread_byte(fp);

		if (!feof(fp))
		{
			// printf("%4.4x : %2.2x\n", addr, b);
			memory[addr] = b;
			++addr;
		}
	}
}
