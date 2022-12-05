# libdenydevice

libdenydevice is a library loaded using LD_PRELOAD to deny applications access to specific devices based on a simple set of configurable rules.

### Motivation

DualSense 2 gamepads are nice but sadly they are not supported by every game.
A great tool to create a virtual Xbox controller these games can make use of is xboxdrv, so in my setup it is automatically started when a DualSense controller is connected.
The initial problem is solved but now there are new difficulties - some games always try to use the first connected gamepad which still is the DualSense controller, other applications use both at the same time so input is processed twice.

libdenydevice can solve these problems by hiding/denying access to specific devices so every game picks up the right controller.

### Building

Since libdenydevice makes use of udev first install udev development files (libudev-dev on Debian based systems).

```sh
git clone https://github.com/bmmlms/libdenydevice.git
cd libdenydevice
git submodule update --init
make
```

### Usage

Make sure libdenydevice can be found by the loader by placing it somewhere in LD_LIBRARY_PATH.
Then start an application:

```sh
LD_PRELOAD=libdenydevice.so LDD_DEBUG=1 LDD_CONFIG=/home/user/libdenydevice_config myapplication
```

LDD_DEBUG enables some logging to stderr, LDD_CONFIG points to the library configuration file to be used.

The configuration is stored in an INI file containing two sections (patterns and attributes).
When an application opens a device file matched by a specified pattern the attributes of the device and its parents are enumerated.
If a configured attribute matches an attribute in the device tree access to the device is denied.
Additionally udev device enumeration is intercepted, if a matching attribute is found in the device tree the device is hidden from the enumeration results.

A configuration to hide virtual Xbox controllers looks like this:

```ini
[patterns]
pattern=/dev/input/event*
pattern=/dev/input/js*

[attributes]
name=Microsoft X-Box 360 pad
```

This configuration can be used to hide DualSense 2 gamepads:

```ini
[patterns]
pattern=/dev/hidraw*
pattern=/dev/input/event*
pattern=/dev/input/js*

[attributes]
driver=playstation
```

Personally I configure the library in .xinitrc to hide controllers created by xboxdrv from every application.
When a game requires a virtual gamepad it is launched with a special configuration file to hide physical controllers.

### Thanks
- [inih library](https://github.com/benhoyt/inih) for easy INI file reading
