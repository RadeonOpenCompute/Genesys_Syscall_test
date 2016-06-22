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
	::std::vector<int> ret(p.parallel);

	auto start = ::std::chrono::high_resolution_clock::now();
	parallel_for_each(concurrency::extent<1>(p.parallel),
	                  [&](concurrency::index<1> idx) restrict(amp)
	{
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			if (p.gpu_sync_before)
				sc.wait_all();
			if (p.non_block) {
				do {
					ret[i] = sc.send_nonblock(0);
				} while (ret[i] == EAGAIN);
			} else {
				ret[i] = sc.send(0);
			}
		}
	});
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
