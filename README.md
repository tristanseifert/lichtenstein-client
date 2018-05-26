# lichtenstein_client
A C++ client daemon that implements the Lichtenstein protocol.

Inputs and outputs are handled by pluggable modules, allowing the client to work on a variety of platforms. The core of the client is standards-compliant C++11 with no platform dependencies, and should compile on most UNIX and UNIX-like systems (macOS 10.13 and Linux 4.2 have been tested.)

## Dependencies
All dependencies are pulled in via submodules in the `libs` directory. Be sure to clone submodules as well.

Additionally, the client depends on sqlite3, libuuid, and glog to be present on the destination system as shared libraries.
