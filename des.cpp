#include <hc.hpp>
#include <deque>
#include <iostream>
#include <random>
#include <string>

#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>

#include "test.h"
#include <hc_syscalls.h>

// S5 table from https://en.wikipedia.org/wiki/S-box
static uint8_t sbox[] = {
	0x2, 0xe, 0xc, 0xb, 0x4, 0x2, 0x1, 0xc, 0x7, 0x4, 0xa, 0x7, 0xb, 0xd,
	0x6, 0x1, 0x8, 0x5, 0x5, 0x0, 0x3, 0xf, 0xf, 0xa, 0xb, 0x3, 0x0, 0x9,
	0xe, 0x8, 0x9, 0x6,
	0x4, 0xb, 0x2, 0x8, 0x1, 0xc, 0xb, 0x7, 0xa, 0x1, 0xb, 0xe, 0x7, 0x2,
	0x8, 0xd, 0xf, 0x6, 0x9, 0xf, 0xc, 0x0, 0x5, 0x9, 0x6, 0xa, 0x3, 0x4,
	0x0, 0x5, 0xe, 0x3
};

static uint64_t initial_perm(uint64_t block) [[hc]]
{
	//This is not cryptographically significant
	return block;
}

static uint64_t final_perm(uint64_t block) [[hc]]
{
	//This is not cryptographically significant
	return block;
}

static uint32_t lrotate28(uint32_t a) [[hc]]
{
	return ((a << 1) & 0xfffffff) | ((a >> 27) & 0x1);
}

static uint64_t subkey(uint64_t key, unsigned i) [[hc]]
{
	uint32_t top = (key >> 32) & 0xfffffff, bottom = key & 0xfffffff;
	for (unsigned j = 0; j << i; ++j) {
		top = lrotate28(top);
		bottom = lrotate28(bottom);
	}
	//This si very primitive permutation selection
	return (((uint64_t)top & 0xffffff) << 24) | ((uint64_t)bottom & 0xffffff);
}

static uint32_t faisal(uint32_t halfblock, uint64_t subkey) [[hc]]
{
	uint64_t expanded = 0;
	for (int i = 0; i < 8; ++i)
		expanded |= (uint64_t)halfblock << (i * 2 + 1) & (0x3f << (i * 6));
	halfblock = 0;
	expanded ^= subkey;
	for (int i = 0; i < 8; ++i)
		halfblock |= sbox[(expanded >> (i * 6)) & 0x3f] << (i * 4);
	
	//TODO: we need to permute here
	return halfblock;
}

static uint64_t run_des(uint64_t block, uint64_t key) [[hc]]
{
	block = initial_perm(block);

	uint32_t a = block >> 32, b = block;
	for (int i = 0; i < 16; ++i) {
		uint32_t tmp = a ^ faisal(b, subkey(key, i));
		a = b;
		b = tmp;
	}
	block = ((uint64_t)a << 32) | (uint64_t) b;
	return final_perm(block);
}

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
	uint64_t tile_size = sizeof(uint64_t) * p.wg_size;

	::std::vector<int> ret(p.parallel / p.wg_size);
	::std::vector<::std::string> wdata(p.parallel / p.wg_size);
	// Make sure everyone uses separate buffer

	auto f = [&](hc::tiled_index<1> idx) [[hc]] {
		int tile_i = idx.tile[0];
		int local_i = idx.local[0];
		int global_i = idx.global[0];


		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		if (local_i == 0) {
			uint64_t local_ptr = (uint64_t)&data[global_i];
			ret[tile_i] = sc.send(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  tile_size, tile_size * tile_i});
		}
	};
	auto f_s = [&](hc::tiled_index<1> idx) [[hc]] {
		int tile_i = idx.tile[0];
		int local_i = idx.local[0];
		int global_i = idx.global[0];


		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		if (local_i == 0) {
			uint64_t local_ptr = (uint64_t)&data[global_i];
			ret[tile_i] = sc.send(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  tile_size, tile_size * tile_i});
		}
		idx.barrier.wait();
	};
	auto f_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int tile_i = idx.tile[0];
		int local_i = idx.local[0];
		int global_i = idx.global[0];


		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		if (local_i == 0) {
			uint64_t local_ptr = (uint64_t)&data[global_i];
			do {
				ret[tile_i] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  tile_size, tile_size * tile_i});
			} while (ret[tile_i] == EAGAIN);
		}
	};
	auto f_w_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int tile_i = idx.tile[0];
		int local_i = idx.local[0];
		int global_i = idx.global[0];


		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		if (local_i == 0) {
			uint64_t local_ptr = (uint64_t)&data[global_i];
			sc.wait_one_free();
			ret[tile_i] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  tile_size, tile_size * tile_i});
		}
	};
	auto f_s_n = [&](hc::tiled_index<1> idx) [[hc]] {
		int tile_i = idx.tile[0];
		int local_i = idx.local[0];
		int global_i = idx.global[0];


		for (size_t j = 0; j < p.serial; ++j) {
			data[global_i] = run_des(data[global_i], keys[j]);
		}
		idx.barrier.wait();
		if (local_i == 0) {
			uint64_t local_ptr = (uint64_t)&data[global_i];
			do {
				ret[tile_i] = sc.send_nonblock(SYS_pwrite64,
				         {local_fd, local_ptr,
			                  tile_size, tile_size * tile_i});
			} while (ret[tile_i] == EAGAIN);
		}
		idx.barrier.wait();
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;
	close(fd);

	if (::std::any_of(ret.begin(), ret.end(), [&](int ret) {
		return p.non_block ? (ret != 0) : (ret != tile_size); }))
		::std::cerr << "Not all return values match\n";

	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.parse_option = parse,
	.help = help,
	.name = "pwrite (work-group scope)",
};
