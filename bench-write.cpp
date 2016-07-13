#include <amp.h>
#include <deque>
#include <iostream>
#include <string>

#include <sys/syscall.h>
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
	if (opt == "--size") {
		size_t size = ::std::stoi(arg);
		::std::cerr << "Setting data size to " << size << " bytes.\n";
		str = ::std::string(size, 'x');
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
			ret[i] = sc.send(SYS_write,
				         {local_fd, local_str_ptr, local_size});
		}
	};
	auto f_s = [&](concurrency::tiled_index<WG_SIZE> tidx) restrict(amp) {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blockingoperation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			ret[i] = sc.send(SYS_write,
				         {local_fd, local_str_ptr, local_size});
			tidx.barrier.wait();
		}
	};
	auto f_n = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			do {
				ret[i] = sc.send_nonblock(SYS_write,
				         {local_fd, local_str_ptr, local_size});
			} while (ret[i] == EAGAIN);
		}
	};
	auto f_w_n = [&](concurrency::index<1> idx) restrict(amp) {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			sc.wait_one_free();
			ret[i] = sc.send_nonblock(SYS_write,
			         {local_fd, local_str_ptr, local_size});
		}
	};
	auto f_s_n = [&](concurrency::tiled_index<WG_SIZE> tidx) restrict(amp) {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			tidx.barrier.wait();
			do {
				ret[i] = sc.send_nonblock(SYS_write,
				         {local_fd, local_str_ptr, local_size});
			} while (ret[i] == EAGAIN);
			tidx.barrier.wait();
		}
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	if (fd != 1)
		close(fd);

	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {
		return p.non_block ? (ret != 0) : (ret != str.size()); }))
		::std::cerr << "Not all return values match\n";

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
	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {
		return ret != str.size(); }))
		::std::cerr << "Not all return values match\n";
	return 0;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.run_cpu = run_cpu,
	.parse_option = parse,
	.help = help,
	.name = "Hello World",
};
