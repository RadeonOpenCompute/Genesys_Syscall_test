#include <hc.hpp>
#include <deque>
#include <iostream>
#include <random>
#include <string>

#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>

#include <hc_syscalls.h>
#include "des.h"
#include "test.h"


static int fd = -1;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--out\twrite output to file\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--out") {
		::std::cerr << "writing to " << arg << " instead of stdout\n";
		fd = open(arg.c_str(), O_CREAT | O_WRONLY, 0666);
		return true;
	}
	return false;
}

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	if (lseek(fd, 0, SEEK_CUR) == -1) {
		::std::cerr << "Error: pwrite works only on seekable fds\n";
		return 1;	
	}

	::std::vector<uint64_t> data(p.parallel, 0xdeadbeefcafebad);


	// This is not proper key generation
	::std::random_device rd;
	::std::vector<uint64_t> keys;
	for (size_t i = 0; i < p.serial; ++i)
		keys.push_back((uint64_t)rd() | ((uint64_t) rd() << 32));

	// HCC is very bad with globals
	uint64_t local_fd = fd;

	::std::vector<int> ret(p.parallel);
	// Make sure everyone uses separate buffer

	auto f = [&](hc::tiled_index<1> idx) [[hc]] {
		int global_i = idx.global[0];

		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		uint64_t local_ptr = (uint64_t)&data[global_i];
		ret[global_i] = sc.send(SYS_pwrite64, {local_fd, local_ptr,
		                  sizeof(uint64_t), sizeof(uint64_t) * global_i});
	};
	auto f_s = [&](hc::tiled_index<1> idx) [[hc]] {
		int global_i = idx.global[0];

		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		uint64_t local_ptr = (uint64_t)&data[global_i];
		ret[global_i] = sc.send(SYS_pwrite64, {local_fd, local_ptr,
			                  sizeof(uint64_t), sizeof(uint64_t) * global_i});
		idx.barrier.wait();
	};
	auto f_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int global_i = idx.global[0];

		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		uint64_t local_ptr = (uint64_t)&data[global_i];
		do {
			ret[global_i] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  sizeof(uint64_t), sizeof(uint64_t) * global_i});
		} while (ret[global_i] == EAGAIN);
	};
	auto f_w_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int global_i = idx.global[0];

		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		uint64_t local_ptr = (uint64_t)&data[global_i];
		sc.wait_one_free();
		ret[global_i] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  sizeof(uint64_t), sizeof(uint64_t) * global_i});
	};
	auto f_s_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int global_i = idx.global[0];

		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		uint64_t local_ptr = (uint64_t)&data[global_i];
		idx.barrier.wait();
		do {
			ret[global_i] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  sizeof(uint64_t), sizeof(uint64_t) * global_i});
		} while (ret[global_i] == EAGAIN);
		idx.barrier.wait();
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	close(fd);

	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {
		return p.non_block ? (ret != 0) : (ret != sizeof(uint64_t)); }))
		::std::cerr << "Not all return values match\n";

	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.parse_option = parse,
	.help = help,
	.name = "des (work-item scope)",
};