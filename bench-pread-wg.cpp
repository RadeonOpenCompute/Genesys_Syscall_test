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

static size_t size = 4096;

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
	FILE * tmpf = NULL;
	char name[] = "/tmp/XXXXXXX";

	::std::vector<char> data(size, 'x');

	mkstemp(name);
	tmpf = fopen(name, "wb+");
	// TODO: consider writing and testing different patterns
	for (size_t i = 0; i < p.parallel; ++i)
		fwrite(data.data(), 1, data.size(), tmpf);
	fflush(tmpf);
	rewind(tmpf);

	int fd = fileno(tmpf);
	// HCC is very bad with globals
	size_t lsize = size * p.wg_size;
	uint64_t lfd = fd;

	::std::vector<ssize_t> ret(p.parallel / p.wg_size);
	::std::vector<::std::vector<char>> rdata(p.parallel / p.wg_size);
	for (auto &s : rdata)
		s.resize(lsize);

	auto f = [&](hc::tiled_index<1> idx) [[hc]] {
		int i = idx.tile[0];
		int local_i = idx.local[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			if (local_i == 0) {
				uint64_t buf = (uint64_t)rdata[i].data();
				ret[i] = sc.send(SYS_pread64, {lfd, buf, lsize,
				                               lsize * i});
			}
			idx.barrier.wait();
		}
	};
	auto f_s = [&](hc::tiled_index<1> idx) [[hc]] {
		int i = idx.tile[0];
		int local_i = idx.local[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			idx.barrier.wait();
			if (local_i == 0) {
				uint64_t buf = (uint64_t)rdata[i].data();
				ret[i] = sc.send(SYS_pread64, {lfd, buf, lsize,
				                               lsize * i});
			}
			idx.barrier.wait();
		}
	};
	auto f_n = [&](hc::tiled_index<1> idx) [[hc]] {
	};
	auto f_w_n = [&](hc::tiled_index<1> idx) [[hc]] {
	};
	auto f_s_n = [&](hc::tiled_index<1> tidx) [[hc]] {
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;


	if (::std::any_of(ret.begin(), ret.end(), [&](ssize_t ret) {
		return ret != lsize; }))
		::std::cerr << "Failed reads\n";

	if (tmpf) {
		fclose(tmpf);
		remove(name);
	}

	for (size_t i = 0; i < ret.size(); ++i) {
		if (ret[i] != lsize) {
			::std::cerr << "Failed read " << i << " ("
			            << (ssize_t)ret[i] << ")\n";
			return 1;
		}
		if (::std::memcmp(data.data(), rdata[i].data(), data.size()) != 0) {
			::std::cerr << "GPU read data do not match\n";
			return 1;
		}
	}
	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.parse_option = parse,
	.help = help,
	.name = "read",
};
