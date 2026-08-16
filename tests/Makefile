# tests/Makefile — host-side unit tests (no devkitARM required)
CXX      ?= c++
CXXFLAGS := -std=gnu++17 -g -O0 -Wall -Wextra -Werror -MMD -MP \
            -I. -Ivendor -I../source -I../source/Snes9x
BUILDDIR := build

# Production code must never rely on exceptions/RTTI (CODING_STANDARD.md section 9):
# CORE_SRCS objects are compiled with them disabled; test files keep framework defaults.
CORE_CXXFLAGS := $(CXXFLAGS) -fno-rtti -fno-exceptions

# test sources (add new test_*.cpp here)
TEST_SRCS := test_main.cpp test_stereo_sig.cpp \
	test_dir_entries.cpp test_zip_select.cpp test_msu1_flac.cpp test_rewind_ring.cpp test_rewind_tape.cpp test_rewind_ticks.cpp test_msu1_rewind_defer.cpp test_msu1_helpers.cpp test_msu1_lifecycle.cpp test_msu1_registers_data.cpp test_msu1_registers_audio.cpp test_msu1_audio.cpp test_msu_bridge.cpp test_msu_event_matrix.cpp test_msu1_snapshot.cpp test_msu1_lock_hooks.cpp test_msu1_data_bulk.cpp test_msu1_dma_offsets.cpp test_msu_prefetch.cpp test_msu_user_volume.cpp
# production sources compiled for host (add msu1.cpp / 3dsmsu.cpp as they appear)
CORE_SRCS := msu1.cpp 3dsmsu.cpp

OBJS := $(addprefix $(BUILDDIR)/,$(notdir $(TEST_SRCS:.cpp=.o) $(CORE_SRCS:.cpp=.o)))

VPATH := . ../source ../source/Snes9x

.PHONY: test clean
test: $(BUILDDIR)/run_tests
	./$(BUILDDIR)/run_tests

$(BUILDDIR)/run_tests: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# core objects: exceptions/RTTI disabled (see CORE_CXXFLAGS note above)
$(BUILDDIR)/msu1.o: msu1.cpp | $(BUILDDIR)
	$(CXX) $(CORE_CXXFLAGS) -c -o $@ $<
$(BUILDDIR)/3dsmsu.o: 3dsmsu.cpp | $(BUILDDIR)
	$(CXX) $(CORE_CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

# auto-generated header dependencies (-MMD): a header edit rebuilds every
# object that includes it — without this, struct layout changes produce
# stale-object crashes that look like memory corruption
-include $(OBJS:.o=.d)
