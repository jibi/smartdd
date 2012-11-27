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
#include <sys/stat.h>

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
	D2S_CTL,

	/*
	 * src_reader write src_fd content into this pipe,
	 * dst_writer read from this
	 */
	S2D_BUF,
	S2D_CTL,

	N_PIPES
};

char *src, *dst;
unsigned int bs;
char smart_mode, show_progress;
char null_char;

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
open_fd(int *fd_src, int *fd_dst_r, int *fd_dst_w) {
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	*fd_src = (src ? open(src, O_RDONLY) : STDIN_FILENO);
	smart_mode = 0;

	if (*fd_src == -1) {
		fatal(2, "Cannot open %s", src);
	}

	if (!dst) {
		*fd_dst_w = STDOUT_FILENO;
	} else {
		if (access(dst, F_OK) != -1) {
			printf("in smart mode\n");
			smart_mode = 1;
		}

		*fd_dst_r = open(dst, O_RDWR | O_CREAT, mode);
		*fd_dst_w = open(dst, O_RDWR | O_CREAT, mode);
	}

	if (*fd_dst_r == -1 || *fd_dst_w == -1) {
		fatal(2, "Cannot open %s", dst);
	}
}

void
src_reader(int fd_src, int d2s_buf_r, int d2s_ctl_w, 
    int s2d_ctl_r, int s2d_buf_w) {

	char *buf_src, *buf_dst;
	ssize_t blocks;
	ssize_t count, count2;

	int dr_running = 1;

	buf_src = malloc(bs * sizeof(char));
	buf_dst = malloc(bs * sizeof(char));

	blocks = 0;

	while ((count = read(fd_src, buf_src, bs)) > 0) {
		char diff;
		diff = true;

		if (dr_running && smart_mode) {

			count2 = read(d2s_buf_r, buf_dst, bs);
			write(d2s_ctl_w, &null_char, sizeof(char));

			if (count != count2) {
				dr_running = 0;
				printf("[+] quitting smart mode at block: %d\n", (int) blocks);
			} else {
				diff = false;
				for (ssize_t i = 0; i < count; i++) {
					if (buf_src[i] != buf_dst[i]) {
						printf("[+] diff at block: %d\n", (int) blocks);
						diff = true;
						break;
					}
				}
			}
		}

		if (diff) {
			/* wait for dst writer to be ready */
			read(s2d_ctl_r, &null_char, sizeof(char));

			/* tell dst writer process current block */
			write(s2d_buf_w, &blocks, sizeof(ssize_t));
			write(s2d_buf_w, buf_src, count);
		}
		blocks++;
	}

	close(s2d_buf_w);
}

void
dst_reader(int fd_dst, int d2s_ctl_r, int d2s_buf_w) {
	/* move a block from dst file to d2s_buf pipe */
	while (splice(fd_dst, NULL, d2s_buf_w, NULL, bs, 0) > 0) {

		/* wait src reader to read next block */
		read(d2s_ctl_r, &null_char, sizeof(char));
	}

	close(d2s_buf_w);
}

void
dst_writer(int fd_src, int fd_dst, int s2d_buf_r, int s2d_ctl_w) {
	ssize_t block;
	struct stat stat_src, stat_dst;

	while (1) {
		write(s2d_ctl_w, &null_char, sizeof(char));

		/* read current block */
		if (read(s2d_buf_r, &block, sizeof(ssize_t)) <= 0) break;
		lseek(fd_dst, block * bs, SEEK_SET);

		/* move block from s2d pipe to dst file */
		splice(s2d_buf_r, NULL, fd_dst, NULL, bs, 0);
	}

	fstat(fd_src, &stat_src); 
	fstat(fd_dst, &stat_dst); 

	if(stat_dst.st_size > stat_src.st_size) {
		ftruncate(fd_dst, stat_src.st_size);
	}
}

void 
close_pipes(int pipes[][2], enum pipes *wat, int num, int rw) {
	for (int i = 0; i < num; i++) {
		close(pipes[wat[i]][rw]);
	}
}

int
main(int argc, char *argv[]) {
	int fd_src, fd_dst_r, fd_dst_w;
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

	open_fd(&fd_src, &fd_dst_r, &fd_dst_w);

	for (int i = 0; i < N_PIPES; i++) {
		pipe(pipes[i]);
	}

	child1 = fork();
	if (child1) {

		child2 = fork();
		if (child2) {
			close_pipes(pipes, (enum pipes []) {D2S_CTL, S2D_BUF}, 2, 0);
			close_pipes(pipes, (enum pipes []) {D2S_BUF, S2D_CTL}, 2, 1);

			src_reader(fd_src, pipes[D2S_BUF][0], pipes[D2S_CTL][1], 
			    pipes[S2D_CTL][0], pipes[S2D_BUF][1]);
		} else {
			close_pipes(pipes, (enum pipes []) {S2D_CTL, D2S_BUF, D2S_CTL}, 3, 0);
			close_pipes(pipes, (enum pipes []) {S2D_BUF, D2S_BUF, D2S_CTL}, 3, 1);

			dst_writer(fd_src, fd_dst_w, pipes[S2D_BUF][0], pipes[S2D_CTL][1]);
		}

	} else {
		close_pipes(pipes, (enum pipes []) {S2D_BUF, S2D_CTL, D2S_BUF}, 3, 0);
		close_pipes(pipes, (enum pipes []) {S2D_BUF, S2D_CTL, D2S_CTL}, 3, 1);

		dst_reader(fd_dst_r, pipes[D2S_CTL][0], pipes[D2S_BUF][1]); 
	}

	waitpid(child2, NULL, 0);

	return 0;
}

// vim: ts=8
