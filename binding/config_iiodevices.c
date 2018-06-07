#include "config_iiodevices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AFB_BINDING_VERSION 2
#include <afb/afb-binding.h>

void set_channel_name(char *name, enum iio_elements iioelts)
{
    switch(iioelts) {
        case X:
            strcat(name, "_x"); break;
        case Y:
            strcat(name, "_y"); break;
        case Z:
            strcat(name, "_z"); break;
        default:
            AFB_WARNING("cannot set channel name, no matching iio_elements found: %d", iioelts);
    }
}

enum iio_elements treat_iio_elts(const char *iioelts_string)
{
    enum iio_elements iioelts = 0;
    if(!iioelts_string || !strcasecmp(iioelts_string, ""))
        iioelts = X | Y | Z;
    else {
        if(strchr(iioelts_string, 'x') || strchr(iioelts_string, 'X'))
            iioelts = iioelts | X;
        if(strchr(iioelts_string, 'y') || strchr(iioelts_string, 'Y'))
            iioelts = iioelts | Y;
        if(strchr(iioelts_string, 'z') || strchr(iioelts_string, 'Z'))
            iioelts = iioelts | Z;
    }
    AFB_INFO("ENUMERATION::: %s %d", iioelts_string, iioelts);
    return iioelts;
}

int get_iio_nb(enum iio_elements iioelts)
{
    int nb_iioelts = 0;
    int tmp_mask = 1;
    while(tmp_mask <= iioelts) { //the number of channels is the number of elements
        if((iioelts & tmp_mask) == tmp_mask)
            nb_iioelts++;
        tmp_mask = tmp_mask << 1;
    }
    return nb_iioelts;
}
