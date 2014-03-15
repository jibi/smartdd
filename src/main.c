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
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/fs.h>

#include <queue.h>
#include <block_allocator.h>

/* XXX: try different count sizes */
#define COUNT 1024

int fd_src_r, fd_dst_r, fd_dst_w;

struct {
	char *src_filename;
	char *dst_filename;
	size_t block_size;
	char smart_mode;
} state;

block_pool_t     *src_block_pool;
blocking_queue_t *src_reader_queue;
blocking_queue_t *dst_writer_queue;

pthread_t src_reader_thread;
pthread_t dst_reader_thread;
pthread_t dst_writer_thread;

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
open_fds() {
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	state.smart_mode = 0;

	fd_src_r = (state.src_filename ? open(state.src_filename, O_RDONLY) : STDIN_FILENO);
	if (fd_src_r == -1) {
		fatal(2, "Cannot open %s", state.src_filename);
	}

	if (!state.dst_filename) {
		fd_dst_w = STDOUT_FILENO;
	} else {
		if (access(state.dst_filename, F_OK) != -1) {
			printf("[+] smart mode\n");
			state.smart_mode = 1;
		}

		fd_dst_w = open(state.dst_filename, O_WRONLY | O_CREAT, mode);
		fd_dst_r = open(state.dst_filename, O_RDONLY, mode);
	}

	if (fd_dst_r == -1 || fd_dst_w == -1) {
		fatal(2, "Cannot open %s", state.dst_filename);
	}
}

ssize_t
get_size(int fd) {
	struct stat stat;
	ssize_t size = 0;

	fstat(fd, &stat);

	if (S_ISBLK(stat.st_mode)) {
	  ioctl(fd, BLKGETSIZE64, &size);
	} else if (S_ISREG(stat.st_mode)) {
	  size = stat.st_size;
	} else {
	  fatal(3, "Not a regular file or block device.");
	}

	return size;
}

void
parse_arg(char *arg) {
	if (!strncmp(arg, "if=", 3)) {
		state.src_filename = arg + 3;
	}

	if (!strncmp(arg, "of=", 3)) {
		state.dst_filename = arg + 3;
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

		state.block_size = atoi(arg + 3) << shift;
	}
}

void *
src_reader(void *args) {
	size_t block_n = 0;
	size_t block_size;
	block_list_t *block = get_new_block(src_block_pool);

	while ((block_size = read(fd_src_r, block->block, state.block_size))) {

		block->n    = block_n++;
		block->last = 0;
		block->size = block_size;

		blocking_queue_enqueue(src_reader_queue, block);
		block = get_new_block(src_block_pool);
	}

	block->last = 1;
	blocking_queue_enqueue(src_reader_queue, block);

	return NULL;
}

void *
dst_reader(void *args) {
	block_list_t *src_block;

	block_list_t *dst_block = malloc(sizeof(block_list_t));
	dst_block->block        = malloc(state.block_size);

	while (read(fd_dst_r, dst_block->block, state.block_size)) {
		src_block = blocking_queue_dequeue(src_reader_queue);

		if (state.smart_mode) {
			if (memcmp(src_block->block, dst_block->block, src_block->size) || src_block->last) {
				blocking_queue_enqueue(dst_writer_queue, src_block);
			} else {
				release_block(src_block_pool, src_block);
			}
		} else {
			blocking_queue_enqueue(dst_writer_queue, src_block);
		}
	}

	return NULL;
}

void *
dst_writer(void *args) {
	block_list_t *block;
	char last_block = 0;
	size_t src_size;
	size_t dst_size;

	while (!last_block) {
		block      = blocking_queue_dequeue(dst_writer_queue);
		last_block = block->last;

		if (!last_block) {
			lseek(fd_dst_w, block->n * state.block_size, SEEK_SET);
			write(fd_dst_w, block->block, block->size);
		}

		release_block(src_block_pool, block);
	}

	src_size = get_size(fd_src_r);
	dst_size = get_size(fd_dst_w);

	if(dst_size > src_size) {
		ftruncate(fd_dst_w, src_size);
	}

	close(fd_dst_w);

	return NULL;
}

int
main(int argc, char *argv[]) {
	state.src_filename = NULL;
	state.dst_filename = NULL;
	state.block_size   = getpagesize();

	for (int i = 1; i < argc; i++) {
		parse_arg(argv[i]);
	}

	open_fds();

	if (state.block_size == 0) {
		fatal(1, "Block size cannot be 0");
	}

	src_reader_queue = new_blocking_queue(COUNT + 1, 0);
	dst_writer_queue = new_blocking_queue(COUNT + 1, 0);
	src_block_pool   = new_block_pool(state.block_size, COUNT);

	pthread_create(&src_reader_thread, NULL, src_reader, NULL);
	pthread_create(&dst_reader_thread, NULL, dst_reader, NULL);
	pthread_create(&dst_writer_thread, NULL, dst_writer, NULL);

	pthread_join(dst_writer_thread, NULL);

	return 0;
}

// vim: ts=8
