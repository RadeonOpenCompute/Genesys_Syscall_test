#include <hc.hpp>
#include <hc_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <unistd.h>

#include "test.h"

struct bmp_header {
        char header[2];
        uint32_t file_size;
        uint16_t res0;
        uint16_t res1;
        uint32_t offset;
} __attribute__((packed));

struct win_bmp_header {
        uint32_t hdr_size;
        int32_t  width;
        int32_t  height;
        uint16_t planes;
        uint16_t bpp;
        uint32_t compression;
        uint32_t img_size;
        uint32_t horizontal_res;
        uint32_t vertical_res;
        uint32_t color_pallete;
        uint32_t important_pallete;
} __attribute__((packed));

static uint32_t pixel_color(uint8_t r, uint8_t g, uint8_t b, struct fb_var_screeninfo *vinfo) __HC__ __CPU__
{
        return (r<<vinfo->red.offset) | (g<<vinfo->green.offset) | (b<<vinfo->blue.offset);
}


::std::string image;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--picture\tBMP picture to load on screen (Default: none)\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--picture") {
		image = arg;
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

	// Framebuffer control structures
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;

	struct bmp_header bmph;
	struct win_bmp_header wbmph;
	int bmp_fd = -1;
	char *bmp = nullptr;
	if (!image.empty()) {
		bmp_fd = open(image.c_str(), O_RDONLY);
		if (read(bmp_fd, &bmph, sizeof(bmph)) != sizeof(bmph))
			return -1;
		if (bmph.header[0] != 'B' || bmph.header[1] != 'M')
			return -1;
		if (read(bmp_fd, &wbmph, sizeof(wbmph)) != sizeof(wbmph))
			return -1;
		bmp = (char*)mmap(NULL, bmph.file_size, PROT_READ, MAP_PRIVATE, bmp_fd, 0);
	        if (bmp == MAP_FAILED)
			return -1;
	}

	::std::atomic_int fb_ready(0);
	uint64_t fbptr = 0;

	auto f = [&](hc::index<2> idx) [[hc]] {
		int x = idx[0];
		int y = idx[1];
		if (x == 0 && y == 0) {
			unsigned long fb_fd = sc.send(SYS_open, {(uint64_t)"/dev/fb0", O_RDWR});

		        //Get variable screen information
			sc.send(SYS_ioctl, {fb_fd, FBIOGET_VSCREENINFO, (uint64_t)&vinfo});
		        vinfo.grayscale=0;
		        vinfo.bits_per_pixel=32;
			sc.send(SYS_ioctl, {fb_fd, FBIOPUT_VSCREENINFO, (uint64_t)&vinfo});
			sc.send(SYS_ioctl, {fb_fd, FBIOGET_VSCREENINFO, (uint64_t)&vinfo});
			sc.send(SYS_ioctl, {fb_fd, FBIOGET_FSCREENINFO, (uint64_t)&finfo});

		        unsigned long screensize =
				vinfo.yres_virtual * finfo.line_length;

			fbptr = sc.send(SYS_mmap,
					       {0, screensize, PROT_READ | PROT_WRITE,
					        MAP_SHARED, fb_fd, 0});
			fb_ready = 1;
		}
		while (fb_ready == 0);
		uint8_t *fbp = (uint8_t*)fbptr;
		if (y < vinfo.yres && x<vinfo.xres) {

			uint32_t pixel = pixel_color(0xFF,0x00,0xFF, &vinfo);
			if (bmp && x < wbmph.width && y < wbmph.height) {
				long bmp_loc = ((wbmph.height - y - 1) * wbmph.width + x) * (wbmph.bpp / 8) + bmph.offset;
				pixel = pixel_color(bmp[bmp_loc + 2], bmp[bmp_loc + 1], bmp[bmp_loc], &vinfo);
			}
			long location = (x+vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y+vinfo.yoffset) * finfo.line_length;
			*((uint32_t*)(fbp + location)) = pixel;
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
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

	return 0;
};

static int display()
{
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;

	struct bmp_header bmph;
	struct win_bmp_header wbmph;
	int bmp_fd = -1;
	char *bmp = nullptr;

	if (!image.empty()) {
		bmp_fd = open(image.c_str(), O_RDONLY);
		if (read(bmp_fd, &bmph, sizeof(bmph)) != sizeof(bmph))
			return -1;
		if (bmph.header[0] != 'B' || bmph.header[1] != 'M')
			return -1;
		if (read(bmp_fd, &wbmph, sizeof(wbmph)) != sizeof(wbmph))
			return -1;
		bmp = (char*)mmap(NULL, bmph.file_size, PROT_READ, MAP_PRIVATE, bmp_fd, 0);
	        if (bmp == MAP_FAILED)
			return -1;
	}

        int fb_fd = open("/dev/fb0",O_RDWR);

        //Get variable screen information
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
        vinfo.grayscale=0;
        vinfo.bits_per_pixel=32;
        ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);

        ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);

        long screensize = vinfo.yres_virtual * finfo.line_length;

        uint8_t *fbp = (uint8_t*)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

        for (int y=0;y<vinfo.yres;y++)
                for (int x=0;x<vinfo.xres;x++)
                {
                        uint32_t pixel = pixel_color(0xFF,0x00,0xFF, &vinfo);
                        if (bmp && x < wbmph.width && y < wbmph.height) {
                                long bmp_loc = ((wbmph.height - y - 1) * wbmph.width + x) * (wbmph.bpp / 8) + bmph.offset;
                                assert(bmp_loc < bmph.file_size);
                                pixel = pixel_color(bmp[bmp_loc + 2], bmp[bmp_loc + 1], bmp[bmp_loc], &vinfo);
                        }
                        long location = (x+vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y+vinfo.yoffset) * finfo.line_length;
                        *((uint32_t*)(fbp + location)) = pixel;
                }
	return 0;
}

static int run_cpu(const test_params &p, ::std::ostream &O,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = uint64_t;

	::std::vector<T> ret(p.parallel);

	auto start = ::std::chrono::high_resolution_clock::now();
	for (size_t i = 0; i < p.parallel; ++i) {
		for (size_t j = 0; j < p.serial; ++j) {
			ret[i] = display();
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
	.name = "frame buffer",
};
