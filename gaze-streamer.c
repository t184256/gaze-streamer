#include <arpa/inet.h>
#include <netdb.h>
#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
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

void gaze_point_callback(tobii_gaze_point_t const *gaze_point, void *_) {
	if (gaze_point->validity != TOBII_VALIDITY_VALID)
		return;
	int l;
	char message[128];
	l = snprintf(message, 128, "%s gaze: %+.20f %+.020f\n", url,
		     gaze_point->position_xy[0], gaze_point->position_xy[1]);
	fputs(message, stdout);
	send(sock, message, strlen(message), 0);
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
	while (1) {
		do {
			error = tobii_wait_for_callbacks(1, &device);
		} while (error == TOBII_ERROR_TIMED_OUT);
		check(error);
		check(tobii_device_process_callbacks(device));
	}
}
