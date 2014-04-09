OBJS := afptool img_maker mkbootimg unpackbootimg

all: $(OBJS)

clean:
	rm -f $(OBJS)

afptool: afptool.c
	gcc -O2 -Wall -Wextra -o afptool afptool.c -lcrypto

img_maker: img_maker.c
	gcc -O2 -Wall -Wextra -o img_maker img_maker.c -lcrypto

mkbootimg: mkbootimg.c
	gcc -O2 -Wall -Wextra -o mkbootimg mkbootimg.c -lcrypto

unpackbootimg: unpackbootimg.c
	gcc -O2 -Wall -Wextra -o unpackbootimg unpackbootimg.c

install:
	cp $(OBJS) /usr/local/bin
