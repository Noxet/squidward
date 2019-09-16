#!/bin/sh

echo "Generating CA key and cert..."
echo "Enter domain name / IP:"
read CADOMAIN
openssl genrsa -out coap_ca.key 2048
openssl req -x509 -new -nodes -key coap_ca.key -subj "/C=US/ST=CA/O=MyOrg, Inc./CN=${CADOMAIN}" -sha256 -days 1024 -out coap_ca.crt
echo "Done!"

echo "Creating server certificate..."
echo "Enter domain name / IP:"
read SRVDOMAIN
openssl genrsa -out coap_server.key 2048
openssl req -new -sha256 -key coap_server.key -subj "/C=US/ST=CA/O=MyOrg, Inc./CN=${SRVDOMAIN}" -out coap_server.csr
openssl x509 -req -in coap_server.csr -CA coap_ca.crt -CAkey coap_ca.key -CAcreateserial -out coap_server.crt -days 500 -sha256
echo "Done"

echo "Creating client certificate..."
echo "Enter domain name / IP:"
read CLDOMAIN
openssl genrsa -out coap_client.key 2048
openssl req -new -sha256 -key coap_client.key -subj "/C=US/ST=CA/O=MyOrg, Inc./CN=${CLDOMAIN}" -out coap_client.csr
openssl x509 -req -in coap_client.csr -CA coap_ca.crt -CAkey coap_ca.key -CAcreateserial -out coap_client.crt -days 500 -sha256
echo "Done"

echo "ESP32 expects the certificate to be in this format"
openssl x509 -in coap_client.crt -text > coap_client.crt
mv coap_ca.crt coap_ca.pem
