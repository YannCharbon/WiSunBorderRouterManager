# C++ Wi-SUN border router implentation using Mbed OS APIs

This implementation of a Wi-SUN border router is based on [https://github.com/PelionIoT/nanostack-border-router](https://github.com/PelionIoT/nanostack-border-router). It currently only supports Ethernet as a backhaul interface. The goal of having the border router wrapped in a C++ API is to use it in a more convenient way and also the extend its functionalities using advanced C++.

# Usage example

```
#include "mbed.h"
#include "mbed-trace/mbed_trace.h"
#define TRACE_GROUP "APP"

#include "WiSunBorderRouterManager.h"

int main() {

    mbed_trace_init();

    WiSunBorderRouterManager br("Wi-SUN Network");
    br.start();

    while(1) {
        ThisThread::sleep_for(Kernel::wait_for_u32_forever);
    }
}
```

Please pay attention that the Wi-SUN certificates and key must still be provided in a separate file called `wisun_certificates.h`. This might change in the future.