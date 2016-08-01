#include <hc.hpp>
#include <chrono>
#include <iostream>
#include <string>

#include <unistd.h>
#include <sys/syscall.h>

#include "test.h"
#include <hc_syscalls.h>

static int run(const test_params &p, ::std::ostream &O,
               syscalls &sc, int argc, char **argv)
{
	if (p.cpu) {
		::std::cerr << "Running NOP on CPU is not supported\n";
	}
	// Initialize with non-zero dummy value
	::std::vector<int> ret(p.parallel, -7);


	auto f =
		[&](hc::index<1> idx) [[hc]] {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j)
				ret[i] = sc.send(SYS_restart_syscall);
		};
	auto f_s =
		[&](hc::tiled_index<1> tidx) [[hc]] {
			int i = tidx.global[0];
			for (size_t j = 0; j < p.serial; ++j) {
				tidx.barrier.wait();
				ret[i] = sc.send(SYS_restart_syscall);
				tidx.barrier.wait();
			}
		};
	auto f_n =
		[&](hc::index<1> idx) [[hc]] {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j) {
				do {
					ret[i] = sc.send_nonblock(SYS_restart_syscall);
				} while (ret[i] == EAGAIN);
			}
		};
	auto f_w_n =
		[&](hc::index<1> &idx) [[hc]] {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j) {
				sc.wait_one_free();
				ret[i] = sc.send_nonblock(SYS_restart_syscall);
			}
		};
	auto f_s_n =
		[&](hc::tiled_index<1> &tidx) [[hc]] {
			int i = tidx.global[0];
			for (size_t j = 0; j < p.serial; ++j) {
				tidx.barrier.wait();
				do {
					ret[i] = sc.send_nonblock(SYS_restart_syscall);
				} while (ret[i] == EAGAIN);
				tidx.barrier.wait();
			}
		};
	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;


	for (size_t i = 0; i < ret.size(); ++i) {
		if (ret[i] != 0)
			::std::cerr << "FAIL at " << i << " ("
			            << ret[i] << ")" << ::std::endl;
	}

	return 0;
}

struct test test_instance = {
	.run_gpu = run,
	.run_cpu = no_cpu,
	.name = "NOP",
};
