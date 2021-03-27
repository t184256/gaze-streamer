#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <unistd.h>

#define check(e) assert((e) == TOBII_ERROR_NO_ERROR)

/* time helper function */

/*
uint64_t time_usec() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return 1000000ull * tv.tv_sec + tv.tv_usec;
}
*/


/* global terminating flag */
bool global_terminating;

void terminating_wrap_up(int signum) {
	global_terminating = 1;
}

void terminating_configure_signals() {
	signal(SIGINT, terminating_wrap_up);
	signal(SIGTERM, terminating_wrap_up);
}


/* UDP socket */

int global_sock = 0;
pthread_mutex_t global_sock_mutex = PTHREAD_MUTEX_INITIALIZER;

void global_sock_init(char* host, char* port) {
	assert(!global_sock);
	struct addrinfo* ai;
	struct addrinfo hints = {.ai_family=AF_UNSPEC, .ai_socktype=SOCK_DGRAM};
	assert(getaddrinfo(host, port, &hints, &ai) == 0);
	assert(!pthread_mutex_lock(&global_sock_mutex));
	global_sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	assert(global_sock > 0);
	assert(connect(global_sock, ai->ai_addr, ai->ai_addrlen) != -1);
	assert(!pthread_mutex_unlock(&global_sock_mutex));
}

void gaze_report(char* uri, float x, float y) {
	int l;
	char message[128];
	l = snprintf(message, 128, "%s gaze: %+.20f %+.020f\n", uri, x, y);
	//fputs(message, stdout);
	assert(!pthread_mutex_lock(&global_sock_mutex));
	assert(global_sock > 0);
	send(global_sock, message, strlen(message), 0);
	assert(!pthread_mutex_unlock(&global_sock_mutex));
}


/* global tobii context */

struct tobii_context {
	tobii_api_t* api;
	unsigned int usage_counter;
	pthread_mutex_t mutex;
};
struct tobii_context global_tobii_context = {0, 0, PTHREAD_MUTEX_INITIALIZER};

struct tobii_context* tobii_context_init() {
	assert(!pthread_mutex_lock(&global_tobii_context.mutex));
	if (!global_tobii_context.usage_counter) {
		assert(!global_tobii_context.api);
		check(tobii_api_create(&global_tobii_context.api, NULL, NULL));
	}
	global_tobii_context.usage_counter++;
	assert(!pthread_mutex_unlock(&global_tobii_context.mutex));
	return &global_tobii_context;
}

void tobii_context_deinit() {
	assert(!pthread_mutex_lock(&global_tobii_context.mutex));
	global_tobii_context.usage_counter--;
	if (!global_tobii_context.usage_counter) {
		check(tobii_api_destroy(global_tobii_context.api));
	}
	assert(!pthread_mutex_unlock(&global_tobii_context.mutex));
}


/* tracker */

struct tracker {
	char* uri;
	tobii_device_t* dev;
	uint64_t timestamp_us;
	uint64_t first_timestamp_us;
	bool started;
	bool initialized;
	bool valid;
	float x;
	float y;
	pthread_mutex_t mutex;
	bool new_data;
	//bool terminating;
};

struct tracker* tracker_init(const char* uri) {
	struct tobii_context* ctx = tobii_context_init();
	struct tracker* t = calloc(1, sizeof(struct tracker));
	t->uri = strdup(uri);
	t->x = t->y = NAN;
	pthread_mutex_init(&t->mutex, NULL);
	check(tobii_device_create(ctx->api, uri, &t->dev));
	return t;
}

void _gaze_point_callback(tobii_gaze_point_t const *gaze_pt, void *tracker_) {
	// already locked by tracker_read
	struct tracker* t = (struct tracker*) tracker_;
	t->initialized = 1;
	assert(t->started);
	t->timestamp_us = gaze_pt->timestamp_us;
	t->valid = gaze_pt->validity == TOBII_VALIDITY_VALID;
	if (t->valid) {
		float* pos_xy = (float*) gaze_pt->position_xy;
		t->x = pos_xy[0];
		t->y = pos_xy[1];
	} else {
		t->x = t->y = NAN;
	}
	t->new_data = 1;
}

void tracker_start(struct tracker* t) {
	assert(!pthread_mutex_lock(&t->mutex));
	assert(!t->started);
	assert(!t->initialized);
	assert(!t->valid);
	assert(!t->timestamp_us);
	assert(!t->first_timestamp_us);
	assert(isnan(t->x) && isnan(t->y));
	check(tobii_gaze_point_subscribe(t->dev, _gaze_point_callback, t));
	check(tobii_update_timesync(t->dev));  // TODO: more often?
	t->started = 1;
	tobii_error_t error;
	do {
		error = tobii_wait_for_callbacks(1, &t->dev);
		if (error == TOBII_ERROR_TIMED_OUT) {
			t->timestamp_us = 0;
			t->initialized = 0;
		}
	} while (error == TOBII_ERROR_TIMED_OUT && !global_terminating);
	while (!t->initialized && !global_terminating) {
		check(tobii_device_process_callbacks(t->dev));
	}
	if (!global_terminating)
		t->initialized = 1;
	assert(!pthread_mutex_unlock(&t->mutex));
}

void tracker_timesync(struct tracker* t) {
	assert(!pthread_mutex_lock(&t->mutex));
	check(tobii_update_timesync(t->dev));
	assert(!pthread_mutex_unlock(&t->mutex));
}

void tracker_read(struct tracker* t,
		uint64_t* timestamp_us, bool* valid, float* x, float* y) {
	assert(!global_terminating);
	assert(t->initialized);
	assert(t->started);
	assert(!pthread_mutex_lock(&t->mutex));
	while (!t->new_data && !global_terminating) {
		check(tobii_device_process_callbacks(t->dev));
	}
	t->new_data = 0;
	assert(t->timestamp_us);  // dirty!
	if (t->valid) {
		if (!t->first_timestamp_us) {
			t->first_timestamp_us = t->timestamp_us;
		}
		else {
			t->first_timestamp_us = 0;
		}
	}
	*timestamp_us = t->timestamp_us;
	*valid = t->valid;
	*x = t->x;
	*y = t->y;
	assert(!pthread_mutex_unlock(&t->mutex));
}

void tracker_stop(struct tracker* t) {
	assert(!pthread_mutex_lock(&t->mutex));
	assert(t->started);
	if (t->started)
		check(tobii_gaze_point_unsubscribe(t->dev));
	t->initialized = 0;
	t->valid = 0;
	t->first_timestamp_us = 0;
	t->timestamp_us = 0;
	t->x = t->y = NAN;
	t->started = 0;
	assert(!pthread_mutex_unlock(&t->mutex));
}

void tracker_deinit(struct tracker* t) {
	assert(!pthread_mutex_lock(&t->mutex));
	assert(!t->started);
	check(tobii_device_destroy(t->dev));
	tobii_context_deinit();
	assert(!pthread_mutex_unlock(&t->mutex));
}


/* main code */


struct tracker* t_main;
struct tracker* t_scnd;


void* thread_main_func(void* _) {
	float x, y;
	uint64_t timestamp_us;
	bool valid;

	tracker_start(t_main);
	while (!global_terminating) {
		tracker_read(t_main, &timestamp_us, &valid, &x, &y);
		gaze_report(t_main->uri, x, y);
	}
	global_terminating = 1;
	tracker_stop(t_main);
	tracker_deinit(t_main);
	pthread_exit(NULL);
}

void* thread_scnd_func(void* _) {
	float x, y;
	uint64_t timestamp_us;
	uint64_t ts_main, ts_scnd;
	int64_t diff;
	bool valid;
	int mismatches = 0;

	tracker_start(t_scnd);
	while (!global_terminating) {
		tracker_read(t_scnd, &timestamp_us, &valid, &x, &y);
		gaze_report(t_scnd->uri, x, y);
		/*
		ts_main = t_main->first_timestamp_us;
		ts_scnd = t_scnd->first_timestamp_us;
		diff = ts_scnd - ts_main;
		if (ts_main || ts_scnd) {
			printf("%+15ld %10lu %10lu %d\n", diff, ts_scnd, ts_main, mismatches);
			if (diff > -5000 && diff < 15000) {
				gaze_report(t_scnd->uri, x, y);
				mismatches -= 1;
				if (mismatches < 0)
					mismatches = 0;
			} else {
				gaze_report(t_scnd->uri, NAN, NAN);
				mismatches += 3;
			}
			if (mismatches > 500) {
				printf("too much\n");
				tracker_stop(t_scnd);
				usleep((diff * 3 + diff / 2) % 10000ul);
				tracker_timesync(t_scnd);
				tracker_start(t_scnd);
				printf("restart\n");
				mismatches = 0;
			}
		}
		*/
	}
	global_terminating = 1;
	tracker_stop(t_scnd);
	tracker_deinit(t_scnd);
	pthread_exit(NULL);
}


int main(int argc, char** argv) {
	pthread_t thread_main, thread_scnd;

	if (argc != 5) {
		fprintf(stderr,
			"Usage: %s host port tobii-uri-main tobii-uri-secondary\n",
			argv[0]);
		exit(1);
	}
	char* host = argv[1];
	char* port = argv[2];
	t_main = tracker_init(argv[3]);
	t_scnd = tracker_init(argv[4]);

	global_sock_init(host, port);
	terminating_configure_signals();

	pthread_create(&thread_main, NULL, thread_main_func, NULL);
	pthread_create(&thread_scnd, NULL, thread_scnd_func, NULL);

	fprintf(stderr, "joining...\n");
	pthread_join(thread_main, NULL);
	pthread_join(thread_scnd, NULL);
	fprintf(stderr, "exiting...\n");
	return 0;
}
