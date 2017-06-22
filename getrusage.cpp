#include <hc.hpp>
#include <hc_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "test.h"

static bool verify = true;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--no-verify\tVerify that info matches between CPU and GPU (Default: verify)\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--no-verify") {
		verify = false;
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
	::std::vector<struct rusage> usage(p.parallel);


	auto f = [&](hc::index<1> idx) [[hc]] {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			ret[i] = sc.send(SYS_getrusage, {RUSAGE_SELF, (uint64_t)&usage[i]});
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			ret[i] = sc.send(SYS_getrusage, {RUSAGE_SELF, (uint64_t)&usage[i]});
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


	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return (ret != 0); }))
		::std::cerr << "Some usage queries failed \n";

	if (verify) {
		struct rusage cpu_u;
		getrusage(RUSAGE_SELF, &cpu_u);

		for (const auto &u : usage)
			if (u.ru_maxrss != cpu_u.ru_maxrss) {
				::std::cerr << "Failed maxrss\n";
				return 1;
			}
	}
	return 0;
};

static int run_cpu(const test_params &p, ::std::ostream &O,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = uint64_t;

	::std::vector<T> ret(p.parallel);
	::std::vector<struct rusage> usage(p.parallel);


	auto start = ::std::chrono::high_resolution_clock::now();
	for (size_t i = 0; i < p.parallel; ++i) {
		for (size_t j = 0; j < p.serial; ++j) {
			ret[i] = getrusage(RUSAGE_SELF, &usage[i]);
		}
	};
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;


	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return (ret != 0); }))
		::std::cerr << "Some usage queries failed \n";
	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.run_cpu = run_cpu,
	.parse_option = parse,
	.help = help,
	.name = "getrusage",
};
