/* tools/mkbootimg/mkbootimg.c
**
** Copyright 2007, The Android Open Source Project
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
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

int usage(void)
{
    fprintf(stderr,"usage: mkbootimg\n"
            "       --kernel <filename>\n"
            "       [ --ramdisk <filename> ]\n"
            "       [ --second <2ndbootloader-filename> ]\n"
            "       [ --cmdline <kernel-commandline> ]\n"
            "       [ --board <boardname> ]\n"
            "       [ --base <address> ]\n"
            "       [ --pagesize <pagesize> ]\n"
            "       [ --kernel_offset <address> ]\n"
            "       [ --ramdisk_offset <address> ]\n"
            "       [ --second_offset <address> ]\n"
            "       [ --tags_offset <address> ]\n"
            "       [ --ramdiskaddr <address> ]\n"
            "       -o|--output <filename>\n"
            );
    return 1;
}



static unsigned char padding[16384] = { 0, };

int write_padding(int fd, unsigned pagesize, unsigned itemsize)
{
    unsigned pagemask = pagesize - 1;
    ssize_t count;

    if((itemsize & pagemask) == 0) {
        return 0;
    }

    count = pagesize - (itemsize & pagemask);

    if(write(fd, padding, count) != count) {
        return -1;
    } else {
        return 0;
    }
}

int main(int argc, char **argv)
{
    boot_img_hdr hdr;

    char *kernel_fn = 0;
    void *kernel_data = 0;
    char *ramdisk_fn = 0;
    void *ramdisk_data = 0;
    char *second_fn = 0;
    void *second_data = 0;
    char *cmdline = "";
    char *bootimg = 0;
    char *board = "";
    unsigned pagesize = 16384;
    int fd;
    SHA_CTX ctx;
    unsigned char sha[SHA_DIGEST_LENGTH];

    argc--;
    argv++;

    memset(&hdr, 0, sizeof(hdr));

        /* default load addresses */
    hdr.kernel_addr =  0x60408000;
    hdr.ramdisk_addr = 0x62000000;
    hdr.second_addr =  0x60F00000;
    hdr.tags_addr =    0x60088000;

    while(argc > 0){
        char *arg = argv[0];
        char *val = argv[1];
        if(argc < 2) {
            return usage();
        }
        argc -= 2;
        argv += 2;
        if(!strcmp(arg, "--output") || !strcmp(arg, "-o")) {
            bootimg = val;
        } else if(!strcmp(arg, "--kernel")) {
            kernel_fn = val;
        } else if(!strcmp(arg, "--ramdisk")) {
            ramdisk_fn = val;
        } else if(!strcmp(arg, "--second")) {
            second_fn = val;
        } else if(!strcmp(arg, "--cmdline")) {
            cmdline = val;
        } else if(!strcmp(arg, "--base")) {
            unsigned base = strtoul(val, 0, 16);
            /* Offsets match stock Android mkbootimg, but differ from defaults above */
            hdr.kernel_addr =  base + 0x00008000;
            hdr.ramdisk_addr = base + 0x01000000;
            hdr.second_addr =  base + 0x00F00000;
            hdr.tags_addr =    base + 0x00000100;
	} else if(!strcmp(arg, "--kernel_offset")) {
	    hdr.kernel_addr = strtoul(val, 0, 16);
	} else if(!strcmp(arg, "--ramdisk_offset")) {
	    hdr.ramdisk_addr = strtoul(val, 0, 16);
	} else if(!strcmp(arg, "--second_offset")) {
	    hdr.second_addr = strtoul(val, 0, 16);
	} else if(!strcmp(arg, "--tags_offset")) {
	    hdr.tags_addr = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--ramdiskaddr")) {
            hdr.ramdisk_addr = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--board")) {
            board = val;
        } else if(!strcmp(arg,"--pagesize")) {
            pagesize = strtoul(val, 0, 10);
            if ((pagesize != 2048) && (pagesize != 4096) && (pagesize != 8192) && (pagesize != 16384)) {
                fprintf(stderr,"error: unsupported page size %d\n", pagesize);
                return -1;
            }
        } else {
            return usage();
        }
    }
    hdr.page_size = pagesize;


    if(bootimg == 0) {
        fprintf(stderr,"error: no output filename specified\n");
        return usage();
    }

    if(kernel_fn == 0) {
        fprintf(stderr,"error: no kernel image specified\n");
        return usage();
    }

    if(strlen(board) >= BOOT_NAME_SIZE) {
        fprintf(stderr,"error: board name too large\n");
        return usage();
    }

    strcpy((char*)hdr.name, board);

    memcpy(hdr.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);

    if(strlen(cmdline) > (BOOT_ARGS_SIZE - 1)) {
        fprintf(stderr,"error: kernel commandline too large\n");
        return 1;
    }
    strcpy((char*)hdr.cmdline, cmdline);

    kernel_data = load_file(kernel_fn, &hdr.kernel_size);
    if(kernel_data == 0) {
        fprintf(stderr,"error: could not load kernel '%s'\n", kernel_fn);
        return 1;
    }

    if(ramdisk_fn == 0 || !strcmp(ramdisk_fn,"NONE")) {
        ramdisk_data = 0;
        hdr.ramdisk_size = 0;
    } else {
        ramdisk_data = load_file(ramdisk_fn, &hdr.ramdisk_size);
        if(ramdisk_data == 0) {
            fprintf(stderr,"error: could not load ramdisk '%s'\n", ramdisk_fn);
            return 1;
        }
    }

    if(second_fn) {
        second_data = load_file(second_fn, &hdr.second_size);
        if(second_data == 0) {
            fprintf(stderr,"error: could not load secondstage '%s'\n", second_fn);
            return 1;
        }
    }

    /* put a hash of the contents in the header so boot images can be
     * differentiated based on their first 2k.
     */
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, kernel_data, hdr.kernel_size);
    SHA1_Update(&ctx, &hdr.kernel_size, sizeof(hdr.kernel_size));
    SHA1_Update(&ctx, ramdisk_data, hdr.ramdisk_size);
    SHA1_Update(&ctx, &hdr.ramdisk_size, sizeof(hdr.ramdisk_size));
    SHA1_Update(&ctx, second_data, hdr.second_size);
    SHA1_Update(&ctx, &hdr.second_size, sizeof(hdr.second_size));
    /* tags_addr, page_size, unused[2], name[], and cmdline[] */
    SHA1_Update(&ctx, &hdr.tags_addr, 4 + 4 + 4 + 4 + 16 + 512);
    SHA1_Final(sha, &ctx);
    memcpy(hdr.id, sha,
           SHA_DIGEST_LENGTH > sizeof(hdr.id) ? sizeof(hdr.id) : SHA_DIGEST_LENGTH);

    fd = open(bootimg, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd < 0) {
        fprintf(stderr,"error: could not create '%s'\n", bootimg);
        return 1;
    }

    if(write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) goto fail;
    if(write_padding(fd, pagesize, sizeof(hdr))) goto fail;

    if(write(fd, kernel_data, hdr.kernel_size) != (ssize_t) hdr.kernel_size) goto fail;
    if(write_padding(fd, pagesize, hdr.kernel_size)) goto fail;

    if(write(fd, ramdisk_data, hdr.ramdisk_size) != (ssize_t) hdr.ramdisk_size) goto fail;
    if(write_padding(fd, pagesize, hdr.ramdisk_size)) goto fail;

    if(second_data) {
        if(write(fd, second_data, hdr.second_size) != (ssize_t) hdr.second_size) goto fail;
        if(write_padding(fd, pagesize, hdr.ramdisk_size)) goto fail;
    }

    return 0;

fail:
    unlink(bootimg);
    close(fd);
    fprintf(stderr,"error: failed writing '%s': %s\n", bootimg,
            strerror(errno));
    return 1;
}
