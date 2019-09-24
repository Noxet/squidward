# squidward
A research project at Lund University investigating IoT protocols with added security

# Set up

## Install ESP32 SDK
Follow the ESP32 "Get Started" guide and install IDF version 3.3.

Set the environment variable `SQUIDWARD_PATH` to point to the root folder of this repo.

## Install Patches
To support DTLS in the stable branch, some files need to be patched, as described next.

In the `$IDF_PATH/components/coap/libcoap` folder, checkout a newer version of the library by running 

`git checkout 98954eb`.

In the `$IDF_PATH/components/coap/libcoap/ext/tinydtls` folder run 

`git checkout 7f8c86e`.

Copy `patch/libcoap/coap_io.c` and replace `$IDF_PATH/components/coap/libcoap/src/coap_io.c`.

Copy all files and folders in `patch/coap` and replace the files in `$IDF_PATH/components/coap`.

Copy `patch/mbedtls/esp_timing.c` to `$IDF_PATH/components/mbedtls/port/`.

# Build
Running `make builds` in the top folder will generate all configured binary files.
To generate binaries for a specific application, run `make builds` inside an application folder in src.
