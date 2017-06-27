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

/*
 * escape code definitions for colorized output
 */
#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE    "\033[1;34m"
#define COLOR_RESET   "\033[m"

struct value_struct {
    double temp;
    uint16_t co2;
    char state;
};
int flag = 0;
int rtrn_code = 0;

char *get_path();

int generate_out(struct value_struct *data);

int pseudo_decrypt(uint64_t *in_data);

void print_help();

int main(const int argc, char **argv) {

    int c;
    while ((c = getopt(argc, argv, "ld:h")) != -1) {
        switch (c) {
            case 'h':
                print_help();
                rtrn_code = 0;
                goto RTRN;
            case 'l':
                flag = -1;
                break;
            case 'd': {
                int op = atoi(optarg);
                if (op >= 0 && op <= 3) {
                    flag = op;
                } else {
                    print_help();
                    rtrn_code = 1;
                    goto RTRN;
                }
                break;
            }
            default:
                print_help();
                rtrn_code = 1;
                goto RTRN;
        }
    }

    if (flag > 0)
        printf(GREEN "[STARTING]" COLOR_RESET "\n");

    char *path;
    int file_descriptor, result;
    uint8_t hid_io_feature[9] = {0};

    /* searching for device */
    if (flag > 1)
        printf(BLUE "[INFO]" COLOR_RESET " searching for device\n");
    path = get_path();
    if (strcmp(path, NODEV) == 0) {
        if (flag > 0)
            printf(RED "[ERROR]" COLOR_RESET " can't find device\nExiting...");
        rtrn_code = 2;
        goto RTRN;
    }
    if (flag > 2)
        printf(BLUE "[INFO]" COLOR_RESET " found device on path %s.\n", path);

    /* open device */
    if (flag > 1)
        printf(BLUE "[INFO]" COLOR_RESET " opening device\n");
    file_descriptor = open(path, O_RDWR);
    if (file_descriptor < 0) {
        if (flag > 0)
            printf(RED "[ERROR]" COLOR_RESET " Unable to open device\nExiting...");
        rtrn_code = 3;
        goto RTRN;
    }

    if (flag > 2)
        printf(BLUE "[INFO]" COLOR_RESET " opened device %s successfully.\n", path);

    /* sending feature 'SET_REPORT' */
    if (flag > 1)
        printf(BLUE "[INFO]" COLOR_RESET " sending HID-Feature 'SET_REPORT' to device\n");
    result = ioctl(file_descriptor, HIDIOCSFEATURE(9), hid_io_feature);
    if (result < 0) {
        if (flag > 0)
            printf(RED "[ERROR]" COLOR_RESET " something went wrong while sending HID I/O Feature\nExiting...\n");
        rtrn_code = 4;
        goto CLOSE_DEV;
    }
    if (flag > 2)
        printf(BLUE "[INFO]" COLOR_RESET " HID-Feature 'SET_REPORT' successfully sent to device\n");

    /* reading from device */
    uint64_t recieved_data = 0;
    struct value_struct data = {0, 0, 0};
    if (flag > 1)
        printf(BLUE "[INFO]" COLOR_RESET " start reading from device\n");
    do {
        result = (int) read(file_descriptor, &recieved_data, 8);

        if (result < 0) {
            if (flag > 0)
                printf(RED "[ERROR]" COLOR_RESET " something went wrong while reading from device\nExiting...\n");
            rtrn_code = 5;
            goto CLOSE_DEV;
        } else {
            /* decrypt read bytes */
            if (flag > 2)
                printf(BLUE "[INFO]" COLOR_RESET " successfully received encrypted data from device\n");
            pseudo_decrypt(&recieved_data);

            /* get data from encryption */
            if ((recieved_data >> 56) == 0x50) {
                if (flag > 1)
                    printf(BLUE "[INFO]" COLOR_RESET " successfully decrypted CO2 value\n");
                data.co2 = (uint16_t) (recieved_data >> 40);
                data.state |= 0x1;
            } else if ((recieved_data >> 56) == 0x42) {
                if (flag > 1)
                    printf(BLUE "[INFO]" COLOR_RESET " successfully decrypted temperature\n");
                data.temp = ((recieved_data >> 40) & 0xffff) / 16.0 - 273.15;
                data.state |= 0x2;
            }
        }
    } while (data.state != 3);


    if (flag > 1)
        printf(BLUE "[INFO]" COLOR_RESET " generating output\n");
    /* generating output */
    generate_out(&data);

    /* closing device */
    CLOSE_DEV:
    if (flag > 1)
        printf(BLUE "[INFO]" COLOR_RESET " start closing device\n");
    close(file_descriptor);
    if (flag > 2)
        printf(BLUE "[INFO]" COLOR_RESET " closed device %s successfully.\n" , path);
    if (flag > 0)
        printf(GREEN "[EXITING]" COLOR_RESET "\n");

    RTRN:
    return rtrn_code;
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
        if (flag > 0)
            printf(RED "[ERROR]" COLOR_RESET " can't create udev device\n");
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
            if (flag > 0)
                printf(RED "[ERROR]" COLOR_RESET " Unable to find parent usb device.");
            return NODEV;
        }

        vid = udev_device_get_sysattr_value(dev, "idVendor");
        pid = udev_device_get_sysattr_value(dev, "idProduct");
        int proof = (strcmp(vid, VID) == 0) && (strcmp(pid, PID) == 0);

        udev_device_unref(dev);

        /* return the path of the device if the pid and vid are correct */
        if (proof) {
            return (char *) out;
        }

    }

    return NODEV;
}

int pseudo_decrypt(uint64_t *in_data) {
    uint64_t out = 0;

    /* phase 1: shuffle */
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

    /* phase 3: cyclic shifting */
    uint64_t mask = out << 61;
    out = (out >> 3) | mask;

    /* phase 4: masquerade */
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
    /* have some fun here
     * i.e.
     * 	(i)		updating space-api
     * 	(ii)	save in a file
     * 	(iii)	blink some lights
     */

    if (flag > -1)
        printf(YELLOW "[VALUE]" COLOR_RESET " CO2: %i Temp: %.2lf\n", data->co2, data->temp);
    else
        printf("CO2: %i\nTEM: %.2lf\n", data->co2, data->temp);

    return 0;
}

void print_help() {
    printf("co2monitor [options]\n"
                   "   -h                 display this help\n"
                   "   -l                 run in headless-mode reduces output to a minimum\n"
                   "   -d <debug-level>   higher debuglevel specializes output:\n"
                   "                          0  only colorized output of the CO2-value and the temperature\n"
                   "                          1  like 0 with colorized additional ERROR-Messages\n"
                   "                          2  like 1 with colorized additional INFO-Messages\n"
                   "                          3  like 2 with higher verbosity\n");
}