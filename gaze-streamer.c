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

void gaze_report(uint64_t timestamp_us, float x, float y) {
	int l;
	char message[512];
	l = snprintf(message, 512, "%s %ld gaze: %+.020f %+.020f\n",
		     url, timestamp_us, x, y);
	//fputs(message, stdout);
	send(sock, message, strlen(message), 0);
}

void gaze_point_callback(tobii_gaze_point_t const *gaze_pt, void *_) {
	if (gaze_pt->validity == TOBII_VALIDITY_VALID)
		gaze_report(gaze_pt->timestamp_us,
			    gaze_pt->position_xy[0], gaze_pt->position_xy[1]);
	else
		gaze_report(gaze_pt->timestamp_us, NAN, NAN);
}

void head_pos_report(uint64_t timestamp_us, float x, float y, float z) {
	int l;
	char message[512];
	l = snprintf(message, 512, "%s %5ld head_pos: %+.020f %+.020f %+.020f\n",
		     url, timestamp_us, x, y, z);
	//fputs(message, stdout);
	send(sock, message, strlen(message), 0);
}

void head_pos_callback(tobii_head_pose_t const* head_pos, void *_) {
	if (head_pos->position_validity == TOBII_VALIDITY_VALID)
		head_pos_report(head_pos->timestamp_us,
			       head_pos->position_xyz[0],
			       head_pos->position_xyz[1],
			       head_pos->position_xyz[2]);
	else
		head_pos_report(head_pos->timestamp_us, NAN, NAN, NAN);
	// TODO: head rotation
}

void eye_pos_report(uint64_t timestamp_us, bool right,
		    float x, float y, float z) {
	int l;
	char message[512];
	l = snprintf(message, 512, "%s %5ld %s_eye: %+.020f %+.020f %+.020f\n",
		     url, timestamp_us, right ? "right" : "left", x, y, z);
	//fputs(message, stdout);
	send(sock, message, strlen(message), 0);
}

void eye_pos_callback(tobii_eye_position_normalized_t const* eye_pos, void *_) {
	if (eye_pos->left_validity == TOBII_VALIDITY_VALID)
		eye_pos_report(eye_pos->timestamp_us, 0,  // left
			       eye_pos->left_xyz[0],
			       eye_pos->left_xyz[1],
			       eye_pos->left_xyz[2]);
	else
		eye_pos_report(eye_pos->timestamp_us, 0 /*l*/, NAN, NAN, NAN);

	if (eye_pos->right_validity == TOBII_VALIDITY_VALID)
		eye_pos_report(eye_pos->timestamp_us, 1,  // right
			       eye_pos->right_xyz[0],
			       eye_pos->right_xyz[1],
			       eye_pos->right_xyz[2]);
	else
		eye_pos_report(eye_pos->timestamp_us, 1 /*r*/, NAN, NAN, NAN);
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
	fprintf(stderr, "Starting up %s\n", url);
	check(tobii_device_create(api, url, &device));
	check(tobii_gaze_point_subscribe(device, gaze_point_callback, 0));
	check(tobii_eye_position_normalized_subscribe(device, eye_pos_callback, 0));
	check(tobii_head_pose_subscribe(device, head_pos_callback, 0));

	signal(SIGINT, wrap_up);
	signal(SIGTERM, wrap_up);

	while (!done) {
		do {
			error = tobii_wait_for_callbacks(1, &device);
			if (error == TOBII_ERROR_TIMED_OUT)
				gaze_report(0, NAN, NAN);
		} while (error == TOBII_ERROR_TIMED_OUT);
		check(error);
		check(tobii_device_process_callbacks(device));
	}
	fprintf(stderr, "Exiting...\n");
	gaze_report(0, NAN, NAN);

	check(tobii_head_pose_unsubscribe(device));
	check(tobii_eye_position_normalized_unsubscribe(device));
	check(tobii_gaze_point_unsubscribe(device));
	check(tobii_device_destroy(device));
	check(tobii_api_destroy(api));
	return 0;
}
