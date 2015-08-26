/*-
 *   BSD LICENSE
 *   Copyright(c) 2015 Mirantis Inc. All rights reserved.
 *   Based on `eventfd_copy.c` from the Intel's DPDK

 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "eventfd_copy.h"

static void
usage()
{
	fprintf(stderr,
"test_eventfd_copy (--file-nr [number] | --check)\n"
"\t--file-nr\tchecks that there is no `struct file' leakage by ensuring that\n"
"\t\t\t `/proc/sys/fs/file-nr' value is not growing while `eventfd_copy'ing\n"
"\t\t\t number times (default is 100000)\n"
"\t--check\t\tchecks that the `fd' is moved correctly\n");
}

static int
read_proc_file_nr()
{
	int fd, ret;
	char buf[1024];
	fd = open("/proc/sys/fs/file-nr", O_RDONLY);
	if (fd < 0) {
		perror("open file-nr");
		return -1;
	}

	if (read(fd, buf, 1024) < 0) {
		perror("read");
		close(fd);
		return -1;
	}

	ret = atoi(buf);

	close(fd);

	return ret;
}

static int
check_file_nr(int count)
{
        pid_t pid;
	int i, fd;

	pid = getpid();

	fprintf(stderr, "dummy eventfd_copy check for %d file(s)\n", count);
	fprintf(stdout, "file-nr before: %d\n", read_proc_file_nr());
        for (i = 0; i < count; ++i) {
                fd = eventfd_copy(2, pid);
                if (fd < 0)
			return 1;
                close(fd);
        }
	fprintf(stdout, "file-nr after: %d\n", read_proc_file_nr());

	return 0;
}


#define FD_TO_LINK 42
#define SALT	"The Life, the Universe and everything"

/* checks link:
   0. opens a pipe
   1. forks
   2. child opens tempfile
   3. child dups tempfile fd to fd=42
   4. child writes salt to the tempfile
   5. child notifies parent by writing to a pipe
   6. parent steals child fd
   7. parent kills child
   8. parent checks file content
   9. parent removes tempfile
 */
static int
check_link(void)
{
	pid_t cpid;
	int pipefd[2];
	char buf;

	if (pipe(pipefd) < 0) {
		perror("pipe");
		return 1;
	}

	cpid = fork();
	if (cpid == -1) {
		perror("fork");
		return 1;
	}

	if (cpid) {
		int stolen_fd, ret = 1;
		char buf[1024], tmpfname[1024] = "";

		close(pipefd[1]);

		if ((ret = read(pipefd[0], tmpfname, 1024)) < 0) {
			perror("read pipefd");
			goto parent_out;
		}
		close(pipefd[0]);

		stolen_fd = eventfd_copy(FD_TO_LINK, cpid);
		if (stolen_fd < 0) {
			goto parent_out;
		}

		if (lseek(stolen_fd, 0, SEEK_SET) < 0) {
			perror("lseek");
			goto parent_out;
		}

		if (read(stolen_fd, buf, 1024) < 0) {
			perror("read stolen salt");
			goto parent_out;
		}

		if (strcmp(buf, SALT)) {
			fprintf(stdout, "Stealing FD failed\n");
		}
		else {
			fprintf(stdout, "Stealing FD OK\n");
		}

		close(stolen_fd);

		ret = 0;
parent_out:
		if (tmpfname[0])
			unlink(tmpfname);
		kill(cpid, SIGKILL);
		wait(NULL);
		return ret;
	}

	if (cpid == 0) {
		int fd;
		char fname[] = "/tmp/linkXXXXXX";

		close(pipefd[0]);

		fd = mkstemp(fname);
		if (fd < 0) {
			perror("mkstemp");
			return 1;
		}

		if (dup2(fd, FD_TO_LINK) < 0) {
			perror("dup2");
			close(fd);
			return 1;
		}

		close(fd);

		if (write(FD_TO_LINK, (void*)SALT, strlen(SALT)) < 0) {
			perror("write salt");
			return 1;
		}

		fsync(FD_TO_LINK);

		if (write(pipefd[1], fname, strlen(fname)) < 0) {
			perror("write pipe");
			return 1;
		}

		close(pipefd[1]);

		while (1) {
			sleep(1);
		}

		return 0;
	}
}

int
main(int argc, const char** argv)
{
	if (argc < 2) {
		usage();
		return 0;
	}

	if (!strcmp(argv[1], "--file-nr")) {
		int count = 100000;
		if (argc >= 3)
			count = atoi(argv[2]);
		return check_file_nr(count);
	}

	if (!strcmp(argv[1], "--check")) {
		return check_link();
	}

err:
	usage();
	return 1;
}
