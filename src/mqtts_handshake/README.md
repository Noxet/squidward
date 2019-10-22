# MQTTs Handshake
This application sends 16 bytes of data using MQTT + TLS, either with PSK or PKI, in order to evaluate different cipher suites.
The selection of chiper suites is done on the server side for convenience.

Refer to the `main/config` folder for different configurations.

## Warning
For PSK, the latest version (>= 4.1) of the SDK must be used due to lack of PSK support in the lower versions.
