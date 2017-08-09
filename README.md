# Deepin movie

Deepin movie is Deepin Desktop Environment Movie Player.

## Dependencies

### Build dependencies

* cmake
* qt5
* ffmpeg
* mpv

### Runtime dependencies

* Qt5 (>= 5.3)
* Qt5-X11extras
* mpv

## Installation

### Build from source code

1. Make sure you have installed all dependencies.

2. Build:
```
$ cd deepin-viewer
$ mkdir Build
$ cd Build
$ cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr ..
$ make
```

3. Install:
```
$ sudo make install
```

When install complete, the executable binary file is placed into `/usr/bin/deepin-movie`.

## Getting help

Any usage issues can ask for help via
* [Gitter](https://gitter.im/orgs/linuxdeepin/rooms)
* [IRC Channel](https://webchat.freenode.net/?channels=deepin)
* [Official Forum](https://bbs.deepin.org/)
* [Wiki](http://wiki.deepin.org/)

## Getting involved

We encourage you to report issues and contribute changes
* [Contribution guide for users](http://wiki.deepin.org/index.php?title=Contribution_Guidelines_for_Users)
* [Contribution guide for developers](http://wiki.deepin.org/index.php?title=Contribution_Guidelines_for_Developers)

## License

Deepin Movie is licensed under [GPLv3](LICENSE).
