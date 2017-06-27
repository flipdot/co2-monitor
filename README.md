CO<sub>2</sub>-Monitor for Linux
================================

The CO<sub>2</sub>-Monitor uses [the USB-device](Hardware) found by udev for getting the current temperature [°C] and CO<sub>2</sub>-value [ppm] and print them 
to stdout.
If you run in compiling-issues proof that `libudev`-packges are installed on your system.

Hardware
--------
[TFA-Dostmann AirControl Mini CO2 Messgerät](http://www.amazon.de/dp/B00TH3OW4Q)

Credits
-------
Special thanks to (and based on code from) projects of [maddindeiss](https://github.com/maddindeiss/co2monitor) and [Henryk Plötz](https://hackaday.io/project/5301-reverse-engineering-a-low-cost-usb-co-monitor).

It should also be mentioned that parts of the code are based on [torvalds hidraw-example](https://github.com/torvalds/linux/blob/04303f8ec14269b0ea2553863553bc7eaadca1f8/samples/hidraw/hid-example.c)
and [Alan Ott's udev-example](http://www.signal11.us/oss/udev/udev_example.c).

License
-------
This project uses the [MIT](https://opensource.org/licenses/MIT)-license