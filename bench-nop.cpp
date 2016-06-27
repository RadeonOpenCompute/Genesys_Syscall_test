#include <amp.h>
#include <chrono>
#include <iostream>
#include <string>

#include <asm/unistd.h>
#include <unistd.h>

#include "test.h"
#include "amp_syscalls.h"

static int run(const test_params &p, ::std::ostream &O,
               syscalls &sc, int argc, char **argv)
{
	if (p.cpu) {
		::std::cerr << "Running NOP on CPU is not supported\n";
	}
	// Initialize with non-zero dummy value
	::std::vector<int> ret(p.parallel, -7);


	auto f =
		[&](concurrency::index<1> idx) restrict(amp) {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j)
				ret[i] = sc.send(0);
		};
	auto f_s =
		[&](concurrency::index<1> idx) restrict(amp) {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j) {
				sc.wait_all();
				ret[i] = sc.send(0);
			}
		};
	auto f_n =
		[&](concurrency::index<1> idx) restrict(amp) {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j) {
				do {
				ret[i] = sc.send_nonblock(0);
				} while (ret[i] == EAGAIN);
			}
		};
	auto f_w_n =
		[&](concurrency::index<1> idx) restrict(amp) {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j) {
				sc.wait_one_free();
				ret[i] = sc.send_nonblock(0);
			}
		};
	auto f_s_n =
		[&](concurrency::index<1> idx) restrict(amp) {
			int i = idx[0];
			for (size_t j = 0; j < p.serial; ++j) {
				sc.wait_all();
				ret[i] = sc.send_nonblock(0);
			}
		};
	auto start = ::std::chrono::high_resolution_clock::now();
	if (!p.non_block) {
		if (!p.gpu_sync_before)
			parallel_for_each(concurrency::extent<1>(p.parallel), f);
		else
			parallel_for_each(concurrency::extent<1>(p.parallel), f_s);
	} else {
		if (p.gpu_sync_before)
			parallel_for_each(concurrency::extent<1>(p.parallel), f_s);
		else if (p.gpu_wait_before)
			parallel_for_each(concurrency::extent<1>(p.parallel), f_w_n);
		else
			parallel_for_each(concurrency::extent<1>(p.parallel), f_n);
	}
	if (p.non_block && !p.dont_wait_after)
		sc.wait_all();
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
