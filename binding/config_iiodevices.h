#ifndef _CONFIG_IIODEVICES_
#define _CONFIG_IIODEVICES_

#include <stdio.h>
#include <stdlib.h>

#define IIODEVICE "/sys/bus/iio/devices/"

struct iio_info {
    const char *dev_name;
    const char *id;
};

#define IIO_INFOS_SIZE 3
static struct iio_info iio_infos[IIO_INFOS_SIZE] = {
 { "16-001d", "accel"},
 { "16-001d", "magn"},
 { "16-006b", "anglvel"}
};

enum iio_elements { X = 1, Y = 2, Z = 4 };

void set_channel_name(char *name, enum iio_elements iioelts);
enum iio_elements treat_iio_elts(const char *iioelts_string);
enum iio_infos treat_iio_infos(const char *infos);
int get_iio_nb(enum iio_elements iioelts);

#endif //_CONFIG_IIODEVICES_
