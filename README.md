# Archiving

Policy based archiving for CDR, Recordings, PCAP, SMS, MMS, etc.

## Code Languages

 * **C++ (C++11, migrating from C++98)** - Any new code should be written in C++11.
 * **PHP 5.6**

## How to build this?

Checkout the code, then initialise, update submodules and then `make`!

## I see `CMakeLists.txt` and `Makefile`; Why two build systems?

For historical reasons! It started off as a Makefile system, and it worked well with the original IDE used during the
early days; i.e. Eclipse CDT. Later on JetBrain's CLion was adopted as the IDE, which initially only supported `cmake`
for its project description.

In its current state; cmake based system is primarily used for project description and unit tests in the IDE. Makefile
system is still used for formal build definition.

Now that CLion supports Makefile projects, there is an opportunity to go back to Makefiles, but CMake seem to be a far
superior build system, especially for library management etc.
