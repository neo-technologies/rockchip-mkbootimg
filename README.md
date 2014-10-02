# Installation

Compilation needs OpenSSL crypto library:

    sudo apt-get install libssl-dev
    
Build and install:

    make
    sudo make install

# Usage

## afptool
```
USAGE:
	afptool <-pack|-unpack> <Src> <Dest>
Example:
	afptool -pack xxx update.img	Pack files
	afptool -unpack update.img xxx	unpack files
```

## img_maker
```
USAGE:
img_maker [chiptype] [loader] [major ver] [minor ver] [subver] [old image] [out image]

Example:
img_maker -rk30 Loader.bin 1 0 23 rawimage.img rkimage.img 	RK30 board
img_maker -rk31 Loader.bin 4 0 4 rawimage.img rkimage.img 	RK31 board
img_maker -rk32 Loader.bin 4 4 2 rawimage.img rkimage.img 	RK32 board


Options:
[chiptype]:
	-rk29
	-rk30
	-rk31
	-rk32
```

## mkbootimg
```
mkbootimg
       --kernel <filename>
       --ramdisk <filename>
       [ --second <2ndbootloader-filename> ]
       [ --cmdline <kernel-commandline> ]
       [ --board <boardname> ]
       [ --base <address> ]
       [ --pagesize <pagesize> ]
       [ --ramdiskaddr <address> ]
       -o|--output <filename>
```

## unmkbootimg
```
usage: unmkbootimg
       [ --kernel <filename> ]
       [ --ramdisk <filename> ]
       [ --second <2ndbootloader-filename> ]
       -i|--input <filename>
```

## mkrootfs
```
Usage: mkrootfs directory size

    directory   Directory used for the creation of the ext4 rootfs image
    size        Image size in 'dd' format (eg. 256M, 512M, 1G, etc.)
```

## mkupdate
```
Usage: mkupdate directory

    directory must contain package-file with bootloader, parameter and image files
```

## mkcpiogz
```
Usage: mkcpiogz directory
```

## unmkcpiogz
```
Usage: unmkcpiogz initramfs.cpio.gz
```
