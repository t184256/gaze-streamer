#include <arpa/inet.h>
#include <netdb.h>
#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define check(e) assert((e) == TOBII_ERROR_NO_ERROR)

//enum which { BOTTOM, TOP };

char* url_bot;
char* url_top;

int sock;
bool done = 0;
tobii_device_t* device_bot;
tobii_device_t* device_top;
tobii_device_t* device_active;
tobii_device_t* device_off;
tobii_device_t* device_switch_to;
int invalid_counter = 0;
int INVALID_THRESH_TOP = 100;
int INVALID_THRESH_BOT = 200;  // bottom tracks better

void gaze_report(float x, float y) {
	int l;
	char message[128];
	l = snprintf(message, 128, "%s gaze: %+.20f %+.020f\n",
		     (device_active == device_bot) ? url_bot : url_top, x, y);
	fputs(message, stdout);
	send(sock, message, strlen(message), 0);
}

void gaze_point_callback(tobii_gaze_point_t const *gaze_pt, void *_) {
	if (gaze_pt->validity == TOBII_VALIDITY_VALID) {
		if (device_active == device_bot) {
			if (gaze_pt->position_xy[1] < .25) {
				device_switch_to = device_top;
				return;
			}
		} else {
			if (gaze_pt->position_xy[1] < .5) {
				device_switch_to = device_bot;
				return;
			}
		}
	}
	if (gaze_pt->validity != TOBII_VALIDITY_VALID) {
		invalid_counter++;
		if (device_active == device_top && invalid_counter > INVALID_THRESH_TOP) {
			device_switch_to = device_bot;
			invalid_counter = 0;
			gaze_report(NAN, NAN);
			return;
		}
		if (device_active == device_bot && invalid_counter > INVALID_THRESH_BOT) {
			device_switch_to = device_top;
			invalid_counter = 0;
			gaze_report(NAN, NAN);
			return;
		}
	}

	if (gaze_pt->validity == TOBII_VALIDITY_VALID)
		gaze_report(gaze_pt->position_xy[0], gaze_pt->position_xy[1]);
	else
		gaze_report(NAN, NAN);
}

void wrap_up(int signum) {
	done = 1;
}

int main(int argc, char** argv) {
	if (argc != 5) {
		fprintf(stderr,
			"Usage: %s host port tobii-url-bottom tobii-url-top\n",
			argv[0]);
		exit(1);
	}
	char* host = argv[1];
	char* port = argv[2];
	url_bot = argv[3];
	url_top = argv[4];

	struct addrinfo* ai;
	struct addrinfo hints = {.ai_family=AF_UNSPEC, .ai_socktype=SOCK_DGRAM};
	assert(getaddrinfo(host, port, &hints, &ai) == 0);
	sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	assert(sock > 0);
	assert(connect(sock, ai->ai_addr, ai->ai_addrlen) != -1);

	tobii_api_t *api;
	tobii_error_t error;
	check(tobii_api_create(&api, NULL, NULL));
	check(tobii_device_create(api, url_bot, &device_bot));
	check(tobii_device_create(api, url_top, &device_top));
	device_switch_to = device_bot;
	device_active = device_bot;
	device_off = device_top;

	signal(SIGINT, wrap_up);
	signal(SIGTERM, wrap_up);

	check(tobii_gaze_point_subscribe(device_active,
					 gaze_point_callback, 0));

	while (!done) {
		while (!done && device_switch_to == device_active) {
			do {
				error = tobii_wait_for_callbacks(1, &device_active);
				if (error == TOBII_ERROR_TIMED_OUT)
					gaze_report(NAN, NAN);
			} while (error == TOBII_ERROR_TIMED_OUT);
			check(error);
			check(tobii_device_process_callbacks(device_active));
		}
		printf("%d\n", done);
		if (!done && device_switch_to != device_active) {
			gaze_report(NAN, NAN);
			check(tobii_gaze_point_unsubscribe(device_active));
			check(tobii_gaze_point_subscribe(device_switch_to,
							 gaze_point_callback, 0));
			device_off = device_active;
			device_active = device_switch_to;
		}
	}
	fprintf(stderr, "Exiting...\n");
	gaze_report(NAN, NAN);

	check(tobii_gaze_point_unsubscribe(device_active));
	check(tobii_device_destroy(device_bot));
	check(tobii_device_destroy(device_top));
	check(tobii_api_destroy(api));
	return 0;
}
