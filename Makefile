OBJS := mkbootimg unpackbootimg

all: $(OBJS)

mkbootimg:
	gcc -O2 -Wall -Wextra -o mkbootimg mkbootimg.c -lcrypto

unpackbootimg:
	gcc -O2 -Wall -Wextra -o unpackbootimg unpackbootimg.c

install:
	cp $(OBJS) /usr/local/bin
