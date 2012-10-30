/*

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO. 

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

enum bool {
	false,
	true
};

char *src, *dst;
int fd_src, fd_dst;
unsigned int bs;

char smart_mode = false;

void
fatal(int code, const char *msg, ...) {
	va_list args;
	char msgbuf[1024];
	char fmtbuf[1024];

	va_start(args, msg);

	snprintf(fmtbuf, sizeof(fmtbuf), "fatal: %s\n", msg);
	vsnprintf(msgbuf, sizeof(msgbuf), fmtbuf, args);

	fprintf(stderr, "%s", msgbuf);
	fflush(stderr);

	va_end(args);

	exit(code);
}

void
parse_arg(char *arg) {
	if (!strncmp(arg, "if=", 3)) {
		src = arg + 3;
	}

	if (!strncmp(arg, "of=", 3)) {
		dst = arg + 3;
	}

	if (!strncmp(arg, "bs=", 3)) {
		bs = atoi(arg + 3);
	}
}

void
open_fd() {
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	fd_src = (src ? open(src, O_RDONLY) : STDIN_FILENO);

	if (fd_src == -1) {
		fatal(2, "Cannot open %s", src);
	}

	if (!dst) {
		fd_dst = STDOUT_FILENO;
	} else {
		if (access(dst, F_OK) != -1) {
			printf("in smart mode\n");
			smart_mode = 1;
		}

		fd_dst = open(dst, O_RDWR | O_CREAT, mode);
	}

	if (fd_dst == -1) {
		fatal(2, "Cannot open %s", dst);
	}
}

int
main(int argc, char *argv[]) {

	char *buf_src, *buf_dst;

	unsigned int i;
	unsigned int blocks, count;

	src = NULL;
	dst = NULL;
	bs = getpagesize();
	blocks = 0;

	for (i = 1; i < argc; i++) {
		parse_arg(argv[i]);
	}

	if (bs == 0) {
		fatal(1, "Block size cannot be 0");
	}

	open_fd();

	buf_src = malloc(bs * sizeof(char));
	if (smart_mode) {
		buf_dst = malloc(bs * sizeof(char));
	}

	blocks = 0;
	while ((count = read(fd_src, buf_src, bs)) > 0) {
		if (smart_mode) {
			char diff;
			int j;

			read(fd_dst, buf_dst, count);

			diff = false;
			for (j = 0; j < count; j++) {
				if (buf_src[j] != buf_dst[j]) {
					diff = true;
					break;
				}
			}

			if (diff) {
				printf("diff at block %d\n", blocks);

				lseek(fd_dst, blocks * bs, SEEK_SET);
				write(fd_dst, buf_src, count);
			}
		} else {
			write(fd_dst, buf_src, count);
		}

		blocks++;
	}

	return 0;
}
