# make            一般版（-O3）
# make debug      找越界、溢位、資料競爭：-O1 -g + AddressSanitizer + UBSan
# make tsan       找資料競爭（ThreadSanitizer）
CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -pthread
app: main.c par.h ../pico-app/bench.h
	$(CC) $(CFLAGS) main.c -o app
debug: main.c
	$(CC) -O1 -g -Wall -Wextra -pthread -fsanitize=address,undefined -fno-omit-frame-pointer main.c -o app_debug
tsan: main.c
	$(CC) -O1 -g -pthread -fsanitize=thread main.c -o app_tsan
clean:
	rm -f app app_debug app_tsan
.PHONY: debug tsan clean
