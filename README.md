# Evolution EteSync

[EteSync](https://www.etesync.com/) plugin for [Evolution](https://wiki.gnome.org/Apps/Evolution/).

## BUILD

### The development packages needed
This is for ubuntu, please check simillar development package depending on you disto
```sh
$ sudo apt install libical2-dev/
    libebook1.2-dev
    libecal2.0-dev
    libedata-cal2.0-dev
    libedata-book1.2-dev
    evolution-data-server-dev
    evolution-dev
```

## Module installation
Go to the evolution-etesync folder then run the following commands
```sh
$ mkdir build
$ cd build
$ cmake -DCMAKE_INSTALL_PREFIX=/usr ..
$ make -j
$ sudo make -j install
```

Run `cmake --help` to get list of available generators (the -G argument)
on your platform.

### Note
Please note that **fedora** users may need to also run
```sh
$ export PKG_CONFIG_PATH=/usr/lib/pkgconfig
```
and replace the cmake command with
```sh
$ cmake -DCMAKE_INSTALL_PREFIX=/usr -DLIB_SUFFIX=64 ..
```
