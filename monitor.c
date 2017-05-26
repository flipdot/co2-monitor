#include <stdio.h>
#include <libudev.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/hidraw.h>
#include <linux/input.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
/*
 * Ugly hack to work around failing compilation on systems that don't
  * yet populate new version of hidraw.h to userspace.
 */
#ifndef HIDIOCSFEATURE
#warning Please have your distro update the userspace kernel headers
#define HIDIOCSFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x06, len)
#define HIDIOCGFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x07, len)
#endif

#define HIDRAW "hidraw"
#define VID "04d9"
#define PID "a052"
#define NODEV "[NODEV]"

struct value_struct {
    double temp;
    uint16_t co2;
    char state;
};
int flag = -1;

char *get_path();

int generate_out(struct value_struct *data);

uint64_t pseudo_decrypt(uint64_t *in_data);

void print_help();

int main(const int argc, char **argv) {

    int c;
    while ((c = getopt(argc, argv, "ld:h")) != -1) {
        switch (c) {
            case 'h':
                print_help();
                return 0;
            case 'l':
                flag = 1;
                break;
            case 'd': {
                // TODO implement debug levels
                int op = atoi(optarg);
                if (op >= 0 && op <= 3) {
                    flag = op + 3;
                } else {
                    print_help();
                    return 1;
                }
                break;
            }
            default:
                print_help();
                return 1;
        }
    }

    if (flag != 1)
        printf("\033[1;34m[INFO]\033[m starting the monitor.\n");

    char *path;
    int file_descriptor, result;
    uint8_t hid_io_feature[9] = {0};

/* searching for device */
    path = get_path();
    if (strcmp(path, NODEV) == 0) {
        if (flag != 1)
            perror("\033[1;31m[ERROR]\033[m can't find device\nExiting...");
        else
            perror("[ERROR] can't find device\nExiting...");
        return 2;
    }
    if (flag != 1)
        printf("\033[1;34m[INFO]\033[m found dev on path %s.\n", path);

/* open device */
    file_descriptor = open(path, O_RDWR);
    if (file_descriptor < 0) {
        if (flag != 1)
            perror("\033[1;31m[ERROR]\033[m Unable to open device\nExiting...");
        else
            perror("[ERROR] Unable to open device\nExiting...");
        return 3;
    }

    if (flag != 1)
        printf("\033[1;34m[INFO]\033[m opened device %s successfully.\n", path);

/* sending feature 'SET_REPORT' */
    result = ioctl(file_descriptor, HIDIOCSFEATURE(9), hid_io_feature);
    if (result < 0) perror("HIDIOCSFEATURE");

/* reading from device */
    uint64_t recieved_data = 0;
    struct value_struct data = {0, 0, 0};

    do {
        result = (int) read(file_descriptor, &recieved_data, 8);

        if (result < 0) {
            if (flag != 1)
                perror("\033[1;31m[ERROR]\033[m reading from device\nExiting...");
            else
                perror("[ERROR] reading from device\nExiting...");
        } else {
/* decrypt read bytes */
            pseudo_decrypt(&recieved_data);

/* get data from encryption */
            if ((recieved_data >> 56) == 0x50) {
                data.co2 = (uint16_t)(recieved_data >> 40);
                data.state |= 0x1;
            } else if ((recieved_data >> 56) == 0x42) {
                data.temp = ((recieved_data >> 40) & 0xffff) / 16.0 - 273.15;
                data.state |= 0x2;
            }
        }
    } while (data.state != 3);

/* generating output */
    generate_out(&data);

/* closing device */
    close(file_descriptor);

    if (flag != 1)
        printf("\033[1;34m[INFO]\033[m closed device %s successfully.\n", path);

    return 0;
}

char *get_path() {
    const char *out;
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev;
    const char *path, *vid, *pid;

    /* creating udev-device */
    udev = udev_new();

    if (!udev) {
        printf("\033[1;31m[ERROR]\033[m can't create udev device\n");
        return NODEV;
    }

    /* create a list of the devices in the 'hidraw' subsystem. */
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, HIDRAW);
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {


        /* Get the filename of the /sys entry for the device
           and create a udev_device object (dev) representing it */
        path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);

        /* usb_device_get_devnode() returns the path to the device node
           itself in /dev. */
        out = udev_device_get_devnode(dev);


        /* The device pointed to by dev contains information about
           the hidraw device. In order to get information about the
           USB device, get the parent device with the
           subsystem/devtype pair of "usb"/"usb_device". This will
           be several levels up the tree, but the function will find
           it.*/
        dev = udev_device_get_parent_with_subsystem_devtype(
                dev,
                "usb",
                "usb_device");
        if (!dev) {
            printf("\033[1;31m[ERROR]\033[m Unable to find parent usb device.");
            return NODEV;
        }

        vid = udev_device_get_sysattr_value(dev, "idVendor");
        pid = udev_device_get_sysattr_value(dev, "idProduct");
        int proof = strcmp(vid, VID) == 0 && strcmp(pid, PID) == 0;

        udev_device_unref(dev);

        /* return the path of the device if the pid and vid are correct */
        if (proof) {
            return (char *) out;
        }

    }

    return NODEV;
}

uint64_t pseudo_decrypt(uint64_t *in_data) {
    uint64_t out = 0;

    /* phase 1 */
    uint8_t shuffle[8] = {2, 4, 0, 7, 1, 6, 5, 3};

    for (int d = 0; d < 8; ++d) {
        out |= ((*in_data >> (d * 8)) & 0xff) << (shuffle[d] * 8);
    }
    *in_data = 0;

    uint64_t out_tmp = out;
    out = 0;

    /* phase 2 is obsolete */

    /* swapping out */
    for (int j = 0; j < 8; ++j) {
        out |= ((out_tmp >> (j * 8)) & 0xff) << ((7 - j) * 8);
    }


    /* phase 3 */
    uint64_t mask = out << 61;
    out = (out >> 3) | mask;

    /* phase 4 */
    uint8_t cstate[8] = {0x84, 0x47, 0x56, 0xD6, 0x07, 0x93, 0x93, 0x56};
    mask = (uint64_t) 0xFF << 56;

    for (int i = 0; i < 8; ++i) {
        uint8_t buffer = (uint8_t) ((((out & mask) >> 56) - cstate[i]) & 0xff);
        out <<= 8;
        out |= buffer;
    }

    *in_data = out;
    return 0;
}

int generate_out(struct value_struct *data) {
    /* TODO dealing with the data in the data_struct data
     * i.e.
     * 	(i)		updating space-api
     * 	(ii)	save in a file
     * 	(iii)	blink some lights
     */

    if (flag != 1)
        printf("\033[1;33m[VALUE]\033[m CO2: %i Temp: %.2lf\n", data->co2, data->temp);
    else
        printf("%i\n%.2lf\n", data->co2, data->temp);

    return 0;
}

void print_help() {
    printf("co2monitor [options]\n"
                   "    --help      -h                 display this help\n"
                   "    --headless  -l                 run in headless-mode reduces output to an minimum\n"
                   "    --debug     -d <debug-level>   higher debuglevel increases output (range: 0-3)\n");
}