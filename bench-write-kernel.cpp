#include <hc.hpp>
#include <deque>
#include <iostream>
#include <string>

#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>

#include "test.h"
#include <hc_syscalls.h>

static ::std::deque<::std::string> files;
static ::std::vector<int> fds(1, 1);
static ::std::string str = "Hello World from the GPU!\n";

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--out\twrite output to file\n";
	::std::cerr << "\t--str\tuse given string instead of the default\n";
	::std::cerr << "\t--strn\tuse given string with appended newline\n";
	::std::cerr << "\t--size\tuse generated string of given size instead fo the default\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--out") {
		::std::cerr << "writing to " << arg << " instead of stdout\n";
		fds[0] = open(arg.c_str(), O_CREAT | O_WRONLY, 0666);
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
	if (p.gpu_sync_before && !p.fitsGPU()) {
		::std::cerr << "Error: Configuration would hang the GPU: " << p << "\n";
		return 1;
	}
	str.resize(str.size() * p.parallel, 'y');

	// HCC is very bad with globals
	uint64_t local_fd = fds[0];
	uint64_t local_str_ptr = (uint64_t)str.c_str();
	uint64_t local_size = str.size();

	int ret = -7;

	std::vector<std::atomic_uint> locks(p.serial);
	std::vector<std::atomic_uint> lock_after(p.serial);

	auto f = [&](hc::index<1> idx) [[hc]] {
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blockingoperation guarantees
			// available slots
			if (++locks[j] % p.parallel == 0)
				ret = sc.send(SYS_write,
				        {local_fd, local_str_ptr, local_size});
		}
	};
	auto f_s = [&](hc::tiled_index<1> idx) [[hc]] {
		int i = idx.global[0];
		int l_i = idx.local[0];
		for (size_t j = 0; j < p.serial; ++j) {
			++locks[j];
			if (l_i == 0)
				while (locks[j] != p.parallel);
			idx.barrier.wait();
			if (i == 0)
				ret = sc.send(SYS_write,
				              {local_fd, local_str_ptr,
				               local_size});
			++lock_after[j];
			if (l_i == 0)
				while (lock_after[j] != p.parallel);
			idx.barrier.wait();
		}
	};

	auto f_n = [&](hc::index<1> idx) [[hc]] {
		for (size_t j = 0; j < p.serial; ++j) {
			if (++locks[j] % p.parallel == 0)
				do {
					ret = sc.send_nonblock(SYS_write,
					       {local_fd, local_str_ptr, local_size});
				} while (ret == EAGAIN);
		}
	};
	auto f_w_n = [&](hc::index<1> idx) [[hc]] {
		for (size_t j = 0; j < p.serial; ++j) {
			if (++locks[j] % p.parallel == 0) {
				sc.wait_one_free();
				ret = sc.send_nonblock(SYS_write,
				         {local_fd, local_str_ptr, local_size});
			}
		}
	};
	auto f_s_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int i = idx.global[0];
		int l_i = idx.local[0];
		for (size_t j = 0; j < p.serial; ++j) {
			++locks[j];
			if (l_i == 0)
				while (locks[j] != p.parallel);
			idx.barrier.wait();
			if (i == 0)
				ret = sc.send_nonblock(SYS_write,
				              {local_fd, local_str_ptr,
				               local_size});
			++lock_after[j];
			if (l_i == 0)
				while (lock_after[j] != p.parallel);
			idx.barrier.wait();
		}
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	if (p.non_block ? (ret != 0) : (ret != str.size()) )
		::std::cerr << "Incorrect return value: " << ret << "\n";

	if (local_fd != 1)
		close(local_fd);
	for (auto f : files)
		remove(f.c_str());

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
	auto local_fds = fds;
	const char * local_str_ptr = str.c_str();
	uint64_t local_size = str.size();

	::std::vector<int> ret(p.parallel);
	auto start = ::std::chrono::high_resolution_clock::now();
	for (size_t i = 0; i < p.parallel; ++i)
	{
		int fd = local_fds[i % local_fds.size()];
		for (size_t j = 0; j < p.serial; ++j) {
			if (p.non_block) {
				do {
					ret[i] = write(fd, local_str_ptr,
					          local_size);
				} while (ret[i] == EAGAIN);
			} else {
				ret[i] = write(fd, local_str_ptr, local_size);
			}
		}
	};
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	for (int fd : fds)
		if (fd != 1)
			close(fd);
	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {
		return ret != str.size(); }))
		::std::cerr << "Not all return values match\n";
	for (auto f : files)
		remove(f.c_str());
	return 0;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.run_cpu = run_cpu,
	.parse_option = parse,
	.help = help,
	.name = "write (kernel scope)",
};
