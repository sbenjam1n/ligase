# @region:erosion_pd.utils Utilities

CC = gcc
CFLAGS = -Wall -O2 -fvisibility=hidden
PD_INCLUDE = /usr/local/include/pd

# Determine OS and set extension
UNAME := $(shell uname -s)
ifeq ($(UNAME),Linux)
	EXT = pd_linux
	CFLAGS += -fPIC
	LDFLAGS = -shared -lm
endif
ifeq ($(UNAME),Darwin)
	EXT = pd_darwin
	LDFLAGS = -dynamiclib -undefined dynamic_lookup
	PD_INCLUDE = /Applications/Pd-0.53-2.app/Contents/Resources/src
endif

TARGET = ligase~.$(EXT)

SOURCES = src/ligase~.c src/envelope.c src/grain.c src/grain_delay.c src/grain_delay_stut.c src/grain_delay_bencina.c src/grain_distortion.c src/grain_moogladder.c src/grain_fog.c src/kiss_fft.c src/kiss_fftr.c src/reel.c src/splice.c src/perlin.c src/sphere.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -I$(PD_INCLUDE) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) erosion_query_test.o erosion_query_test.$(EXT)

install: $(TARGET)
	mkdir -p ~/Documents/Pd/externals
	cp $(TARGET) ~/Documents/Pd/externals/
	cp ligase~-help.pd ~/Documents/Pd/externals/

# Test random sources
test_random_sources: test_random_sources.c src/grain.o src/perlin.o src/envelope.o src/grain_distortion.o src/grain_delay.o
	$(CC) $(CFLAGS) -o test_random_sources test_random_sources.c src/grain.o src/perlin.o src/envelope.o src/grain_distortion.o src/grain_delay.o -lm

test_random: test_random_sources
	./test_random_sources

# Test pitch scale mode
test_pitch_scale: test_pitch_scale.c src/grain.o src/perlin.o src/envelope.o src/grain_distortion.o src/grain_delay.o
	$(CC) $(CFLAGS) -o test_pitch_scale test_pitch_scale.c src/grain.o src/perlin.o src/envelope.o src/grain_distortion.o src/grain_delay.o -lm

test_pitch: test_pitch_scale
	./test_pitch_scale

# Test tanh distortion with all noise sources
test_distortion: test_distortion.c src/grain.o src/perlin.o src/envelope.o src/grain_distortion.o src/grain_delay.o src/reel.o src/splice.o
	$(CC) $(CFLAGS) -o test_distortion test_distortion.c src/grain.o src/perlin.o src/envelope.o src/grain_distortion.o src/grain_delay.o src/reel.o src/splice.o -lm

test_dist: test_distortion
	./test_distortion

# Test splice markers (Morphagene compatibility test)
test_splice_markers.o: test_splice_markers.c src/types.h
	$(CC) $(CFLAGS) -I$(PD_INCLUDE) -c test_splice_markers.c -o test_splice_markers.o

test_splice_markers~.$(EXT): test_splice_markers.o src/reel.o src/splice.o src/envelope.o src/grain.o src/grain_delay.o src/grain_distortion.o src/perlin.o
	$(CC) $(LDFLAGS) -o test_splice_markers~.$(EXT) test_splice_markers.o src/reel.o src/splice.o src/envelope.o src/grain.o src/grain_delay.o src/grain_distortion.o src/perlin.o

test_splice: test_splice_markers~.$(EXT)
	@echo "Test external built. Create a Pd patch to test it."

# Test query system
erosion_query_test.o: erosion_query_test.c
	$(CC) $(CFLAGS) -I$(PD_INCLUDE) -c erosion_query_test.c -o erosion_query_test.o

erosion_query_test.$(EXT): erosion_query_test.o
	$(CC) $(LDFLAGS) -o erosion_query_test.$(EXT) erosion_query_test.o

test_query: erosion_query_test.$(EXT)
	@echo "Query test external built: erosion_query_test.$(EXT)"
	@echo "Open test_query_system.pd to run tests"

# Test fog bypass/passthrough (FFT->IFFT roundtrip)
test_fog_build: test_fog.c src/grain_fog.o src/kiss_fft.o src/kiss_fftr.o
	$(CC) $(CFLAGS) -o test_fog test_fog.c src/grain_fog.o src/kiss_fft.o src/kiss_fftr.o -lm

test_fog: test_fog_build
	./test_fog

# Regenerate the PDF manual from the Markdown master (docs/ligase_manual.md).
# Requires: pandoc + weasyprint (pip install weasyprint).
manual: docs/ligase_manual.md docs/manual.css
	pandoc docs/ligase_manual.md --toc --toc-depth=1 -s \
		--pdf-engine=weasyprint --css docs/manual.css \
		-o ligase_manual.pdf
	@echo "Regenerated ligase_manual.pdf"

.PHONY: all clean install test_random test_pitch test_dist test_splice test_query test_fog manual

# @endregion:erosion_pd.utils
