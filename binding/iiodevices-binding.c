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

/*structure in order to handle client_channels connections*/
struct client_channels {
    struct iio_channel *chn;
    int index;
    char uid[100];
    int fd;
    uint64_t u_period;
    enum iio_elements iioelts;
    char name[100];
    struct iio_device *dev;
    struct afb_event *event;
    struct iio_info *infos;
    json_object *jobject;
    struct client_channels *first;
    struct client_channels *next;
};

struct s_channels {
    struct client_channels *channels;
    unsigned int nb;
};

/*gather all afb_event*/
struct event
{
    struct event *next;
    struct afb_event event;
    unsigned int nb_subscribers;
    char tag[100];
};

static struct event *events = 0;
static struct iio_context *ctx = NULL;
static struct s_channels channels = { NULL, 0 };

static struct event *event_get_event(const struct afb_event *event)
{
    struct event *e = events;
    while(&e->event != event) {
        e = e->next;
    }
    return e;
}

static void event_add_subscriber(struct afb_event *event)
{
    struct event *e;
    e = event_get_event(event);
    if(e)
        e->nb_subscribers++;
    else
        AFB_WARNING("event not found");
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
    e->nb_subscribers++;
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

    e->nb_subscribers--;
    if(e->nb_subscribers > 0)
        return 0;
    else {
	    /* unlink */
	    p = &events;
	    while(*p != e) p = &(*p)->next;
	    *p = e->next;

	    /* destroys */
	    afb_event_unref(e->event);
	    free(e);
    }
	return 0;
}

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

static struct iio_device *init_dev(const char *dev_name)
{
    if(!ctx)
        init_context();
    struct iio_device *dev =  iio_context_find_device(ctx, dev_name);
    if(!dev) {
        AFB_ERROR("No %s device found", dev_name);
        return NULL;
    }
    return dev;
}

static int read_infos(struct client_channels *cl_chn)
{
    if(!cl_chn) {
        AFB_ERROR("cl_chn is null");
        return -1;
    }
    struct iio_channel *chn = cl_chn->chn;
    json_object *jobject = cl_chn->jobject;
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

static void deinit_channels(struct client_channels *cl_chn)
{
    if(!cl_chn || !channels.channels || channels.nb == 0) {
        AFB_ERROR("null pointer");
        return;
    }

    int i = 0;
    while(&channels.channels[i] != cl_chn->first) //looking for index matching cl_chn
        i++;

    int offset = 0;
    while(channels.channels[i + offset].next) { //looking for number of channels a client
        iio_channel_disable(channels.channels[i + offset].chn);
        offset++;
    }

    while(i < channels.nb - offset) { //move above all remained channels
        channels.channels[i] = channels.channels[i + offset + 1];
        i++;
    }

    channels.nb -= offset + 1;
    channels.channels = realloc(channels.channels, channels.nb * sizeof(struct client_channels));

    if(channels.nb == 0)
        channels.channels = NULL;
    AFB_DEBUG("deinit channel: size=%d", channels.nb);
}

static void close_fd(struct client_channels *cl_chn)
{
    if(!cl_chn) {
        AFB_ERROR("null pointer");
        return;
    }

    close(cl_chn->fd);
}

static void desallocate_channels(sd_event_source* src,
        struct client_channels *cl_chn)
{
    afb_event_drop(*cl_chn->event);
    if(src) {
        sd_event_source_set_enabled(src, SD_EVENT_OFF);
        sd_event_source_unref(src);
    }
    close_fd(cl_chn);
    event_del(cl_chn->event);
    deinit_channels(cl_chn);
}

static int read_data(struct client_channels *cl_chn, sd_event_source* src)
{
    if(!cl_chn) {
        AFB_ERROR("cl_chn is null");
        return -1;
    }
    struct iio_channel *chn = NULL;
    json_object *jobject = cl_chn->jobject;
    char val[10];

    struct client_channels *p_cl_chn = cl_chn->first;
    AFB_DEBUG("TOTOTO %p", p_cl_chn);
    while(p_cl_chn) {
        chn = p_cl_chn->chn;
        if(!chn) {
            AFB_ERROR("chn is null for cl_chn=%s", p_cl_chn->name);
            return -1;
        }
        iio_channel_attr_read(chn, "raw", val, 10);
        int data = (int)strtol(val, NULL, 10);
        AFB_DEBUG("read_data: %s %d", iio_channel_get_id(chn), data);
        json_object *value = json_object_new_int(data);
        json_object_object_add(jobject, p_cl_chn->name, value);
        p_cl_chn = p_cl_chn->next;
    }

    /*if no more subscribers -> desallocate*/
    if(afb_event_push(*cl_chn->event, json_object_get(cl_chn->jobject)) <= 0)
        desallocate_channels(src, cl_chn);
    return 0;
}

static int read_data_push(sd_event_source* src, int fd, uint32_t revents, void* userdata)
{
    struct client_channels *cl_chn = (struct client_channels *)userdata;
    read_data(cl_chn, src);
    return 0;
}

static int read_data_timer(sd_event_source* src, uint64_t usec, void *userdata)
{
    struct client_channels *cl_chn = (struct client_channels *)userdata;

    /*set next time to trigger*/
    uint64_t usecs;
    sd_event_now(afb_daemon_get_event_loop(), CLOCK_MONOTONIC, &usecs);
    usecs += cl_chn->u_period;
    sd_event_source_set_time(src, usecs);

    read_data(cl_chn, src);
    return 0;
}

static void init_event_io(struct iio_device *dev, struct client_channels *cl_chn)
{
    if(!dev || !cl_chn) {
        AFB_ERROR("dev or cl_chn is null");
        return;
    }
    read_infos(cl_chn); //get unchanged infos
    sd_event_source *source = NULL;
    if(cl_chn->u_period <= 0) { //no given frequency
        char filename[100];
        sprintf(filename, IIODEVICE"%s/%s", iio_device_get_id(dev),
                iio_channel_attr_get_filename(cl_chn->chn, "raw"));
        if((cl_chn->fd = open(filename, O_RDONLY)) < 0) {
            AFB_ERROR("cannot open %s file", filename);
            return;
        }

        sd_event_add_io(afb_daemon_get_event_loop(), &source, cl_chn->fd, EPOLLIN, read_data_push, cl_chn);
    }
    else {
        sd_event_add_time(afb_daemon_get_event_loop(), &source, CLOCK_MONOTONIC, 0, 1, read_data_timer, cl_chn);
        sd_event_source_set_enabled(source, SD_EVENT_ON);
    }
}

/*initialise channel_fd*/
static struct client_channels *set_channel(const int index,
        enum iio_elements i, struct client_channels *first)
{
    int g_index = channels.nb + index;
    char *channel_name = channels.channels[g_index].name;

    /*initialise structure*/
    channels.channels[g_index].infos = first->infos;
    channels.channels[g_index].next = NULL;
    channels.channels[g_index].first = first;
    channels.channels[g_index].fd = -1;
    channels.channels[g_index].iioelts = i;
    channels.channels[g_index].index = first->index;
    channels.channels[g_index].u_period = first->u_period;
    channels.channels[g_index].jobject = first->jobject;
    channels.channels[g_index].dev = first->dev;

    /*set channel name with iio_elements*/
    strcpy(channel_name, first->infos->id);
    set_channel_name(channel_name, i);

    if(!(channels.channels[g_index].chn = iio_device_find_channel(first->dev, channel_name, false))) {
        AFB_ERROR("cannot find %s channel", channel_name);
        return NULL;
    }
    if(g_index > channels.nb) {
        channels.channels[g_index].event = channels.channels[channels.nb].event;
        channels.channels[g_index - 1].next = &channels.channels[g_index];
        strcpy(channels.channels[g_index].uid, first->uid);
    }
    iio_channel_enable(channels.channels[g_index].chn);
    return &channels.channels[g_index];
}

static enum iio_elements get_all_iioets(struct client_channels *cl_chn)
{
    if(!cl_chn) {
        AFB_ERROR("cl_chn is null");
        return 0;
    }
    enum iio_elements iioelts = 0;
    struct client_channels *it_cl_chn = cl_chn->first;
    while(it_cl_chn) {
        iioelts |= it_cl_chn->iioelts;
        it_cl_chn = it_cl_chn->next;
    }
    return iioelts;
}

static struct afb_event* is_allocation_needed(struct iio_info *infos,
        enum iio_elements *iioelts, struct client_channels **first)
{
    struct client_channels *cl_chn = channels.channels;
    //check if already allocated
    while(cl_chn) {
        if(cl_chn->infos == infos) {
            *iioelts = ~get_all_iioets(cl_chn) & *iioelts;
            if(*iioelts == 0) { //iio elts already treated
                AFB_DEBUG("iioelts is already treated");
                return cl_chn->event;
            }
            else {
                *first = cl_chn->first;
            }
            break;
        }
        cl_chn += sizeof(struct client_channels);
    }
    return NULL;
}

static void init_channel(struct iio_device *dev,
        struct iio_info *infos, const int iioelts, struct client_channels **first,
        const uint64_t u_period, const char* uid)
{
    if(!dev || !infos) {
        AFB_ERROR("dev=%p or infos=%p is null", dev, infos);
        return;
    }
    char event_name[100];
    const char *name = infos->id;
    int nb_iioelts = get_iio_nb(iioelts);

    AFB_INFO("size of channels=%d", nb_iioelts);
    channels.channels = realloc(channels.channels, (channels.nb + nb_iioelts) * sizeof(struct client_channels));
    if(!channels.channels) {
        AFB_ERROR("alloc failed for channels");
        return;
    }

    if(!*first) { //first channel for this client
        int index_cl_chn
            = channels.nb == 0 ? 0 : channels.channels[channels.nb - 1].index + 1;
        sprintf(event_name, "%s%d", name, index_cl_chn);
        channels.channels[channels.nb].index = index_cl_chn;
        channels.channels[channels.nb].event = event_add(event_name);
        channels.channels[channels.nb].jobject = json_object_new_object();
        channels.channels[channels.nb].u_period = u_period;
        channels.channels[channels.nb].dev = dev;
        channels.channels[channels.nb].infos = infos;
        strcpy(channels.channels[channels.nb].uid, uid);
        *first = &channels.channels[channels.nb];
    }
    else
        event_add_subscriber((*first)->event);
    channels.channels[channels.nb].first = *first;

    int index = 0;
    for(int i = 1; i <= iioelts; i = i << 1) { //iterate on enumerations
        if(i == (i & iioelts)) {
            set_channel(index, i, *first);
            index++;
        }
    }
    channels.nb = channels.nb + nb_iioelts;
}

static uint64_t get_period(const char* freq)
{
    double frequency = 0;
    if(freq)
        frequency = strtod(freq, NULL);
    return (frequency == 0) ? 0 : (uint64_t)((1.0 / frequency) * 1000000);
}

static bool is_new_event_needed(struct iio_info *infos, const uint64_t u_period)
{
    if(channels.nb == 0)
        return true;

    for(int i = 0; i < channels.nb; i++) {
        if(channels.channels[i].infos == infos)
            if(channels.channels[i].u_period == u_period) //no need for new event
                return false;
    }
    return true;
}

struct client_channels *looking_for_chn(struct iio_info *infos, const char* uid)
{
    struct client_channels *cl_chn = NULL;
    int i = 0;
    while(i < channels.nb) {
        cl_chn = &channels.channels[i];
        if(!strcasecmp(uid, cl_chn->uid) && cl_chn->infos == infos)
            return cl_chn;
        i++;
    }
    return NULL;
}

static bool is_uid_used(struct iio_info *infos, const char* uid)
{
    return looking_for_chn(infos, uid);
}

static void subscribe(struct afb_req request)
{
    const char *value = afb_req_value(request, "event");
    const char *uid = afb_req_value(request, "uid");
    const char *s_iioelts = afb_req_value(request, "args");
    const char *freq = afb_req_value(request, "frequency");

    if(!value || !uid || !s_iioelts) {
        afb_req_fail(request, "failed", "please, fill 'event', 'uid' and 'args' fields");
        return;
    }

    AFB_INFO("subscription with: value=%s, uid=%s, s_iioelts=%s, freq=%s",
            value, uid, s_iioelts, freq);

    bool found = false;
    for(int i = 0; i < IIO_INFOS_SIZE; i++) {
        if(!strcasecmp(value, iio_infos[i].id)) {
            found = true;
            if(is_uid_used(&iio_infos[i], uid)) {
                afb_req_fail(request, "failed", "uid is already used for this event, please changed");
                return;
            }

            struct iio_device *dev = init_dev(iio_infos[i].dev_name);

            uint64_t u_period = get_period( freq);
            enum iio_elements iioelts = (int)treat_iio_elts(s_iioelts);

            struct client_channels *first = NULL;
            struct afb_event* p_event = NULL;
            bool event_needed = is_new_event_needed(&iio_infos[i], u_period);
            if(!event_needed) {
                /*is new client_channel needed*/
                p_event = is_allocation_needed(&iio_infos[i],
                        &iioelts, &first);
            }
            if(!p_event || event_needed) { //new cl_chn or event_needed
                init_channel(dev, &iio_infos[i], iioelts, &first, u_period,
                        uid);
                if(!first) {
                    AFB_ERROR("first is null");
                    return;
                }
                init_event_io(dev, first);
                p_event = first->event;
            }

            if(afb_req_subscribe(request, *p_event) != 0) {
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

static void unsubscribe(struct afb_req request)
{
    const char *value = afb_req_value(request, "event");
    const char *uid = afb_req_value(request, "uid");
    if(!value || !uid) {
        afb_req_fail(request, "failed", "please, fill 'event', 'uid' fields");
        return;
    }

    bool found = false;
    if(value) {
        for(int i = 0; i < IIO_INFOS_SIZE; i++) {
            if(!strcasecmp(value, iio_infos[i].id)) {
                found = true;
                struct client_channels *cl_chn = looking_for_chn(&iio_infos[i], uid);
                if(!cl_chn) {
                    AFB_ERROR("cannot find %s event with uid=%s", value, uid);
                    return;
                }
                if(afb_req_unsubscribe(request, *cl_chn->event) != 0) {
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

const afb_verb_v2 verbs[] = {
	    { .verb = "subscribe",		.session = AFB_SESSION_NONE, .callback = subscribe,		.info = "Subscribe for an event" },
	    { .verb = "unsubscribe",	.session = AFB_SESSION_NONE, .callback = unsubscribe,		.info = "Unsubscribe for an event" },
        { .verb=NULL }
};

const afb_binding_v2 afbBindingV2 = {
        .info = "iio devices service",
        .api = "iiodevices",
        .verbs = verbs
};
