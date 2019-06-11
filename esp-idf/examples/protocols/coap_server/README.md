
# CoAP server example

(See the README.md file in the upper level 'examples' directory for more information about examples.)  
This CoAP server example is very simplified adaptation of one of the
[libcoap](https://github.com/obgm/libcoap) examples.

CoAP server example will startup a daemon task, receive requests / data from CoAP client and transmit
data to CoAP client.

If the incoming request requests the use of DTLS (connecting to port 5684), then the CoAP server will
try to establish a DTLS session using the previously defined Preshared Key, which must be the same
as the one that the CoAP client is using.

The Constrained Application Protocol (CoAP) is a specialized web transfer protocol for use with
constrained nodes and constrained networks in the Internet of Things.   
The protocol is designed for machine-to-machine (M2M) applications such as smart energy and
building automation.

Please refer to [RFC7252](https://www.rfc-editor.org/rfc/pdfrfc/rfc7252.txt.pdf) for more details.

## How to use example

### Configure the project

```
make menuconfig
```

* Set WiFi SSID under Example Configuration
* Set WiFi Password under Example Configuration
* Set CoAP Preshared Key to use in the connection from the client

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
make -j4 flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Example Output
current CoAP server would startup a daemon task,   
and the log is such as the following:  

```
...
I (332) wifi: mode : sta (30:ae:a4:04:1b:7c)
I (1672) wifi: n:11 0, o:1 0, ap:255 255, sta:11 0, prof:1
I (1672) wifi: state: init -> auth (b0)
I (1682) wifi: state: auth -> assoc (0)
I (1692) wifi: state: assoc -> run (10)
I (1692) wifi: connected with huawei_cw, channel 11
I (1692) wifi: pm start, type: 1

I (2622) event: sta ip: 192.168.3.84, mask: 255.255.255.0, gw: 192.168.3.1
I (2622) CoAP_server: Connected to AP
...
```

If a CoAP client queries the `/Espressif` resource, CoAP server will return `"no data"`  
until a CoAP client does a PUT with different data.

## libcoap Documentation
This can be found at https://libcoap.net/doc/reference/4.2.0/

## Troubleshooting
* Please make sure CoAP client fetchs or puts data under path: `/Espressif` or
fetches `/.well-known/core`

* libcoap logging can be increased by changing `#define COAP_LOGGING_LEVEL 0`
to `#define COAP_LOGGING_LEVEL 7`
