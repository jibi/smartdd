/*

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO. 

  
*/


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <sys/wait.h>

enum bool {
	false,
	true
};

enum pipes {
	/*
	 * dst_reader write dst_fd content into this pipe, 
	 * src_reader read from this.
	 */
	D2S_BUF,

	/*
	 * src_reader write src_fd content into this pipe,
	 * dst_writer read from this
	 */
	S2D_BUF,

	/*
	 * dst_reader ctl pipe
	 */
	DR_CTL,


	N_PIPES
};

char *src, *dst;
unsigned int bs;
char smart_mode;

void
fatal(int code, const char *msg, ...) {
	va_list args;
	char msgbuf[1024];
	char fmtbuf[1024];

	va_start(args, msg);

	snprintf(fmtbuf, sizeof(fmtbuf), "fatal: %s", msg);
	vsnprintf(msgbuf, sizeof(msgbuf), fmtbuf, args);
	fprintf(stderr, "%s\n", msgbuf);

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
		int shift = 0;
		int m = strlen(arg) - 1;

		if (arg[m] == 'k') {
			shift = 10;
			arg[m] = '\x00';
		} else if (arg[m] == 'm') {
			shift = 20;
			arg[m] = '\x00';
		} else if (arg[m] == 'g') {
			shift = 30;
			arg[m] = '\x00';
		}

		bs = atoi(arg + 3) << shift;
	}
}

void
open_fd(int *fd_src, int *fd_dst) {
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	*fd_src = (src ? open(src, O_RDONLY) : STDIN_FILENO);
	smart_mode = 0;

	if (*fd_src == -1) {
		fatal(2, "Cannot open %s", src);
	}

	if (!dst) {
		*fd_dst = STDOUT_FILENO;
	} else {
		if (access(dst, F_OK) != -1) {
			printf("in smart mode\n");
			smart_mode = 1;
		}

		*fd_dst = open(dst, O_RDWR | O_CREAT, mode);
	}

	if (*fd_dst == -1) {
		fatal(2, "Cannot open %s", dst);
	}
}

void
src_reader(int fd_src, int d2s_r, int s2d_w, int dr_ctl_r) {
	char *buf_src, *buf_dst;

	int blocks;
	ssize_t count;
	ssize_t negative_count = -1;
	int dr_running = 1;
	char ctl;

	buf_src = malloc(bs * sizeof(char));
	buf_dst = malloc(bs * sizeof(char));

	blocks = 0;

	while ((count = read(fd_src, buf_src, bs)) > 0) {
		char diff;

		diff = true;
		if (dr_running && smart_mode) {
			/* syncrhonize dst reader process */
			read(dr_ctl_r, &ctl, sizeof(char));

			if (ctl == 'e') {
				dr_running = 0;
				printf
				    ("[+] quitting smart mode at block: %d\n",
				     blocks);
			} else {

				/* read from dst file pipe */
				read(d2s_r, buf_dst, count);

				diff = false;
				for (int i = 0; i < count; i++) {
					if (buf_src[i] != buf_dst[i]) {
						printf
						    ("[+] diff at block: %d\n",
						     blocks);
						diff = true;
						break;
					}
				}
			}
		}

		if (diff) {
			/* tell dst writer process current block and count */
			write(s2d_w, &count, sizeof(ssize_t));
			write(s2d_w, &blocks, sizeof(int));

			write(s2d_w, buf_src, count);

		}
		blocks++;
	}
	write(s2d_w, &negative_count, sizeof(ssize_t));
}

void
dst_reader(int fd_dst, int d2s_w, int dr_ctl_w) {
	char ctl = 'r';

	/* move a block from dst file to d2s pipe */
	while (splice
	       (fd_dst, NULL, d2s_w, NULL, bs, SPLICE_F_NONBLOCK) > 0) {

		/* wait src reader process */
		write(dr_ctl_w, &ctl, sizeof(char));
	}

	ctl = 'e';
	write(dr_ctl_w, &ctl, sizeof(char));
}

void
dst_writer(int fd_dst, int s2d_r) {
	ssize_t count;
	int block;

	/* read current block */
	while (1) {
		read(s2d_r, &count, sizeof(ssize_t));

		if (count == -1) {
			break;
		}

		read(s2d_r, &block, sizeof(int));

		lseek(fd_dst, block * bs, SEEK_SET);

		/* move block from s2d pipe to dst file */
		splice(s2d_r, NULL, fd_dst, NULL, count, 0);
	}

	/* TODO: truncate */
}

int
main(int argc, char *argv[]) {
	int fd_src, fd_dst;
	int pipes[N_PIPES][2];
	pid_t child1, child2;

	src = NULL;
	dst = NULL;
	bs = getpagesize();

	for (int i = 1; i < argc; i++) {
		parse_arg(argv[i]);
	}

	if (bs == 0) {
		fatal(1, "Block size cannot be 0");
	}

	open_fd(&fd_src, &fd_dst);

	for (int i = 0; i < N_PIPES; i++) {
		pipe(pipes[i]);
	}

	child1 = fork();
	if (child1) {

		child2 = fork();
		if (child2) {
			src_reader(fd_src, pipes[D2S_BUF][0],
				   pipes[S2D_BUF][1], pipes[DR_CTL][0]);

		} else {
			dst_writer(fd_dst, pipes[S2D_BUF][0]);
		}

	} else {
		dst_reader(fd_dst, pipes[D2S_BUF][1], pipes[DR_CTL][1]);
	}

	waitpid(child2, NULL, 0);

	return 0;
}

// vim: ts=8
