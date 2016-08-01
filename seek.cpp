#include <hc.hpp>
#include <hc_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdio>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <unistd.h>
#include "test.h"

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = int64_t;
	int tmp_fd;
	char name[] = "/tmp/XXXXXXX";

	char text[] = "foo";

	mkstemp(name);
	tmp_fd = open(name, O_RDWR | O_CREAT);
	if (pwrite(tmp_fd, "x", 1, p.parallel) != 1) {
		::std::cerr << "failed to extend file\n";
		return 1;
	}
	lseek(tmp_fd, 0, SEEK_SET);

	::std::vector<T> ret(p.parallel);

	auto f = [&](hc::index<1> idx) [[hc]] {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			ret[i] = sc.send(SYS_lseek, {(uint64_t)tmp_fd, 1, SEEK_CUR});
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			ret[i] = sc.send(SYS_lseek, {(uint64_t)tmp_fd, 1, SEEK_CUR});
			tidx.barrier.wait();
		}
	};
	auto f_n = [&](hc::index<1> idx) [[hc]] {
	};
	auto f_w_n = [&](hc::index<1> idx) [[hc]] {
	};
	auto f_s_n = [&](hc::tiled_index<1> tidx) [[hc]] {
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;

	ssize_t current = lseek(tmp_fd, 0, SEEK_CUR);

	close(tmp_fd);
	remove(name);

	if (current != p.parallel) {
		::std::cerr << "Error: unexpected final position: "
		            << current << "\n";
		return 1;
	}

	return 0;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.name = "seek",
};
