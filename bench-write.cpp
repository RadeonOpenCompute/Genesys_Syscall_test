#include <amp.h>
#include <deque>
#include <iostream>
#include <string>

#include <asm/unistd.h>
#include <unistd.h>
#include <fcntl.h>

#include "test.h"
#include "amp_syscalls.h"

static int fd = 1;
static ::std::string str = "Hello World from the GPU!\n";

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--out\twrite output to file\n";
	::std::cerr << "\t--str\tuse given string instead of the default\n";
	::std::cerr << "\t--strn\tuse given string with appended newline\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--out") {
		::std::cerr << "writing to " << arg << " instead of stdout\n";
		fd = open(arg.c_str(), O_CREAT | O_WRONLY, 0666);
		return true;
	}
	if (opt == "--str") {
		::std::cerr << "Setting string to " << arg << " instead of "
		            << str << "\n";
		str = arg;
		return true;
	}
	if (opt == "--strn") {
		::std::cerr << "Setting string to " << arg << "(+newline) "
		            << "instead of " << str << "\n";
		str = arg + '\n';
		return true;
	}
	return false;
}

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	// HCC is very bad with globals
	uint64_t local_fd = fd;
	uint64_t local_str_ptr = (uint64_t)str.c_str();
	uint64_t local_size = str.size();

	::std::vector<int> ret(p.parallel);

	auto f = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blockingoperation guarantees
			// available slots
			ret[i] = sc.send(__NR_write,
				         {local_fd, local_str_ptr, local_size});
		}
	};
	auto f_s = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blockingoperation guarantees
			// available slots. but we can sync across WGs
			sc.wait_all();
			ret[i] = sc.send(__NR_write,
				         {local_fd, local_str_ptr, local_size});
		}
	};
	auto f_n = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			do {
				ret[i] = sc.send_nonblock(__NR_write,
				         {local_fd, local_str_ptr, local_size});
			} while (ret[i] == EAGAIN);
		}
	};
	auto f_w_n = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			sc.wait_one_free();
			ret[i] = sc.send_nonblock(__NR_write,
			         {local_fd, local_str_ptr, local_size});
		}
	};
	auto f_s_n = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			sc.wait_all();
			ret[i] = sc.send_nonblock(__NR_write,
			         {local_fd, local_str_ptr, local_size});
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
	if (fd != 1)
		close(fd);

	return 0;
};

static int run_cpu(const test_params &p, ::std::ostream &O,
                   int argc, char *argv[])
{
	if (p.non_block) {
		::std::cerr << "Non blocking syscalls on CPU are not supported\n";
		return 0;
	}
	// HCC is very bad with globals
	uint64_t local_fd = fd;
	const char * local_str_ptr = str.c_str();
	uint64_t local_size = str.size();

	::std::vector<int> ret(p.parallel);
	auto start = ::std::chrono::high_resolution_clock::now();
	for (size_t i = 0; i < p.parallel; ++i)
	{
		for (size_t j = 0; j < p.serial; ++j) {
			if (p.non_block) {
				do {
					ret[i] = write(local_fd, local_str_ptr,
					          local_size);
				} while (ret[i] == EAGAIN);
			} else {
				ret[i] = write(local_fd, local_str_ptr, local_size);
			}
		}
	};
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	if (fd != 1)
		close(fd);
	for (size_t i = 0; i < ret.size(); ++i) {
		if (ret[i] != str.size())
			::std::cerr << "FAIL at " << i << " ("
			            << ret[i] << ")" << ::std::endl;
	}

	return 0;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.run_cpu = run_cpu,
	.parse_option = parse,
	.help = help,
	.name = "Hello World",
};
