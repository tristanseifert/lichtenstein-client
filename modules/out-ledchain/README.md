# PWM Output Plugin
This plugin supports the 2-channel PWM-based output from [this](https://github.com/tristanseifert/lichtenstein-omega2) repository, operating in conjunction with the [plan44 ledchain](https://github.com/plan44/plan44-feed/tree/master/p44-ledchain) kernel module.

The UUID of the output hardware is `845D25C1-C3AB-4D95-97A4-E33FF235C173`.

## Dependencies
In addition to the libraries used by the lichtenstein client, this plugin requires the [libkmod](https://git.kernel.org/pub/scm/utils/kernel/kmod/kmod.git) library to be installed on the system.
