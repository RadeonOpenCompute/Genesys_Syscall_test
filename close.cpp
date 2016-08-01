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

	::std::vector<int64_t> fds(p.parallel, -1);
	for (auto &fd : fds) {
		fd = open("/dev/null", O_RDWR);
		if (fd < 0) {
			::std::cerr << "Failed to open file: " << fd << "\n";
			return 1;
		}
	}

	::std::vector<int64_t> ret(p.parallel, -1);
	auto f = [&](hc::index<1> idx) [[hc]] {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			ret[i] = sc.send(SYS_close, {(uint64_t)fds[i]});
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			ret[i] = sc.send(SYS_close, {(uint64_t)fds[i]});
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

	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {return ret != 0;}))
		::std::cerr << "Found failed \"close\" calls\n";

	for (auto fd : fds) {
		if (close(fd) == 0) {
			::std::cerr << "Error: Closed fd that should not exist\n";
			return 1;
		}
	}

	return 0;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.name = "close",
};
