#include <hc.hpp>
#include <hc_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdio>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#include <unistd.h>
#include "test.h"

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	struct rlimit lim = {0};
	if (getrlimit(RLIMIT_NOFILE, &lim) != 0) {
		::std::cerr << "Failed to get max fd limit\n";
		return 1;
	}
	// 3 reserver for stding, stdout, stderr
	if (lim.rlim_cur - 3 < p.parallel) {
		::std::cerr << "OS limits do not allow fd for each WI: "
		            << lim.rlim_cur << "\n";
		return 1;
	}

	const char *file = "/dev/null";
	::std::vector<int64_t> ret(p.parallel, -1);
	auto f = [&](hc::index<1> idx) [[hc]] {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			ret[i] = sc.send(SYS_open, {(uint64_t)file, O_RDWR});
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			ret[i] = sc.send(SYS_open, {(uint64_t)file, O_RDWR});
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

	int rc = 0;
	for (auto fd : ret) {
		if (fd < 0) {
			::std::cerr << "Error: invalid descriptor: " << fd << "\n";
			rc = 1;
			continue;
		}
		char buf[10] = {};
		int ret = 0;
		// /dev/null does not support reading, it returns without error
		if ((ret = read(fd, buf, sizeof(buf))) < 0) {
			::std::cerr << "Error: Failed to read from the fd: " << ret << "\n";
			rc = 1;
			continue;
		}
		if ((ret = write(fd, buf, sizeof(buf))) != sizeof(buf)) {
			::std::cerr << "Error: Failed to write to the fd: " << ret << "\n";
			rc = 1;
			continue;
		}
		if ((ret = close(fd)) != 0) {
			::std::cerr << "Error: Failed to close fd: " << ret << "\n";
			rc = 1;
			continue;
		}
	}

	return rc;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.name = "open",
};
