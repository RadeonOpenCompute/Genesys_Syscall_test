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
static int size = 1;
static int divisor = 1;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--out\twrite output to file\n";
	::std::cerr << "\t--size\tnumber of blocks to process per wi\n";
	::std::cerr << "\t--divisor\tarbitrary ratio of gpu compute work\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--out") {
		::std::cerr << "writing to " << arg << " instead of stdout\n";
		fd = open(arg.c_str(), O_CREAT | O_WRONLY, 0666);
		return true;
	}
	if (opt == "--size") {
		::std::cerr << "setting size to " << arg << "\n";
		size = ::std::stoi(arg);
		return true;
	}
	if (opt == "--divisor") {
		::std::cerr << "setting divisor to " << arg << "\n";
		divisor = ::std::stoi(arg);
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
	if (p.gpu_sync_before && !p.fitsGPU()) {
		::std::cerr << "Error: Configuration would hang the GPU: " << p << "\n";
		return 1;
	}

	::std::vector<uint64_t> data(p.parallel * size, 0xdeadbeefcafebad);


	// This is not proper key generation
	::std::random_device rd;
	::std::vector<uint64_t> keys;
	for (size_t i = 0; i < p.serial; ++i)
		keys.push_back((uint64_t)rd() | ((uint64_t) rd() << 32));

	// HCC is very bad with globals
	uint64_t local_fd = fd;
	uint64_t local_size = sizeof(uint64_t) * p.parallel * size;
	uint64_t local_ptr = (uint64_t)data.data();
	size_t wi_size = size;
	int local_div = divisor;
	const uint8_t *lsbox = sbox;

	::std::atomic_uint lock, post_lock;
	lock = 0;
	post_lock = 0;

	::std::vector<int> ret(1);
	// Make sure everyone uses separate buffer

	auto f = [&](hc::tiled_index<1> idx) [[hc]] {
		int local_i = idx.local[0];
		int global_i = idx.global[0];

		for (size_t k = 0; k < wi_size/local_div; ++k)
			for (size_t j = 0; j < p.serial; ++j) {
				data[global_i] = run_des(data[global_i], keys[j], lsbox);
			}
		if (local_i == 0 && ((++lock * idx.tile_dim[0]) == p.parallel)) {
			ret[0] = sc.send(SYS_pwrite64, {local_fd, local_ptr,
			                                local_size, 0});
		}
	};
	auto f_s = [&](hc::tiled_index<1> idx) [[hc]] {
		int local_i = idx.local[0];
		int global_i = idx.global[0];

		for (size_t k = 0; k < wi_size/local_div; ++k)
			for (size_t j = 0; j < p.serial; ++j) {
				data[global_i] = run_des(data[global_i], keys[j], lsbox);
			}
		if (local_i == 0 && ((++lock * idx.tile_dim[0]) == p.parallel)) {
			ret[0] = sc.send(SYS_pwrite64, {local_fd, local_ptr,
			                                local_size, 0});
			post_lock = 1;
		}
		if (local_i == 0)
			while (post_lock != 1);
		idx.barrier.wait();
	};
	auto f_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int local_i = idx.local[0];
		int global_i = idx.global[0];

		for (size_t k = 0; k < wi_size/local_div; ++k)
			for (size_t j = 0; j < p.serial; ++j) {
				data[global_i] = run_des(data[global_i], keys[j], lsbox);
			}
		if (local_i == 0 && ((++lock * idx.tile_dim[0]) == p.parallel)) {
			do {
				ret[0] = sc.send_nonblock(SYS_pwrite64,
				                          {local_fd, local_ptr,
			                                   local_size, 0});
			} while (ret[0] == EAGAIN);
		}
	};
	auto f_w_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int local_i = idx.local[0];
		int global_i = idx.global[0];

		for (size_t k = 0; k < wi_size/local_div; ++k)
			for (size_t j = 0; j < p.serial; ++j) {
				data[global_i] = run_des(data[global_i], keys[j], lsbox);
			}
		if (local_i == 0 && ((++lock * idx.tile_dim[0]) == p.parallel)) {
			sc.wait_one_free();
			ret[0] = sc.send_nonblock(SYS_pwrite64,
				                  {local_fd, local_ptr,
			                           local_size, 0});
		}
	};
	auto f_s_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int local_i = idx.local[0];
		int global_i = idx.global[0];

		for (size_t k = 0; k < wi_size/local_div; ++k)
			for (size_t j = 0; j < p.serial; ++j) {
				data[global_i] = run_des(data[global_i], keys[j], lsbox);
			}
		if (local_i == 0 && ((++lock * idx.tile_dim[0]) == p.parallel)) {
			do {
				ret[0] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  local_size, 0});
			} while (ret[0] == EAGAIN);
			post_lock = 1;
		}
		if (local_i == 0)
			while (post_lock != 1);
		idx.barrier.wait();
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	close(fd);

	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {
		return p.non_block ? (ret != 0) : (ret != local_size); }))
		::std::cerr << "Not all return values match\n";

	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.parse_option = parse,
	.help = help,
	.name = "des (kernel scope)",
};
