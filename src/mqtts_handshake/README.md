# MQTTs Handshake
This application sends 16 bytes of data using MQTT + TLS, either with PSK or PKI, in order to evaluate different cipher suites.
The selection of chiper suites is done on the server side for convenience.

Refer to the `main/config` folder for different configurations.

For use of different curves, copy either the ECDSA-256 or ECDSA-224 cert to `mqtt_server_ca_ecdsa.pem`.

## Warning
For PSK, the latest version (>= 4.1) of the SDK must be used due to lack of PSK support in the lower versions.
