#include <stdio.h>
#include <libudev.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/hidraw.h>
#include <linux/input.h>

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

char *get_path();

int generate_out(int out[8]);

int decrypt(int key[8], int *data, int out[8]);

int main(void)
{
	printf("\033[1;34m[INFO]\033[m starting the monitor.\n");
	// printf("\033[1;31m[ERROR]\033[m can't start");


	char *path;
	int file_descriptor, result;
	char hid_io_feature[9] =
			{(char) 0x00, (char) 0xc4, (char) 0xc6,
			 (char) 0xc0, (char) 0x92, (char) 0x40,
			 (char) 0x23, (char) 0xd, (char) 0x96};


	/* searching for device */
	path = get_path();
	if (strcmp(path, NODEV) == 0)
	{
		perror("\033[1;31m[ERROR]\033[m can't find device");
		return 1;
	}
	printf("\033[1;34m[INFO]\033[m found dev on path %s.\n", path);

	/* open device */
	file_descriptor = open(path, O_RDWR);
	if (file_descriptor < 0)
	{
		perror("\033[1;31m[ERROR]\033[m Unable to open device");
		return 1;
	}

	printf("\033[1;34m[INFO]\033[m opened device %s successfully.\n", path);

	/* sending feature 'SET_REPORT' */
	result = ioctl(file_descriptor, HIDIOCSFEATURE(9), hid_io_feature);
	if (result < 0)
		perror("HIDIOCSFEATURE");


	/* reading from device */
	int encrypted[8] = {0};
	int decrypted[8] = {0};
	char tmp[8] = {0};
	int key[8] = {0xc4, 0xc6, 0xc0, 0x92, 0x40, 0x23, 0xdc, 0x96};

	for (int i = 0; i < 11; i++)
	{
		result = (int) read(file_descriptor, tmp, 16);

		for (int k = 0; k < 8; k++)
			encrypted[k] = tmp[k] & 0xFF;


		if (result < 0)
		{
			perror("\033[1;31m[ERROR]\033[m reading from device ");
		} else
		{
			/* decrypt read bytes */
			decrypt(key, encrypted, decrypted);

			/* generating output */
			generate_out(decrypted);
		}
	}
	/* closing device */
	close(file_descriptor);
	printf("\033[1;34m[INFO]\033[m closed device %s successfully.\n", path);
	return 0;
}

char *get_path()
{
	const char *out;
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *path, *vid, *pid;

	/* creating udev-device */
	udev = udev_new();

	if (!udev)
	{
		printf("\033[1;31m[ERROR]\033[m can't create udev device\n");
		return NODEV;
	}

	/* create a list of the devices in the 'hidraw' subsystem. */
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, HIDRAW);
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, devices)
	{


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
		if (!dev)
		{
			printf("\033[1;31m[ERROR]\033[m Unable to find parent usb device.");
			return NODEV;
		}

		vid = udev_device_get_sysattr_value(dev, "idVendor");
		pid = udev_device_get_sysattr_value(dev, "idProduct");


		udev_device_unref(dev);

		if (strcmp(vid, VID) == 0 && strcmp(pid, PID) == 0)
		{

			return (char *) out;
		}

	}

	return NODEV;
}


int generate_out(int out[8])
{

	if (out[0] == 0x50)
		printf("\033[1;33m[VALUE]\033[m CO2: %i\n", out[1] << 8 | out[2]); // co2

	else if (out[0] == 0x42)
		printf("\033[1;33m[VALUE]\033[m Temp: %.2lf\n", (out[1] << 8 | out[2]) / 16.0 - 273.15); // temperature

	return 0;
}

int decrypt(int key[8], int *data, int out[8])
{
	int cstate[8] = {0x48, 0x74, 0x65, 0x6D, 0x70, 0x39, 0x39, 0x65};
	int shuffel[8] = {2, 4, 0, 7, 1, 6, 5, 3};

	/* phase 1 */
	int phase1[8] = {0};
	for (int d = 0; d < 8; d++)
		phase1[shuffel[d]] = data[d];

	/* phase 2 */
	int phase2[8] = {0};
	for (int d = 0; d < 8; d++)
		phase2[d] = phase1[d] ^ key[d];

	/* phase 3 */
	int phase3[8] = {0};
	for (int d = 0; d < 8; d++)
		phase3[d] = ((phase2[d] >> 3) | (phase2[(d - 1 + 8) % 8] << 5)) & 0xff;

	/* phase 4 */

	int ctmp[8] = {0};
	for (int d = 0; d < 8; d++)
		ctmp[d] = ((cstate[d] >> 4) | (cstate[d] << 4)) & 0xff;

	for (int d = 0; d < 8; d++)
	{
		out[d] = (0x100 + phase3[d] - ctmp[d]) & 0xff;
	}

	return 0;
}