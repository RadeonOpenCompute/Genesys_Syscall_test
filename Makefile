MAIN_SRCS= \
	bench-nop.cpp \
	bench-pread-kernel.cpp \
	bench-pread-wg.cpp \
	bench-pread-wi.cpp \
	bench-pwrite-kernel.cpp \
	bench-pwrite-wg.cpp \
	bench-pwrite-wi.cpp \
	bench-read-kernel.cpp \
	bench-read-wg.cpp \
	bench-read-wi.cpp \
	bench-write-kernel.cpp \
	bench-write-wg.cpp \
	bench-write-wi.cpp \
	close.cpp \
	des-wg.cpp \
	mmap-anonymous.cpp \
	mmap-file.cpp \
	munmap.cpp \
	seek.cpp

AUX_SRCS= \
	main.cpp

BINS=$(addprefix test-, $(subst .cpp,,$(MAIN_SRCS)))

SRCS=$(MAIN_SRCS) $(AUX_SRCS)
AUX_OBJS=$(AUX_SRCS:.cpp=.o)
OBJS=$(SRCS:.cpp=.o)
DEPS=$(SRCS:.cpp=.d)

HCC_CONFIG=/opt/hcc-amdgpu/bin/hcc-config
CXX=/opt/hcc-amdgpu/bin/hcc

#hcc-config mixes compiler and preprocessor flags
HCC_CPPFLAGS=$(shell $(HCC_CONFIG) --cxxflags --install)
HCC_CXXFLAGS=$(shell $(HCC_CONFIG) --cxxflags --install)
HCC_LDFLAGS=$(shell $(HCC_CONFIG) --ldflags --install)

CPP_FLAGS=$(HCC_CPPFLAGS)
CXX_FLAGS=$(HCC_CXXFLAGS) -g -O3
LD_FLAGS=$(HCC_LDFLAGS)

all: $(BINS)

test-% : %.o $(AUX_OBJS)
	$(CXX) $^ -o $@ $(LD_FLAGS)

%.o: %.cpp
	$(CXX) -c $< $(CPP_FLAGS) $(CXX_FLAGS) -o $@

%.d: %.cpp
	$(CXX) -MMD -MF $@ $(CPP_FLAGS) $< -E > /dev/null

-include $(DEPS)

clean:
	rm -vf $(OBJS) $(BINS) $(DEPS)
