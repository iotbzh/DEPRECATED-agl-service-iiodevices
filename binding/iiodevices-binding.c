#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <systemd/sd-event.h>
#include <time.h>

#include <json-c/json.h>
#define AFB_BINDING_VERSION 2
#include <afb/afb-binding.h>

#include <iio.h>

#include "config_iiodevices.h"

/*structure in order to handle channels connections*/
struct channels {
    struct iio_channel *chn;
    enum iio_elements iioelts;
    char name[100];
    struct channels *next;
};

/*handle client subscription*/
struct client_sub {
    struct channels *channels;
    int index;
    int fd;
    uint64_t u_period;
    enum iio_elements iioelts;
    struct iio_device *dev;
    struct afb_event *event;
    struct iio_info *infos;
    json_object *jobject;
    struct client_sub *next;
};

/*gather all afb_event*/
struct event
{
    struct event *next;
    struct afb_event event;
    char tag[100];
};

/*events*/
static struct event *events = 0;
/*iio context*/
static struct iio_context *ctx = NULL;
/*clients*/
static struct client_sub * clients = NULL;
/*save last registered client*/
static struct client_sub * last_client = NULL;

/*get event by afb_event*/
static struct event *event_get_event(const struct afb_event *event)
{
    struct event *e = events;
    while(&e->event != event) {
        e = e->next;
    }
    return e;
}

/* searchs the event of tag */
static struct event *event_get(const char *tag)
{
	struct event *e = events;
	while(e && strcmp(e->tag, tag))
		e = e->next;
	return e;
}

/* creates the event of tag */
static struct afb_event* event_add(const char *tag)
{
	struct event *e;

	/* check valid tag */
	e = event_get(tag);
	if (e) return NULL;

	/* creation */
	e = malloc(strlen(tag) + sizeof *e);
	if (!e) return NULL;
	strcpy(e->tag, tag);

	/* make the event */
	e->event = afb_daemon_make_event(tag);
	if (!e->event.closure) { free(e); return NULL; }

	/* link */
	e->next = events;
	events = e;
	return &e->event;
}

/* deletes the event of event */
static int event_del(struct afb_event *event)
{
	struct event *e, **p;

	/* check exists */
    e = event_get_event(event);
    if (!e) return -1;

    /* unlink */
    p = &events;
    while(*p != e) p = &(*p)->next;
    *p = e->next;

    /* destroys */
    afb_event_unref(e->event);
    free(e);
	return 0;
}

/*initialse iio context*/
int init_context()
{
    ctx = iio_create_local_context();
    if(!ctx)
    {
        AFB_ERROR("cannot create local iio context");
        return -1;
    }
    return 0;
}

/*initialise device: find by its name*/
static void init_dev(struct client_sub *client)
{
    if(!client) {
        AFB_ERROR("client is null");
        return NULL;
    }

    if(!ctx)
        init_context();
    AFB_DEBUG("iio_context_find_device %s", client->infos->dev_name);
    client->dev =  iio_context_find_device(ctx, client->infos->dev_name);
    if(!client->dev) {
        AFB_ERROR("No %s device found", client->infos->dev_name);
        return NULL;
    }
}

/*read static infos from channels*/
static int read_infos(struct client_sub* client)
{
    if(!client || !client->channels || !client->channels->chn) {
        AFB_ERROR("client=%p or client->channels=%p or client->channels->chn=%p is null", client, client->channels, client->channels->chn);
        return -1;
    }
    struct iio_channel *chn = client->channels->chn;
    json_object *jobject = client->jobject;
    char val[100];

    //read all infos
    for(int i = 0; i < iio_channel_get_attrs_count(chn); i++) {
        const char *id_attr = iio_channel_get_attr(chn, i);
		if(strcasecmp(id_attr, "raw")) { //do not take raw infos
            iio_channel_attr_read(chn, id_attr, val, 100);
            json_object *value = json_object_new_string(val);
            json_object_object_add(jobject, id_attr, value);
        }
    }
    return 0;
}

/*close file descriptor*/
static void close_fd(struct client_sub *client)
{
    if(!client) {
        AFB_ERROR("client is null pointer");
        return;
    }

    if(client->fd >= 0)
        close(client->fd);
}

/*deinit client channels*/
static void deinit_channels(struct client_sub *client)
{
    if(!client->channels) {
        AFB_ERROR("client->channels is null pointer");
        return;
    }

    struct channels *it_chn = client->channels;
    while(it_chn) {
        struct channels *tmp = it_chn->next;
        AFB_DEBUG("free it_chn=%s", it_chn->name);
        free(it_chn);
        it_chn = tmp;
    }
    client->channels = NULL;
}

/*looking for previous client: if client is first returns client*/
static struct client_sub* looking_for_previous(struct client_sub *client)
{
    if(!client || !clients) {
        AFB_ERROR("client or clients is null pointer");
        return;
    }
    struct client_sub* prev = clients;
    while(prev->next != client) {
        prev = prev->next;
    }
    return prev;
}

/*deinit client sub*/
static void deinit_client_sub(struct client_sub *client)
{
    if(!client) {
        AFB_ERROR("client is null pointer");
        return;
    }

    struct client_sub *prev_client;
    if(client == clients) {//first one
        clients = client->next;
    }
    else {
        prev_client = looking_for_previous(client);
        if(!prev_client) {
            AFB_ERROR("cannot find previous client");
            exit(-1);
        }
        prev_client->next = client->next;
    }
    AFB_DEBUG("free client for %s", client->infos->id);
    free(client);
}

/*desallocate channels*/
static void desallocate_channels(sd_event_source* src,
        struct client_sub *client)
{
    afb_event_drop(*client->event);
    if(src) {
        sd_event_source_set_enabled(src, SD_EVENT_OFF);
        sd_event_source_unref(src);
    }
    close_fd(client);
    event_del(client->event);
    deinit_channels(client);
    deinit_client_sub(client);
}

/*!read data from channels*/
static int read_data(struct client_sub *client, sd_event_source* src)
{
    if(!client) {
        AFB_ERROR("client is null");
        return -1;
    }

    char val[10];
    struct channels *channel = client->channels;
    while(channel) { //iterate on client channels
        if(!channel->chn) {
            AFB_ERROR("chn is null for cl_chn=%s", channel->name);
            return -1;
        }

        iio_channel_attr_read(channel->chn, "raw", val, 10);
        int data = (int)strtol(val, NULL, 10);
        AFB_DEBUG("read_data: %s %d", iio_channel_get_id(channel->chn), data);

        json_object *value = json_object_new_int(data);
        json_object_object_add(client->jobject, channel->name, value);
        channel = channel->next;
    }

    /*if no more subscribers -> desallocate*/
    int nb_subscribers = afb_event_push(*client->event, json_object_get(client->jobject));
    AFB_DEBUG("nb_subscribers for %s is %d", afb_event_name(*client->event), nb_subscribers);
    if(nb_subscribers <= 0)
        desallocate_channels(src, client);
    return 0;
}

/*read data from file descriptor*/
static int read_data_push(sd_event_source* src, int fd, uint32_t revents, void* userdata)
{
    struct client_sub * client= (struct client_sub *)userdata;
    read_data(client, src);
    return 0;
}

/*read data from timer*/
static int read_data_timer(sd_event_source* src, uint64_t usec, void *userdata)
{
    struct client_sub *client = (struct client_sub *)userdata;

    /*set next time to trigger*/
    uint64_t usecs;
    sd_event_now(afb_daemon_get_event_loop(), CLOCK_MONOTONIC, &usecs);
    usecs += client->u_period;
    sd_event_source_set_time(src, usecs);

    read_data(client, src);
    return 0;
}

/*init event io: depending on the frequency, if no filled, triggered on file
 * descriptor */
static void init_event_io(struct client_sub *client)
{
    if(!client) {
        AFB_ERROR("client is null");
        return;
    }
    read_infos(client); //get unchanged infos
    sd_event_source *source = NULL;
    if(client->u_period <= 0) { //no given frequency
        char filename[100];
        sprintf(filename, IIODEVICE"%s/%s", iio_device_get_id(client->dev),
                iio_channel_attr_get_filename(client->channels->chn, "raw"));
        if((client->fd = open(filename, O_RDONLY)) < 0) {
            AFB_ERROR("cannot open %s file", filename);
            return;
        }

        sd_event_add_io(afb_daemon_get_event_loop(), &source, client->fd, EPOLLIN, read_data_push, client);
    }
    else { //frequency specified
        sd_event_add_time(afb_daemon_get_event_loop(), &source, CLOCK_MONOTONIC, 0, 1, read_data_timer, client);
        sd_event_source_set_enabled(source, SD_EVENT_ON);
    }
}

/*initialise client channel*/
static struct channels* set_channel(
        struct client_sub* client, struct channels *prev_chn, const enum iio_elements i)
{
    prev_chn->next = malloc(sizeof(struct channels));
    if(!prev_chn->next) {
        AFB_ERROR("alloc failed for channels");
        return;
    }

    struct channels *chn = prev_chn->next;
    chn->next = NULL;
    chn->iioelts = i;

    /*set channel name with iio_elements*/
    strcpy(chn->name, client->infos->id);
    set_channel_name(chn->name, i);

    if(!(chn->chn = iio_device_find_channel(client->dev, chn->name, false))) {
        AFB_ERROR("cannot find %s channel", chn->name);
        return NULL;
    }
    iio_channel_enable(chn->chn);
    return chn;
}

static void init_channel(struct client_sub* client, const unsigned int iioelts)
{
    if(!client) {
        AFB_ERROR("client is null");
        return;
    }
    char event_name[100];
    const char *name = client->infos->id;
    AFB_DEBUG("size of channels=%d", get_iio_nb(iioelts));

    /*looking for the last channel: could be the first*/
    struct channels pre_index;
    pre_index.next = client->channels;
    struct channels *it_chn = &pre_index;
    while(it_chn->next) {
        it_chn = it_chn->next;
    }

    /*set channel*/
    for(int i = 1; i <= iioelts; i = i << 1) { //iterate on enumerations
        if(i == (i & iioelts)) {
            it_chn = set_channel(client, it_chn, i);
        }
    }
    client->channels = pre_index.next;
}

/*get period in microseconds from a frequency*/
static uint64_t get_period(const char* freq)
{
    double frequency = 0;
    if(freq)
        frequency = strtod(freq, NULL);
    return (frequency == 0) ? 0 : (uint64_t)((1.0 / frequency) * 1000000);
}

/*check if it is needed to create a new client: if NULL return it is needed,
 * else it is no needed but can be needed to add channels if rest_iioelts > 0*/
static struct client_sub* is_new_client_sub_needed(struct iio_info* infos,
        uint64_t u_period, enum iio_elements *rest_iioelts)
{
    struct client_sub *it_client = clients;
    while(it_client) {
            if(it_client->infos == infos && u_period == it_client->u_period) {
                AFB_DEBUG("a client is matching");
                *rest_iioelts = ~it_client->iioelts & *rest_iioelts;
                it_client->iioelts |= *rest_iioelts;
                return it_client;
            }
            it_client = it_client->next;
    }
    return NULL;
}

/*add new client with new event*/
static struct client_sub *add_new_client(const struct iio_info *infos,
        const enum iio_elements iioelts, const uint64_t u_period)
{
    struct client_sub *client = malloc(sizeof(struct client_sub));
    if(!client) {
        AFB_ERROR("cannot allocate client");
        return NULL;
    }
    //initialise client
    if(!clients) {
        clients = client;
        client->index = 0;
    }
    else {
        if(!last_client) {
            AFB_ERROR("last_client should not be null");
            return NULL;
        }
        last_client->next = client;
        client->index = last_client->index + 1;
    }

    char event_name[100];
    sprintf(event_name, "%s%d", infos->id, client->index);
    AFB_DEBUG("add_new_client with event_name=%s", event_name);

    client->channels = NULL;
    client->jobject = json_object_new_object();
    client->iioelts = iioelts;
    client->u_period = u_period;
    client->infos = infos;
    client->fd = -1;
    client->next = NULL;
    client->dev = NULL;
    client->event = event_add(event_name);

    last_client = client;
    return client;
}

/*subscribe verb*/
static void subscribe(struct afb_req request)
{
    const char *value = afb_req_value(request, "event");
    const char *s_iioelts = afb_req_value(request, "args");
    const char *freq = afb_req_value(request, "frequency");

    if(!value || !s_iioelts) {
        afb_req_fail(request, "failed", "please, fill 'event' and 'args' fields");
        return;
    }

    AFB_INFO("subscription with: value=%s, s_iioelts=%s, freq=%s",
            value, s_iioelts, freq);

    bool found = false;
    for(int i = 0; i < sizeof(iio_infos)/sizeof(iio_infos[0]); i++) {
        if(!strcasecmp(value, iio_infos[i].id)) {
            found = true;

            uint64_t u_period = get_period(freq);
            enum iio_elements iioelts = (int)treat_iio_elts(s_iioelts);
            struct client_sub* client = is_new_client_sub_needed(&iio_infos[i],
                    u_period, &iioelts);
            if(!client || iioelts > 0) { //no client found or new channels
                if(!client)
                    client = add_new_client(&iio_infos[i], iioelts, u_period);
                init_dev(client);
                init_channel(client, iioelts);
                init_event_io(client);
            }
            //stored client in order to be get in unsubscription
            afb_req_context_set(request, client, NULL);

            if(afb_req_subscribe(request, *client->event) != 0) {
                afb_req_fail(request, "failed", "subscription failed");
                return;
            }
        }
    }
    if(!found) {
        afb_req_fail(request, "failed", "Invalid event");
        return;
    }
    afb_req_success(request, NULL, NULL);
}

/*unsubscribe verb*/
static void unsubscribe(struct afb_req request)
{
    const char *value = afb_req_value(request, "event");
    if(!value) {
        afb_req_fail(request, "failed", "please, fill 'event' fields");
        return;
    }

    bool found = false;
    if(value) {
        for(int i = 0; i < sizeof(iio_infos)/sizeof(iio_infos[0]); i++) {
            if(!strcasecmp(value, iio_infos[i].id)) {
                found = true;
                struct client_sub *client
                    = (struct client_sub *)afb_req_context_get(request);
                if(!client) {
                    AFB_ERROR("cannot find %s event, it seems that there was \
                            no subscription", value);
                    return;
                }
                if(afb_req_unsubscribe(request, *client->event) != 0) {
                    afb_req_fail(request, "failed", "unsubscription failed");
                    return;
                }
            }
        }
        if(!found) {
            afb_req_fail(request, "failed", "Invalid event");
            return;
        }
    }
    afb_req_success(request, NULL, NULL);
}

/*list of api verbs*/
const afb_verb_v2 verbs[] = {
	    { .verb = "subscribe",		.session = AFB_SESSION_NONE, .callback = subscribe,		.info = "Subscribe for an event" },
	    { .verb = "unsubscribe",	.session = AFB_SESSION_NONE, .callback = unsubscribe,		.info = "Unsubscribe for an event" },
        { .verb=NULL }
};

/*binding configuration*/
const afb_binding_v2 afbBindingV2 = {
        .info = "iio devices service",
        .api = "iiodevices",
        .verbs = verbs
};
