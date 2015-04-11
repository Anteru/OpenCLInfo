# OpenCL Info #

This is a simple tool to gather diagnostic information about  the available OpenCL platforms. It consists of three parts: ``lib`` which is a static library that gathers the information, ``cli`` which is a simple command line wrapper that can format the output as Xml, Json or plain text and ``ui`` which is a graphical frontend for the data.

## License

Licensed under the 2-clause BSD. See ``LICENSE.txt`` for details.

## Requirements

A working OpenCL implementation and a recent C++ compiler. Tested with MSVC 2013, GCC 4.8 and Clang 3.4. Older versions of MSVC might not work. GCC was set to `-std=c++11`. Notice that you can get MSVC 2013 for free (http://www.visualstudio.com/products/visual-studio-community-vs).

## Changelog

1.0.1
-----

* Fixed ``clContext`` not being released.
* Improved error handling.
* Fixed superfluous spaces in the version string not being handled correctly.

1.0.0
-----

* Initial release
