#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/md5.h>
#include "rkrom.h"
#include "rkafp.h"

unsigned int import_data(const char* infile, void *head, size_t head_len, FILE *fp)
{
	FILE *in_fp = NULL;
	unsigned readlen = 0;
	unsigned char buffer[1024];

	in_fp = fopen(infile, "rb");

	if (!in_fp)
		goto import_end;

	readlen = fread(head, 1, head_len, in_fp);
	if (readlen)
	{
		fwrite(head, 1, readlen, fp);
	}

	while (1)
	{
		int len = fread(buffer, 1, sizeof(buffer), in_fp);

		if (len)
		{
			fwrite(buffer, 1, len, fp);
			readlen += len;
		}

		if (len != sizeof(buffer))
			break;
	}

import_end:
	if (in_fp)
		fclose(in_fp);

	return readlen;
}

void append_md5sum(FILE *fp)
{
	MD5_CTX md5_ctx;
	unsigned char buffer[1024];
	int i;

	MD5_Init(&md5_ctx);
	fseek(fp, 0, SEEK_SET);

	while (1)
	{
		int len = fread(buffer, 1, sizeof(buffer), fp);
		if (len)
		{
			MD5_Update(&md5_ctx, buffer, len);
		}

		if (len != sizeof(buffer))
			break;
	}

	MD5_Final(buffer, &md5_ctx);

	for (i = 0; i < 16; ++i)
	{
		fprintf(fp, "%02x", buffer[i]);
	}
}

int pack_rom(unsigned int chiptype, const char *loader_filename, int majver, int minver, int subver, const char *image_filename, const char *outfile)
{
	time_t nowtime;
	struct tm local_time;
	unsigned int i;

	struct rkfw_header rom_header = {
		.head_code = "RKFW",
		.head_len = 0x66,
		.loader_offset = 0x66
	};

	struct update_header rkaf_header;
	struct bootloader_header loader_header;

	rom_header.chip = chiptype;
	rom_header.version = (((majver) << 24) + ((minver) << 16) + (subver));
	if(chiptype == 0x50){
		rom_header.code = 0x01030000;
	}else if(chiptype == 0x60) {
		rom_header.code = 0x01050000;
	}else if(chiptype == 0x70) {
		rom_header.code = 0x01060000;
	}else if(chiptype == 0x33313241) {
		rom_header.code = 0x01030000;
	}
	nowtime = time(NULL);
	localtime_r(&nowtime, &local_time);

	rom_header.year = local_time.tm_year + 1900;
	rom_header.month = local_time.tm_mon + 1;
	rom_header.day = local_time.tm_mday;
	rom_header.hour = local_time.tm_hour;
	rom_header.minute = local_time.tm_min;
	rom_header.second = local_time.tm_sec;

	FILE *fp = fopen(outfile, "wb+");
	if (!fp)
	{
		fprintf(stderr, "Can't open file %s\n, reason: %s\n", outfile, strerror(errno));
		goto pack_fail;
	}

	unsigned char buffer[0x66];
	if (1 != fwrite(buffer, 0x66, 1, fp))
		goto pack_fail;


	printf("rom version: %x.%x.%x\n",
		(rom_header.version >> 24) & 0xFF,
		(rom_header.version >> 16) & 0xFF,
		(rom_header.version) & 0xFFFF);

	printf("build time: %d-%02d-%02d %02d:%02d:%02d\n",
		rom_header.year, rom_header.month, rom_header.day,
		rom_header.hour, rom_header.minute, rom_header.second);

	printf("chip: %x\n", rom_header.chip);

	fseek(fp, rom_header.loader_offset, SEEK_SET);
	fprintf(stderr, "generate image...\n");
	rom_header.loader_length = import_data(loader_filename, &loader_header, sizeof(loader_header), fp);

	if (rom_header.loader_length <  sizeof(loader_header))
	{
		fprintf(stderr, "invalid loader :\"\%s\"\n",  loader_filename);
		goto pack_fail;
	}

	rom_header.image_offset = rom_header.loader_offset + rom_header.loader_length;
	rom_header.image_length = import_data(image_filename, &rkaf_header, sizeof(rkaf_header), fp);
	if (rom_header.image_length < sizeof(rkaf_header))
	{
		fprintf(stderr, "invalid rom :\"\%s\"\n",  image_filename);
		goto pack_fail;
	}

	rom_header.unknown2 = 1;

	rom_header.system_fstype = 0;

	for (i = 0; i < rkaf_header.num_parts; ++i)
	{
		if (strcmp(rkaf_header.parts[i].name, "backup") == 0)
			break;
	}

	if (i < rkaf_header.num_parts)
		rom_header.backup_endpos = (rkaf_header.parts[i].nand_addr + rkaf_header.parts[i].nand_size) / 0x800;
	else
		rom_header.backup_endpos = 0;

	fseek(fp, 0, SEEK_SET);
	if (1 != fwrite(&rom_header, sizeof(rom_header), 1, fp))
		goto pack_fail;

	fprintf(stderr, "append md5sum...\n");
	append_md5sum(fp);
	fclose(fp);
	fprintf(stderr, "success!\n");

	return 0;
pack_fail:
	if (fp)
		fclose(fp);
	return -1;
}

void usage(const char *appname) {
	const char *p = strrchr(appname, '/');
	p = p ? p + 1 : appname;

	printf("USAGE:\n"
			"%s [chiptype] [loader] [major ver] [minor ver] [subver] [old image] [out image]\n\n"
			"Example:\n"
			"%s -rk30 Loader.bin 1 0 23 rawimage.img rkimage.img \tRK30 board\n"
			"%s -rk31 Loader.bin 4 0 4 rawimage.img rkimage.img \tRK31 board\n"
			"%s -rk3128 Loader.bin 4 0 4 rawimage.img rkimage.img \tRK3128 board\n"
			"%s -rk32 Loader.bin 4 4 2 rawimage.img rkimage.img \tRK32 board\n"
			"%s -rk3368 Loader.bin 5 0 0 rawimage.img rkimage.img \tRK3368 board\n"
			"\n\n"
			"Options:\n"
			"[chiptype]:\n\t-rk29\n\t-rk30\n\t-rk31\n\t-rk3128\n\t-rk32\n\t-rk3368\n", p, p, p, p, p, p);
}

int main(int argc, char **argv)
{
	int ret = 0;
	// loader, majorver, minorver, subver, oldimage, newimage
	if (argc == 8)
	{
		if (strcmp(argv[1], "-rk29") == 0)
		{
			ret = pack_rom(0x50, argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6], argv[7]);
		}
		else if (strcmp(argv[1], "-rk30") == 0)
		{
			ret = pack_rom(0x60, argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6], argv[7]);
		}
		else if (strcmp(argv[1], "-rk31") == 0)
		{
			ret = pack_rom(0x70, argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6], argv[7]);
		}
		else if (strcmp(argv[1], "-rk3128") == 0)
		{
			pack_rom(0x33313241, argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6], argv[7]);
		}
		else if (strcmp(argv[1], "-rk32") == 0)
		{
			ret = pack_rom(0x80, argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6], argv[7]);
		}
		else if (strcmp(argv[1], "-rk3368") == 0)
                  {
                    pack_rom(0x41, argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6], argv[7]);
                  }
		else
		{
			usage(argv[0]);
			return 0;
		}
	}
	else
	{
		usage(argv[0]);
	}

	return ret < 0 ? 1 : 0;
}
