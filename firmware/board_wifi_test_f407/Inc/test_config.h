#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* Local test credentials supplied by the user; never print the password. */
#define WIFI_SSID             "YOUR_WIFI_SSID"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

/* Computer IPv4 on the YOUR_WIFI_SSID hotspot when this test was prepared. */
#define TEST_SERVER_HOST      "192.0.2.10"
#define TEST_HTTP_PORT        18080U
#define TEST_MQTT_PORT        1883U
#define TEST_TOPIC_PREFIX     "f407/wifi_test/"

#endif
