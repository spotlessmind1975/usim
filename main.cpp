//
//	main.cpp
//

#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <unistd.h>
#include <cstring>

#include "mc6809.h"
#include "mc6850.h"
#include "memory.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
char *strdup(const char *s)
{
	size_t slen = strlen(s);
	char *result = (char*)malloc(slen + 1);
	if (result == NULL)
	{
		return NULL;
	}

	memcpy(result, s, slen + 1);
	return result;
}
#endif

int is_done;
int stop_address;
int org_address;
char *input_filename;
char *output_filename;
int label_count;
char *label_name[1024];
int label_address[1024];
int inspection_count;
char *inspection_name[1024];
int inspection_address[1024];
int inspection_size[1024];
bool binary = false;

static unsigned long htol(char *hex)
{
	char *end;
	unsigned long l = strtol(hex, &end, 16);
	if (*end)
	{
		fprintf(stderr, "bad hex number: %s", hex);
		exit(EXIT_FAILURE);
	}
	return l;
}

static void usage(int status)
{
	FILE *stream = status ? stderr : stdout;
	fprintf(stream, "usage: usim [option ...] <hexfile>\n");
	fprintf(stream, "  -h                -- help (print this message)\n");
	fprintf(stream, "  -b                -- load binary\n");
	fprintf(stream, "  -l addr file      -- load file at addr\n");
	fprintf(stream, "  -R addr           -- run from this address\n");
	fprintf(stream, "  -X addr           -- terminate emulation if PC reaches addr\n");
	fprintf(stream, "\n");
	exit(status);
}

static int doLabels(int argc, char **argv) /* -L file */
{
	if (argc < 2)
		usage(1);
	char *filename = argv[1];
	FILE *handle = fopen(filename, "rt");
	while (!feof(handle))
	{
		char command[1024];
		char address[16];
		char label[16];
		(void)!fscanf(handle, "%s equ %s", label, address);
		(void)!fgets(command, 1024, handle);

		label_name[label_count] = strdup(label);
		label_address[label_count] = org_address + atol(address);
		++label_count;
	}
	fclose(handle);
	return 1;
}

static int doLabels2(int argc, char **argv) /* -L file */
{
	if (argc < 2)
		usage(1);
	char *filename = argv[1];
	FILE *handle = fopen(filename, "rt");
	while (!feof(handle))
	{
		char address[16];
		char label[128];
		(void)!fscanf(handle, "%s %s", address, label);
		if (feof(handle))
			continue;
		printf("%s %s\n", address, label);
		label_name[label_count] = strdup(label);
		label_address[label_count] = htol(address);
		++label_count;
	}
	fclose(handle);
	return 1;
}

static int doInspections(int argc, char **argv) /* -L file */
{
	if (argc < 2)
		usage(1);
	char *filename = argv[1];
	FILE *handle = fopen(filename, "rt");
	while (!feof(handle))
	{
		char address[16];
		char size[16];
		char label[16];
		memset(label, 0, 16);
		(void)!fscanf(handle, "%s %s %s", address, size, label);

		if (strcmp(label, "") == 0)
		{
			continue;
		}
		inspection_name[inspection_count] = strdup(label);
		inspection_size[inspection_count] = htol(size);
		inspection_address[inspection_count] = htol(address);
		++inspection_count;
	}
	fclose(handle);
	return 1;
}

char *listing_instructions[0xffff];
int listing_lines[0xffff];

static int doListing(int argc, char **argv) /* -i file */
{
	if (argc < 2)
		usage(1);
	char *filename = argv[1];
	FILE *handle = fopen(filename, "rt");
	int lastLine = 0;
	while (!feof(handle))
	{
		char line[256];
		(void)!fgets(line, 256, handle);

		char *sp = strstr(line, "; L");

		if (sp != NULL)
		{
			sp += 4;
			lastLine = atoi(sp);
		}

		sp = strchr(line, ' ');
		if (!sp)
			continue;
		if ((sp - line) > 16)
			continue;

		char address[16];
		memset(address, 0, 16);
		memcpy(address, line, (sp - line));
		int pc = htol(address);
		if (pc == 0)
			continue;
		if (pc >= 0x8000)
			continue;

		char *sp2 = strchr(sp + 4, ' ');
		if (!sp2)
			continue;

		char *sp3 = strchr(sp2 + 1, 0);
		if (!sp3)
			continue;

		char instructions[256];
		memset(instructions, 0, 256);
		memcpy(instructions, sp2, (sp3 - sp2));
		if (strcmp(instructions, "") == 0)
			continue;

		if (strlen(instructions) == 0)
			continue;

		listing_instructions[pc] = strdup(instructions);
		listing_lines[pc] = lastLine;

		listing_instructions[pc][strlen(listing_instructions[pc]) - 1] = 0;
	}
	fclose(handle);
	return 1;
}

char *profile_filename;
int profile_cycles;
int profile_heatmap[0xffff];
int profile_heatmap_max;

static int doProfiling(int argc, char **argv) /* -p filename cycles */
{
	char *filename = NULL;
	int cycles = 0;
	if (argc < 2)
		usage(1);
	filename = argv[1];
	cycles = atoi(argv[2]);
	profile_filename = strdup(filename);
	profile_cycles = cycles;
	memset(profile_heatmap, 0, sizeof(int) * 0xffff);
	profile_heatmap_max = 0;
	return 2;
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		usage(EXIT_FAILURE);
	}

	Word loading_address = 0x0000;

	const Word ram_size = 0x8000;
	Word rom_base = 0xe000;
	const Word rom_size = 0x10000 - rom_base;
	Word pc = 0x0000;
	Word trap = 0xffff;
	bool trace = false;

	while (++argv, --argc > 0)
	{
		int n = 0;
		if (!strcmp(*argv, "-h"))
			usage(0);
		else if (!strcmp(*argv, "-l"))
		{
			loading_address = htol(*(argv + 1));
			n = 1;
		}
		else if (!strcmp(*argv, "-R"))
		{
			pc = htol(*(argv + 1));
			n = 1;
		}
		else if (!strcmp(*argv, "-L"))
			n = doLabels(argc, argv);
		else if (!strcmp(*argv, "-L2"))
			n = doLabels2(argc, argv);
		else if (!strcmp(*argv, "-Li"))
			n = doInspections(argc, argv);
		else if (!strcmp(*argv, "-O"))
		{
			output_filename = strdup(*(argv + 1));
			n = 1;
		}
		else if (!strcmp(*argv, "-X"))
		{
			trap = htol(*(argv + 1));
			n = 1;
		}
		else if (!strcmp(*argv, "-b"))
		{
			binary = true;
		}
		else if (!strcmp(*argv, "-t"))
		{
			trace = true;
		}
		else if (!strcmp(*argv, "-i"))
			n = doListing(argc, argv);
		else if (!strcmp(*argv, "-p"))
			n = doProfiling(argc, argv);
		else if ('-' == **argv)
			usage(1);
		else
		{
			input_filename = strdup(*argv);
		}
		argc -= n;
		argv += n;
	}

	// (void)signal(SIGINT, SIG_IGN);

	mc6809 cpu;
	auto ram = std::make_shared<RAM>(ram_size);
	auto rom = std::make_shared<ROM>(rom_size);
	auto acia = std::make_shared<mc6850>();

	cpu.attach(ram, 0x0000, ~(ram_size - 1));
	cpu.attach(rom, rom_base, ~(rom_size - 1));
	cpu.attach(acia, 0xc000, 0xfffe);

	cpu.FIRQ.bind([&]()
				  { return acia->IRQ; });

	if (binary)
	{
		ram->load_binary(input_filename, loading_address);
	}
	else
	{
		ram->load_intelhex(input_filename, loading_address);
	}

	cpu.reset(pc, trap);
	if (trace)
	{
		// cpu.enableTrace();
		cpu.tron(listing_instructions);
	}
	cpu.run();

	if (profile_filename != NULL)
	{
		FILE *profile_heatmap_file = fopen(profile_filename, "wt+");
		fprintf(profile_heatmap_file, "%4.4x %4.4x %4.4x PROFILING\n", profile_heatmap_max, 0, 0);
		for (int i = 0; i < 0xffff; ++i)
		{
			if (listing_instructions[i])
			{
				fprintf(profile_heatmap_file, "%4.4x %4.4x %4.4x %s\n", profile_heatmap[i], i, listing_lines[i], listing_instructions[i]);
			}
		}
		fclose(profile_heatmap_file);
	}

	if (output_filename)
	{
		int i, j = 0, max;
		FILE *handle = fopen(output_filename, "wt");
		fprintf(handle, "%2.2x %2.2x %4.4x %4.4x %4.4x %4.4x %2.2x %2.2x\n",
				cpu.a,
				cpu.b,
				cpu.x,
				cpu.y,
				cpu.u,
				cpu.s,
				cpu.dp,
				cpu.cc.all);
		for (i = 0; i < label_count; ++i)
		{
			if (
				strcmp(label_name[i], "WORKING") == 0 || strcmp(label_name[i], "TEMPORARY") == 0 || strcmp(label_name[i], "DESCRIPTORS") == 0)
			{
				fprintf(handle, "%s\n", label_name[i]);
				if (strcmp(label_name[i], "WORKING") == 0 || strcmp(label_name[i], "TEMPORARY") == 0)
				{
					max = 1024;
					fprintf(handle, "%4.4x ", label_address[i]);
				}
				else
				{
					max = 4 * 255;
				}
				for (j = 0; j < max; ++j)
				{
					fprintf(handle, "%2.2x ", ram->read(label_address[i] + j));
				}
				fprintf(handle, "\n");
			}
			else if (strcmp(label_name[i], "USING") == 0)
			{
				fprintf(handle, "%s\n", label_name[i]);
				fprintf(handle, "%2.2x ", ram->read(label_address[i]));
			}
			else
			{
			}
		}
		for (i = 0; i < label_count; ++i)
		{
			if (
				strcmp(label_name[i], "WORKING") == 0 || strcmp(label_name[i], "TEMPORARY") == 0 || strcmp(label_name[i], "DESCRIPTORS") == 0 || strcmp(label_name[i], "USING") == 0)
			{
			}
			else
			{
				fprintf(handle, "%s %4.4x %2.2x %2.2x %2.2x %2.2x\n", label_name[i], label_address[i], ram->read(label_address[i]), ram->read(label_address[i] + 1), ram->read(label_address[i] + 2), ram->read(label_address[i] + 3));
			}
		}

		for (i = 0; i < inspection_count; ++i)
		{
			fprintf(handle, "%s\n", inspection_name[i]);
			for (j = 0; j < inspection_size[i]; ++j)
			{
				fprintf(handle, "%2.2x ", ram->read(inspection_address[i] + j));
			}
			fprintf(handle, "\n");
		}

		fclose(handle);
	}

	return EXIT_SUCCESS;
}
