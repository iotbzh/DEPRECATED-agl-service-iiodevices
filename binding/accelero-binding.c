#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <glib.h>

#include <json-c/json.h>
#define AFB_BINDING_VERSION 2
#include <afb/afb-binding.h>

#define IIODEVICE "/sys/bus/iio/devices/iio:device"
#define IIODEVICE0 "/sys/bus/iio/devices/iio:device0/"
#define IIODEVICE1 "/sys/bus/iio/devices/iio:device1/"

static struct afb_event accel_event;
static struct afb_event magn_event;
static struct afb_event anglvel_event;

static pthread_t thread;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void get_buffer(const char *iio_file, const char *sub_name,
        void *buffer, size_t count)
{
    int fd;
    fd = open(iio_file, O_RDONLY);
    if(fd < 0) {
		AFB_ERROR("Cannot open %s\n", iio_file);
        return;
    }
    if(read(fd, buffer, count) < 0){
        AFB_ERROR("cannot read %s\n", iio_file);
        return;
    }
    close(fd);
}

static void get_subdata_string(const char * iio_device_path, const char *sub_name,
        json_object *jresp) {
    char iio_file[100];
    sprintf(iio_file, "%s%s", iio_device_path, sub_name);
    size_t buffer_size = 1000;
    char buffer[buffer_size];

    get_buffer(iio_file, sub_name, buffer, buffer_size);

    json_object *value = json_object_new_string(buffer);
    if(jresp)
        json_object_object_add(jresp, sub_name, value);
}

static void get_subdata_int(const char * iio_device_path, const char *sub_name,
        json_object *jresp) {
    char iio_file[100];
    sprintf(iio_file, "%s%s", iio_device_path, sub_name);
    AFB_INFO("iio_file=%s", iio_file);
    size_t buffer_size = 1000;
    char buffer[buffer_size];

    get_buffer(iio_file, sub_name, buffer, buffer_size);

    json_object *value = json_object_new_int(atoi(buffer));
    if(jresp)
        json_object_object_add(jresp, sub_name, value);
}

static void get_data(const char *name, const int iio_device, json_object *jresp) {
    char iio_device_path[100];
    sprintf(iio_device_path, IIODEVICE"%d/in_%s_", iio_device, name);

    get_subdata_string(iio_device_path, "scale", jresp);
    get_subdata_string(iio_device_path, "scale_available", jresp);
    get_subdata_int(iio_device_path, "x_raw", jresp);
    get_subdata_int(iio_device_path, "y_raw", jresp);
    get_subdata_int(iio_device_path, "z_raw", jresp);
}

static int treat_event(const char *name, const int device_number,
        struct afb_event event)
{
	json_object *jresp = json_object_new_object();
    get_data(name, device_number, jresp);
    AFB_INFO("push event %s\n", name);
	pthread_mutex_lock(&mutex);
    afb_event_push(event, json_object_get(jresp));
	pthread_mutex_unlock(&mutex);
    return 0;
}

gboolean data_poll(gpointer ptr)
{
    AFB_INFO("data_poll");
    treat_event("accel", 0, accel_event);
    treat_event("magn", 0, magn_event);
    treat_event("anglvel", 1, anglvel_event);
    return TRUE;
}

static void *data_thread(void *ptr)
{
    g_timeout_add_seconds(1, data_poll, NULL);
	g_main_loop_run(g_main_loop_new(NULL, FALSE));
    return NULL;
}

static void subscribe(struct afb_req request)
{
	const char *value = afb_req_value(request, "event");
	if(value) {
		if(!strcasecmp(value, "accel")) {
			afb_req_subscribe(request, accel_event);
		} else if(!strcasecmp(value, "magn")) {
			afb_req_subscribe(request, magn_event);
		} else if(!strcasecmp(value, "anglvel")) {
			afb_req_subscribe(request, anglvel_event);
		} else {
			afb_req_fail(request, "failed", "Invalid event");
			return;
		}
	}
	afb_req_success(request, NULL, NULL);
}

static void unsubscribe(struct afb_req request)
{
	const char *value = afb_req_value(request, "value");
	if(value) {
		if(!strcasecmp(value, "accel")) {
			afb_req_unsubscribe(request, accel_event);
		} else if(!strcasecmp(value, "magn")) {
			afb_req_unsubscribe(request, magn_event);
		} else {
			afb_req_fail(request, "failed", "Invalid event");
			return;
		}
	}
	afb_req_success(request, NULL, NULL);
}

const afb_verb_v2 verbs[] = {
	    { .verb = "subscribe",		.session = AFB_SESSION_NONE, .callback = subscribe,		.info = "Subscribe for an event" },
	    { .verb = "unsubscribe",	.session = AFB_SESSION_NONE, .callback = unsubscribe,		.info = "Unsubscribe for an event" },
        { .verb=NULL }
};

static int init()
{
	accel_event = afb_daemon_make_event("accel");
	magn_event = afb_daemon_make_event("magn");
	anglvel_event = afb_daemon_make_event("anglvel");

    pthread_create(&thread, NULL, &data_thread, NULL);

	return 0;
}

const afb_binding_v2 afbBindingV2 = {
        .info = "accelero service",
        .api = "accelero",
        .verbs = verbs,
        .init = init,
};
