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

char first_url[128];
void get_first_url(char const *received_url, void *_) {
	strcpy(first_url, received_url);
}

char* url = first_url;
int sock;
bool done = 0;

void gaze_report(float x, float y) {
	int l;
	char message[128];
	l = snprintf(message, 128, "%s gaze: %+.20f %+.020f\n", url, x, y);
	fputs(message, stdout);
	send(sock, message, strlen(message), 0);
}

void gaze_point_callback(tobii_gaze_point_t const *gaze_pt, void *_) {
	if (gaze_pt->validity == TOBII_VALIDITY_VALID)
		gaze_report(gaze_pt->position_xy[0], gaze_pt->position_xy[1]);
	else
		gaze_report(NAN, NAN);
}

void wrap_up(int signum) {
	done = 1;
}

int main(int argc, char** argv) {
	if (argc != 3 && argc != 4) {
		fprintf(stderr, "Usage: %s host port [tobii-url]\n", argv[0]);
		exit(1);
	}

	struct addrinfo* ai;
	struct addrinfo hints = {.ai_family=AF_UNSPEC, .ai_socktype=SOCK_DGRAM};
	assert(getaddrinfo(argv[1], argv[2], &hints, &ai) == 0);
	sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	assert(sock > 0);
	assert(connect(sock, ai->ai_addr, ai->ai_addrlen) != -1);

	tobii_api_t *api;
	tobii_device_t *device;
	tobii_error_t error;
	check(tobii_api_create(&api, NULL, NULL));
	if (argc == 4)
		url = argv[3];
	else
		check(tobii_enumerate_local_device_urls(api, get_first_url, 0));
	check(tobii_device_create(api, url, &device));
	check(tobii_gaze_point_subscribe(device, gaze_point_callback, 0));

	signal(SIGINT, wrap_up);
	signal(SIGTERM, wrap_up);

	while (!done) {
		do {
			error = tobii_wait_for_callbacks(1, &device);
			if (error == TOBII_ERROR_TIMED_OUT)
				gaze_report(NAN, NAN);
		} while (error == TOBII_ERROR_TIMED_OUT);
		check(error);
		check(tobii_device_process_callbacks(device));
	}
	fprintf(stderr, "Exiting...\n");
	gaze_report(NAN, NAN);

	check(tobii_gaze_point_unsubscribe(device));
	check(tobii_device_destroy(device));
	check(tobii_api_destroy(api));
	return 0;
}
