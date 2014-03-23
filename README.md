# Installation

Compilation needs OpenSSL crypto library:

    sudo apt-get install libssl-dev
    
Build and install:

    make
    sudo make install

# Usage

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
```
unpackbootimg
	-i|--input boot.img
	[ -o|--output output_directory]
	[ -p|--pagesize <size-in-hexadecimal> ]

```
