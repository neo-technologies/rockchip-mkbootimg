OBJS := mkbootimg unpackbootimg

all: $(OBJS)

clean:
	rm -f $(OBJS)

mkbootimg:
	gcc -O2 -Wall -Wextra -o mkbootimg mkbootimg.c -lcrypto

unpackbootimg:
	gcc -O2 -Wall -Wextra -o unpackbootimg unpackbootimg.c

install:
	cp $(OBJS) /usr/local/bin
