#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "rkcrc.h"
#include "rkafp.h"

unsigned int filestream_crc(FILE *fs, size_t stream_len)
{
	char buffer[1024];
	unsigned int crc = 0;

	while (stream_len)
	{
		int read_len = stream_len < sizeof(buffer) ? stream_len : sizeof(buffer);
		read_len = fread(buffer, 1, read_len, fs);
		if (!read_len)
			break;

		RKCRC(crc, buffer, read_len);
		stream_len -= read_len;
	}

	return crc;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// unpack functions

int create_dir(char *dir) {
	char *sep = dir;
	while ((sep = strchr(sep, '/')) != NULL) {
		*sep = '\0';
		if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
			printf("Can't create directory: %s\n", dir);
			return -1;
		}

		*sep = '/';
		sep++;
	}

	return 0;
}

int extract_file(FILE *fp, off_t ofst, size_t len, const char *path) {
	FILE *ofp;
	char buffer[1024];

	if ((ofp = fopen(path, "wb")) == NULL) {
		printf("Can't open/create file: %s\n", path);
		return -1;
	}

	fseeko(fp, ofst, SEEK_SET);
	while (len)
	{
		size_t read_len = len < sizeof(buffer) ? len : sizeof(buffer);
		read_len = fread(buffer, 1, read_len, fp);
		if (!read_len)
			break;
		fwrite(buffer, read_len, 1, ofp);
		len -= read_len;
	}
	fclose(ofp);

	return 0;
}

int unpack_update(const char* srcfile, const char* dstdir) {
	FILE *fp = NULL;
	struct update_header header;
	unsigned int crc = 0;

	fp = fopen(srcfile, "rb");
	if (!fp) {
		fprintf(stderr, "can't open file \"%s\": %s\n", srcfile,
				strerror(errno));
		goto unpack_fail;
	}

	fseek(fp, 0, SEEK_SET);
	if (sizeof(header) != fread(&header, 1, sizeof(header), fp)) {
		fprintf(stderr, "Can't read image header\n");
		goto unpack_fail;
	}

	if (strncmp(header.magic, RKAFP_MAGIC, sizeof(header.magic)) != 0) {
		fprintf(stderr, "Invalid header magic\n");
		goto unpack_fail;
	}

	fseek(fp, header.length, SEEK_SET);
	if (sizeof(crc) != fread(&crc, 1, sizeof(crc), fp))
	{
		fprintf(stderr, "Can't read crc checksum\n");
		goto unpack_fail;
	}

	
	printf("Check file...");
	fflush(stdout);
	fseek(fp, 0, SEEK_SET);
	if (crc != filestream_crc(fp, header.length)) {
		printf("Fail\n");
		goto unpack_fail;
	}
	printf("OK\n");

	printf("------- UNPACK -------\n");
	if (header.num_parts) {
		unsigned i;
		char dir[PATH_MAX];

		for (i = 0; i < header.num_parts; i++) {
			struct update_part *part = &header.parts[i];
			printf("%s\t0x%08X\t0x%08X\n", part->filename, part->pos,
					part->size);

			if (strcmp(part->filename, "SELF") == 0) {
				printf("Skip SELF file.\n");
				continue;
			}

			// parameter 多出文件头8个字节,文件尾4个字节
			if (memcmp(part->name, "parameter", 9) == 0) {
				part->pos += 8;
				part->size -= 12;
			}

			snprintf(dir, sizeof(dir), "%s/%s", dstdir, part->filename);

			if (-1 == create_dir(dir))
				continue;

			if (part->pos + part->size > header.length) {
				fprintf(stderr, "Invalid part: %s\n", part->name);
				continue;
			}

			extract_file(fp, part->pos, part->size, dir);
		}
	}

	fclose(fp);

	return 0;

unpack_fail:
	if (fp) {
		fclose(fp);
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// pack functions

struct pack_part {
	char name[32];
	char filename[60];
	unsigned int nand_addr;
	unsigned int nand_size;
};

struct partition {
	char name[32];
	unsigned int start;
	unsigned int size;
};

typedef struct {
	unsigned int version;

	char machine_model[0x22];
	char machine_id[0x1e];
	char manufacturer[0x38];

	unsigned int num_package;
	struct pack_part packages[16];

	unsigned int num_partition;
	struct partition partitions[16];
} PackImage;

static PackImage package_image;

int parse_partitions(char *str) {
	char *parts;
	char *part, *token1 = NULL, *ptr;
	struct partition *p_part;
	unsigned int i;

	parts = strchr(str, ':');

	if (parts) {
		*parts = '\0';
		parts++;
		part = strtok_r(parts, ",", &token1);

		for (; part; part = strtok_r(NULL, ",", &token1)) {
			p_part = &(package_image.partitions[package_image.num_partition]);

			p_part->size = strtol(part, &ptr, 16);
			ptr = strchr(ptr, '@');
			if (!ptr)
				continue;

			ptr++;
			p_part->start = strtol(ptr, &ptr, 16);

			for (; *ptr && *ptr != '('; ptr++);

			for (i = 0, ptr++; i < sizeof(p_part->name) && *ptr && *ptr != ')'; i++, ptr++)
			{
				p_part->name[i] = *ptr;
			}

			if (i < sizeof(p_part->name))
				p_part->name[i] = '\0';
			else
				p_part->name[i-1] = '\0';

			package_image.num_partition++;
		}

		for (i = 0; i < package_image.num_partition; ++i)
		{
			p_part = &(package_image.partitions[i]);
		}
	}

	return 0;
}

int action_parse_key(char *key, char *value) {
	if (strcmp(key, "FIRMWARE_VER") == 0) {
		unsigned int a, b, c;
		sscanf(value, "%d.%d.%d", &a, &b, &c);
		package_image.version = (a << 24) + (b << 16) + c;
	} else if (strcmp(key, "MACHINE_MODEL") == 0) {
		package_image.machine_model[sizeof(package_image.machine_model) - 1] =
				0;
		strncpy(package_image.machine_model, value,
				sizeof(package_image.machine_model));
		if (package_image.machine_model[sizeof(package_image.machine_model) - 1])
			return -1;
	} else if (strcmp(key, "MACHINE_ID") == 0) {
		package_image.machine_id[sizeof(package_image.machine_id) - 1] = 0;
		strncpy(package_image.machine_id, value,
				sizeof(package_image.machine_id));
		if (package_image.machine_id[sizeof(package_image.machine_id) - 1])
			return -1;
	} else if (strcmp(key, "MANUFACTURER") == 0) {
		package_image.manufacturer[sizeof(package_image.manufacturer) - 1] = 0;
		strncpy(package_image.manufacturer, value,
				sizeof(package_image.manufacturer));
		if (package_image.manufacturer[sizeof(package_image.manufacturer) - 1])
			return -1;
	} else if (strcmp(key, "CMDLINE") == 0) {
		char *param, *token1 = NULL;
		char *param_key, *param_value;
		param = strtok_r(value, " ", &token1);

		while (param) {
			param_key = param;
			param_value = strchr(param, '=');

			if (param_value)
			{
				*param_value = '\0';
				param_value++;

				if (strcmp(param_key, "mtdparts") == 0) {
					parse_partitions(param_value);
				}
			}

			param = strtok_r(NULL, " ", &token1);
		}
	}
	return 0;
}

int parse_parameter(const char *fname) {
	char line[512], *startp, *endp;
	char *key, *value;
	FILE *fp;

	if ((fp = fopen(fname, "r")) == NULL) {
		printf("Can't open file: %s\n", fname);
		return -1;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		startp = line;
		endp = line + strlen(line) - 1;
		if (*endp != '\n' && *endp != '\r' && !feof(fp))
			break;

		// trim line
		while (isspace(*startp))
			++startp;

		while (isspace(*endp))
			--endp;
		endp[1] = 0;

		if (*startp == '#' || *startp == 0)
			continue;

		key = startp;
		value = strchr(startp, ':');

		if (!value)
			continue;

		*value = '\0';
		value++;

		action_parse_key(key, value);
	}

	if (!feof(fp)) {
		printf("File read failed!\n");
		fclose(fp);
		return -3;
	}

	fclose(fp);

	return 0;
}

static struct partition first_partition =
{
		"parameter",
		0,
		0x2000
};

struct partition* find_partition_byname(const char *name)
{
	int i;
	struct partition *p_part;

	for (i = package_image.num_partition - 1; i >= 0; i--)
	{
		p_part = &package_image.partitions[i];
		if (strcmp(p_part->name, name) == 0)
			return p_part;
	}

	if (strcmp(name, first_partition.name) == 0)
	{
		return &first_partition;
	}

	return NULL;
}

struct pack_part* find_package_byname(const char *name)
{
	int i;
	struct pack_part *p_pack;

	for (i = package_image.num_partition - 1; i >= 0; i--)
	{
		p_pack = &package_image.packages[i];
		if (strcmp(p_pack->name, name) == 0)
			return p_pack;
	}

	return NULL;
}

void append_package(const char *name, const char *path)
{
	struct partition *p_part;
	struct pack_part *p_pack = &package_image.packages[package_image.num_package];

	strncpy(p_pack->name, name, sizeof(p_pack->name));
	strncpy(p_pack->filename, path, sizeof(p_pack->filename));

	p_part = find_partition_byname(name);
	if (p_part)
	{
		p_pack->nand_addr = p_part->start;
		p_pack->nand_size = p_part->size;
	} else {
		p_pack->nand_addr = (unsigned int)-1;
		p_pack->nand_size = 0;
	}

	package_image.num_package++;
}

int get_packages(const char *fname)
{
	char line[512], *startp, *endp;
	char *name, *path;
	FILE *fp;

	if ((fp = fopen(fname, "r")) == NULL) {
		printf("Can't open file: %s\n", fname);
		return -1;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		startp = line;
		endp = line + strlen(line) - 1;
		if (*endp != '\n' && *endp != '\r' && !feof(fp))
			break;

		// trim line
		while (isspace(*startp))
			++startp;

		while (isspace(*endp))
			--endp;
		endp[1] = 0;

		// skip UTF-8 BOM
		if (startp[0] == (char)0xEF && startp[1] == (char)0xBB
		 && startp[2] == (char)0xBF)
			startp += 3;

		if (*startp == '#' || *startp == 0)
			continue;

		name = startp;

		while (*startp && *startp != ' ' && *startp != '\t')
			startp++;

		while (*startp == ' ' || *startp == '\t')
		{
			*startp = '\0';
			startp++;
		}

		path = startp;

		append_package(name, path);
	}

	if (!feof(fp)) {
		printf("File read failed!\n");
		fclose(fp);
		return -3;
	}

	fclose(fp);

	return 0;
}

int import_package(FILE *ofp, struct update_part *pack, const char *path)
{
	FILE *ifp;
	char buf[2048];
	size_t readlen;

	pack->pos = ftell(ofp);
	ifp = fopen(path, "rb");
	if (!ifp)
		return -1;

	if (strcmp(pack->name, "parameter") == 0)
	{
		unsigned int crc = 0;
		struct param_header *header = (struct param_header*)buf;
		memcpy(header->magic, "PARM", sizeof(header->magic));

		readlen = fread(buf + sizeof(*header), 1, sizeof(buf) - 12, ifp);
		header->length = readlen;
		RKCRC(crc, buf + sizeof(*header), readlen);
		readlen += sizeof(*header);
		memcpy(buf + readlen, &crc, sizeof(crc));
		readlen += sizeof(crc);
		memset(buf+readlen, 0, sizeof(buf) - readlen);

		fwrite(buf, 1, sizeof(buf), ofp);
		pack->size += readlen;
		pack->padded_size += sizeof(buf);
	} else {
		do {
			readlen = fread(buf, 1, sizeof(buf), ifp);
			if (readlen == 0)
				break;

			if (readlen < sizeof(buf))
				memset(buf + readlen, 0, sizeof(buf) - readlen);

			fwrite(buf, 1, sizeof(buf), ofp);
			pack->size += readlen;
			pack->padded_size += sizeof(buf);
		} while (!feof(ifp));
	}

	fclose(ifp);

	return 0;
}

void append_crc(FILE *fp)
{
	unsigned int crc = 0;
	off_t file_len = 0;

	fseeko(fp, 0, SEEK_END);
	file_len = ftello(fp);

	if (file_len == (off_t) -1)
		return;

	fseek(fp, 0, SEEK_SET);

	printf("Add CRC...\n");

	crc = filestream_crc(fp, file_len);

	fseek(fp, 0, SEEK_END);
	fwrite(&crc, 1, sizeof(crc), fp);
}

int pack_update(const char* srcdir, const char* dstfile) {
	struct update_header header;
	FILE *fp = NULL;
	unsigned int i;

	printf("------ PACKAGE ------\n");
	memset(&header, 0, sizeof(header));

	fp = fopen(dstfile, "wb+");
	if (!fp) {
		printf("Can't open destination file \"%s\": %s\n", dstfile, strerror(errno));
		return -1;
	}

	if (chdir(srcdir))
		return -1;

	if (parse_parameter("parameter"))
		return -1;

	if (get_packages("package-file"))
		return -1;

	fwrite(&header, sizeof(header), 1, fp);

	for (i = 0; i < package_image.num_package; ++i) {
		strcpy(header.parts[i].name, package_image.packages[i].name);
		strcpy(header.parts[i].filename, package_image.packages[i].filename);
		header.parts[i].nand_addr = package_image.packages[i].nand_addr;
		header.parts[i].nand_size = package_image.packages[i].nand_size;

		if (strcmp(package_image.packages[i].filename, "SELF") == 0)
			continue;

		printf("Add file: %s\n", header.parts[i].filename);
		import_package(fp, &header.parts[i], header.parts[i].filename);
	}

	memcpy(header.magic, RKAFP_MAGIC, sizeof(header.magic));
	strcpy(header.manufacturer, package_image.manufacturer);
	strcpy(header.model, package_image.machine_model);
	strcpy(header.id, package_image.machine_id);
	header.length = ftell(fp);
	header.num_parts = package_image.num_package;
	header.version = package_image.version;

	for (i = 0; i < header.num_parts; i++)
	{
		if (strcmp(header.parts[i].filename, "SELF") == 0)
		{
			header.parts[i].size = header.length + 4;
			header.parts[i].padded_size = (header.parts[i].size + 511) / 512 *512;
		}
	}

	fseek(fp, 0, SEEK_SET);
	fwrite(&header, sizeof(header), 1, fp);

	append_crc(fp);

	fclose(fp);

	printf("------ OK ------\n");

	return 0;
}

void usage(const char *appname) {
	const char *p = strrchr(appname, '/');
	p = p ? p + 1 : appname;

	printf("USAGE:\n"
			"\t%s <-pack|-unpack> <Src> <Dest>\n"
			"Example:\n"
			"\t%s -pack xxx update.img\tPack files\n"
			"\t%s -unpack update.img xxx\tunpack files\n", p, p, p);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "-pack") == 0 && argc == 4) {
		if (pack_update(argv[2], argv[3]) == 0) {
			printf("Pack OK!\n");
		} else {
			printf("Pack failed\n");
			return 1;
		}
	} else if (strcmp(argv[1], "-unpack") == 0 && argc == 4) {
		if (unpack_update(argv[2], argv[3]) == 0) {
			printf("UnPack OK!\n");
		} else {
			printf("UnPack failed\n");
			return 1;
		}
	} else {
		usage(argv[0]);
		return 1;
	}

	return 0;
}
