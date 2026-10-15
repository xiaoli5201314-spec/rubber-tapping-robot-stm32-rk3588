#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* Local test credentials supplied by the user; never print the password. */
#define WIFI_SSID             "YOUR_WIFI_SSID"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

/* Empty selects the gateway reported by AT+CIPSTA? on the PC hotspot. */
#define TEST_SERVER_HOST      ""
#define TEST_MQTT_PORT        1883U
#define TEST_TOPIC_PREFIX     "f407/web_test/"

/* Only the separate board_rc_web_test_f407 target enables receiver testing. */
#ifndef WEB_RC_TEST
#define WEB_RC_TEST           0
#endif
#ifndef WEB_NO_LCD
#define WEB_NO_LCD            0
#endif
#ifndef WEB_MOTOR_TEST
#define WEB_MOTOR_TEST        0
#endif
#if WEB_MOTOR_TEST && (!WEB_RC_TEST || !WEB_NO_LCD)
#error Motor test requires the RC capture and no-LCD target.
#endif
#if WEB_MOTOR_TEST
#include "board_config.h"
#if MOTOR_MAX_OUTPUT_PERMILLE == 300
#define WEB_FIRMWARE_VERSION  "mqttmotor30"
#elif MOTOR_MAX_OUTPUT_PERMILLE == 600
#define WEB_FIRMWARE_VERSION  "mqttmotor60"
#else
#define WEB_FIRMWARE_VERSION  "mqttmotor100"
#endif
#elif WEB_RC_TEST
#define WEB_FIRMWARE_VERSION  "mqttrc1"
#else
#define WEB_FIRMWARE_VERSION  "mqttweb1"
#endif

#endif
