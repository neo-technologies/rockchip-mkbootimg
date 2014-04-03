OBJS := mkbootimg unpackbootimg

all: $(OBJS)

clean:
	rm -f $(OBJS)

mkbootimg: mkbootimg.c
	gcc -O2 -Wall -Wextra -o mkbootimg mkbootimg.c -lcrypto

unpackbootimg: unpackbootimg.c
	gcc -O2 -Wall -Wextra -o unpackbootimg unpackbootimg.c

install:
	cp $(OBJS) /usr/local/bin
