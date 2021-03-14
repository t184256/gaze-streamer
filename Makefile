LDFLAGS+=-ltobii_stream_engine
LDFLAGS+=-lpthread

all: dual-gaze-streamer

clean:
	rm -f dual-gaze-streamer
