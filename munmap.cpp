#include <amp.h>
#include <amp_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>

#include <asm/unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "test.h"

static bool print_maps = false;
static size_t size = 4096;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--maps\tPrint content of /proc/self/maps\n";
	::std::cerr << "\t--size\tamount of space to allocate (Default: 4kB)\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--maps") {
		print_maps = true;
		return true;
	}
	if (opt == "--size") {
		size = ::std::stoi(arg);
		return true;
	}
	return false;
}

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = uint64_t;

	::std::vector<T> ret(p.parallel);


	// HCC is very bad with globals
	size_t lsize = size;

	for (T &val : ret) {
		val = (uint64_t)mmap(NULL, lsize, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}

	auto f = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			uint64_t ptr = ret[i];
			ret[i] = sc.send(__NR_munmap, {ptr, lsize});
		}
	};
	auto f_s = [&](concurrency::tiled_index<WG_SIZE> tidx) restrict(amp) {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			uint64_t ptr = ret[i];
			ret[i] = sc.send(__NR_munmap, {ptr, lsize});
			tidx.barrier.wait();
		}
	};
	auto f_n = [&](concurrency::index<1> idx) restrict(amp) {
	};
	auto f_w_n = [&](concurrency::index<1> idx) restrict(amp) {
	};
	auto f_s_n = [&](concurrency::tiled_index<WG_SIZE> tidx) restrict(amp) {
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;

	if (print_maps) {
		static char buffer [4096] = {0};
		::std::ifstream maps("/proc/self/maps");
		while (maps.good()) {
			size_t count = maps.read(buffer, sizeof(buffer)).gcount();
			::std::cout.write(buffer, count);
		}
		maps.close();
	}

	for (const T& r : ret)
	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return ret != 0; })) {
		::std::cerr << "Some memory deallocations failed \n";
		return 1;
	}

	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.parse_option = parse,
	.help = help,
	.name = "munmap",
};
