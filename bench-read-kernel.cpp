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

static size_t size = 8;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--size\tamount of space to allocate (Default: 4kB)\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--size") {
		size = ::std::stoi(arg);
		return true;
	}
	return false;
}

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	if (p.non_block || p.gpu_sync_before || p.gpu_wait_before) {
		::std::cerr << "Error: Unsupported configuration: " << p << "\n";
		return 1;
	}

	::std::vector<char> data(size, 'x');
	char name[] = "/tmp/XXXXXXX";
	FILE * tmpf = init_tmp_file(data, p.parallel * p.serial, name);
	int fd = fileno(tmpf);

	// HCC is very bad with globals
	size_t lsize = size * p.parallel;
	uint64_t lfd = fd;

	ssize_t ret = -7;
	::std::vector<char> rdata(lsize);
	::std::vector<std::atomic_uint> lock(p.serial);
	::std::vector<std::atomic_uint> lock_after(p.serial);

	auto f = [&](hc::tiled_index<1> idx) [[hc]] {
		int l_i = idx.local[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			if (lock[j].exchange(1) == 0) {
				uint64_t buf = (uint64_t)rdata.data();
				ret = sc.send(SYS_read, {lfd, buf, lsize});
				lock_after[j] = 1;
			}
			if (l_i == 0)
				while (lock_after[j] != 1);
			idx.barrier.wait();
		}
	};
	auto f_s = [&](hc::tiled_index<1> idx) [[hc]] {
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

	fclose(tmpf);
	remove(name);

	if (ret != lsize) {
		::std::cerr << "Failed read (" << (ssize_t)ret << ")\n";
		return 1;
	}
	if (::std::memcmp(data.data(), rdata.data(), data.size()) != 0) {
		::std::cerr << "GPU read data do not match\n";
		return 1;
	}
	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.parse_option = parse,
	.help = help,
	.name = "read (kernel scope)",
};
