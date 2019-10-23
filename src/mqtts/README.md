# MQTTs
This application sends various sizes of data using MQTT + TLS.

Refer to the `main/config` folder for different configurations.

# Note
Note that PSK is not available in this application due to lack of support in versions < 4.1.
However, PSK ciphers only affect the handshake and thus is not important for the data itself.

Refer to the `mqtts_handshake` application for PSK support.
