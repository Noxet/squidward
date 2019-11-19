# MQTTs
This application sends various sizes of data using MQTT + TLS.

Refer to the `main/config` folder for different configurations.

For use of different curves, copy either the ECDSA-256 or ECDSA-224 cert to `mqtt_server_ca_ecdsa.pem`.

# Note
Note that PSK is not available in this application due to lack of support in versions < 4.1.
However, PSK ciphers only affect the handshake and thus is not important for the data itself.

Refer to the `mqtts_handshake` application for PSK support.
