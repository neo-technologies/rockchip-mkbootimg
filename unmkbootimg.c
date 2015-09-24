/* tools/mkbootimg/unmkbootimg.c
**
** Copyright 2013, Pete Batard <pete@akeo.ie>
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <openssl/sha.h>

#include "bootimg.h"

static void *load_file(const char *fn, unsigned *_sz)
{
    char *data;
    size_t sz;
    long offset;
    FILE *fd;

    data = 0;
    fd = fopen(fn, "rb");
    if(fd == 0) return 0;

    if(fseek(fd, 0, SEEK_END) != 0) goto oops;
    offset = ftell(fd);
    if(offset < 0) goto oops;
    sz = offset;

    if(fseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz);
    if(data == 0) goto oops;

    if(fread(data, 1, sz, fd) != sz) goto oops;
    fclose(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
    fclose(fd);
    if(data != 0) free(data);
    return 0;
}

static unsigned save_file(const char *fn, const void* data, const unsigned sz)
{
    FILE *fd;
    size_t _sz = 0;

    fd = fopen(fn, "wb");
    if(fd == 0) return 0;

    _sz = fwrite(data, 1, sz, fd);
    fclose(fd);

    return _sz;
}

int usage(void)
{
    fprintf(stderr,"usage: unmkbootimg\n"
            "       [ --kernel <filename> ]\n"
            "       [ --ramdisk <filename> ]\n"
            "       [ --second <2ndbootloader-filename> ]\n"
            "       -i|--input <filename>\n"
            );
    return 1;
}

static inline unsigned align(unsigned x, unsigned page_size)
{
    return (x + (page_size - 1)) & ~(page_size - 1);
}

int main(int argc, char **argv)
{
    void *file_data = 0;
    unsigned file_size = 0;
    boot_img_hdr *hdr = 0;

    char *kernel_fn = "kernel";
    char *ramdisk_fn = "ramdisk.cpio.gz";
    char *second_fn = "second_bootloader";
    char *bootimg = 0;
    unsigned offset;

    SHA_CTX ctx;
    unsigned char sha[SHA_DIGEST_LENGTH];
    void* kernel_data = 0;
    void* ramdisk_data = 0;
    void* second_data = 0;

    argc--;
    argv++;

    while(argc > 0){
        char *arg = argv[0];
        char *val = argv[1];
        if(argc < 2) {
            return usage();
        }
        argc -= 2;
        argv += 2;
        if(!strcmp(arg, "--input") || !strcmp(arg, "-i")) {
            bootimg = val;
        } else if(!strcmp(arg, "--kernel")) {
            kernel_fn = val;
        } else if(!strcmp(arg, "--ramdisk")) {
            ramdisk_fn = val;
        } else if(!strcmp(arg, "--second")) {
           second_fn = val;
        } else {
            return usage();
        }
    }

    if(bootimg == 0) {
        fprintf(stderr,"error: no input filename specified\n");
        return usage();
    }

    file_data = load_file(bootimg, &file_size);
    if(file_data == 0) {
        fprintf(stderr,"error: could not load image '%s'\n", bootimg);
        return 1;
    }
    if(file_size < sizeof(boot_img_hdr)) {
        fprintf(stderr,"error: file too small for a boot image\n");
        goto fail;
    }
    hdr = (boot_img_hdr *)file_data;
    if (memcmp(hdr->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0) {
        fprintf(stderr,"error: not an Android boot image\n");
        goto fail;
    }

    if(hdr->kernel_size != 0) {
        offset = hdr->page_size;
        if (save_file(kernel_fn, (kernel_data = &((char *)file_data)[offset]),
            hdr->kernel_size) != hdr->kernel_size) {
            fprintf(stderr,"error: could not save kernel '%s'\n", kernel_fn);
            return 1;
        }
        printf("kernel written to '%s' (%d bytes)\n", kernel_fn,
            hdr->kernel_size);
    }

    if(hdr->ramdisk_size != 0) {
        offset = hdr->page_size + align(hdr->kernel_size, hdr->page_size);
        if (save_file(ramdisk_fn, (ramdisk_data = &((char *)file_data)[offset]),
            hdr->ramdisk_size) != hdr->ramdisk_size) {
            fprintf(stderr,"error: could not save ramdisk '%s'\n",
                ramdisk_fn);
            return 1;
        }
        printf("ramdisk written to '%s' (%d bytes)\n", ramdisk_fn,
            hdr->ramdisk_size);
    }

    if(hdr->second_size != 0) {
        offset = hdr->page_size + align(hdr->kernel_size, hdr->page_size) +
                align(hdr->ramdisk_size, hdr->page_size);
        if (save_file(second_fn, (second_data = &((char *)file_data)[offset]),
            hdr->second_size) != hdr->second_size) {
            fprintf(stderr,"error: could not save second bootloader '%s'\n",
                second_fn);
            return 1;
        }
        printf("second bootloader written to '%s' (%d bytes)\n",
            second_fn, hdr->second_size);
    }

    /* Ideally, we'd also check the SHA sums here */
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, kernel_data, hdr->kernel_size);
    SHA1_Update(&ctx, &hdr->kernel_size, sizeof(hdr->kernel_size));
    SHA1_Update(&ctx, ramdisk_data, hdr->ramdisk_size);
    SHA1_Update(&ctx, &hdr->ramdisk_size, sizeof(hdr->ramdisk_size));
    SHA1_Update(&ctx, second_data, hdr->second_size);
    SHA1_Update(&ctx, &hdr->second_size, sizeof(hdr->second_size));
    /* tags_addr, page_size, unused[2], name[], and cmdline[] */
    SHA1_Update(&ctx, &hdr->tags_addr, 4 + 4 + 4 + 4 + 16 + 512);
    SHA1_Final(sha, &ctx);

    int idlen = (SHA_DIGEST_LENGTH > sizeof(hdr->id) ? sizeof(hdr->id) : SHA_DIGEST_LENGTH);
    int res = memcmp(hdr->id, sha, idlen);

    if(res != 0 || idlen != SHA_DIGEST_LENGTH)
    {
        int i;
	unsigned char *p = (unsigned char *) hdr->id;

    	printf("\nSHA1 HASH MISMATCH!\n");
    	printf("  Expected : ");
    	for(i=0;i<SHA_DIGEST_LENGTH;++i)
    	  printf("%02x", sha[i]);
    	printf("\n  Got      : ");
    	for(i=0;i<idlen;++i)
    	  printf("%02x", p[i]);
	printf("\n\n");    	
    }

    printf("\nTo rebuild this boot image, you can use the command:\n");

    /* MUST MATCH WITH THE OFFSETS CALCULATED IN mkbootimg when using --base !! */
    unsigned base = hdr->kernel_addr - 0x00008000;

    printf("  mkbootimg --base 0x%08x --pagesize %d ", base, hdr->page_size);
    if(hdr->name[0] != 0) {
        printf("--board %s ", hdr->name);
    }

    /* Unneeded if we print out base */
    /* printf("--kernel_offset 0x%08x ", hdr->kernel_addr); */

    /* MUST MATCH WITH THE OFFSETS CALCULATED IN mkbootimg when using --base !! */
    /* Only print out commandline options if the offsets cannot be derived from base */
    if(hdr->ramdisk_addr != base + 0x01000000)
    	printf("--ramdisk_offset 0x%08x ", hdr->ramdisk_addr);

    if(hdr->second_addr != base + 0x00F00000)
	printf("--second_offset 0x%08x ", hdr->second_addr);

    if(hdr->tags_addr != base + 0x00000100)
	printf("--tags_offset 0x%08x ", hdr->tags_addr);

    if(hdr->cmdline[0] != 0)
        printf("--cmdline '%s%s' ", hdr->cmdline, hdr->extra_cmdline);

    if(hdr->kernel_size != 0)
        printf("--kernel %s ", kernel_fn);

    if(hdr->ramdisk_size != 0)
        printf("--ramdisk %s ", ramdisk_fn);

    if(hdr->second_size != 0)
        printf("--second %s ", second_fn);

    printf("-o %s\n", bootimg);

    free(file_data);
    return 0;

fail:
    free(file_data);
    return 1;
}
