LDFLAGS+=-ltobii_stream_engine
LDFLAGS+=-lpthread

all: gaze-streamer

clean:
	rm -f gaze-streamer
