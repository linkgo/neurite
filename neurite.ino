/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Linkgo LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Arduino.h>

#include "neurite_utils.h"

#include <ESP8266WiFi.h>
#ifdef WIFI_CONFIG_MULTI
#include <ESP8266WiFiMulti.h>
#endif
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <Ticker.h>

#define SSID1		"ssid1"
#define PSK1		"psk1"
#define SSID2		"ssid2"
#define PSK2		"psk2"

#define NEURITE_LED	13
#define NEURITE_BUTTON	0

#ifdef WIFI_CONFIG_MULTI
ESP8266WiFiMulti WiFiMulti;
#endif
Ticker ticker_worker;
Ticker ticker_led;

//static char *ota_url = "http://accrete.org:8080/firmware/esp.bin";
static char *ota_url = "http://192.168.100.154/firmware/esp.bin";

enum {
	WORKER_ST_0 = 0,
	WORKER_ST_1,
	WORKER_ST_2,
	WORKER_ST_3
};

static int worker_st = WORKER_ST_0;

static inline void update_worker_state(int st)
{
	log_dbg("-> WORKER_ST_%d\n\r", st);
	worker_st = st;
}

static void ticker_led_breath(void)
{
	static int val = 700;
	static int state = 0;
	if (val >= 700)
		state = 1;
	else if (val <= 200)
		state = 0;
	else
		;
	if (state)
		val -= 10;
	else
		val += 10;
	analogWrite(NEURITE_LED, val);
}

static void ticker_led_blink(void)
{
	static int val = 1000;
	analogWrite(NEURITE_LED, val);
	val = (val == 1000) ? 200 : 1000;
}

#ifndef WIFI_CONFIG_MULTI
static int setup_wifi(char *ssid, char *password)
{
	if (!ssid || !password) {
		log_err("invalid args\n\r");
		return -1;
	}

	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);
}
#endif

static inline void config_wifi(void)
{
#ifdef WIFI_CONFIG_MULTI
	WiFiMulti.addAP(SSID1, PSK1);
	WiFiMulti.addAP(SSID2, PSK2);
#else
	setup_wifi(SSID1, PSK1);
#endif
}

static inline bool check_wifi_status(void)
{
#ifdef WIFI_CONFIG_MULTI
	return (WiFiMulti.run() == WL_CONNECTED);
#else
	return (WiFi.status() == WL_CONNECTED);
#endif
}

static int ota_over_http(char *url)
{
	if (!url) {
		log_err("invalid url\n\r");
		return -1;
	}
	log_dbg("ota firmware: %s\n\r", url);

	t_httpUpdate_return ret;
	ret = ESPhttpUpdate.update(url);

	switch (ret) {
		case HTTP_UPDATE_FAILED:
			log_dbg("failed\n\r");
			break;

		case HTTP_UPDATE_NO_UPDATES:
			log_dbg("no updates\n\r");
			break;

		case HTTP_UPDATE_OK:
			log_dbg("ok\n\r");
			break;
	}
	return ret;
}

static void neurite_child_worker(void)
{
	log_dbg("my time is: %d ms\n\r", millis());
}

static inline void start_child_worker(void)
{
	ticker_worker.attach(1, neurite_child_worker);
}

static inline void stop_child_worker(void)
{
	ticker_worker.detach();
}

static inline void neurite_worker(void)
{
	switch (worker_st) {
		case WORKER_ST_0:
			ticker_led.attach_ms(50, ticker_led_blink);
			config_wifi();
			update_worker_state(WORKER_ST_1);
			break;
		case WORKER_ST_1:
			if (!check_wifi_status())
				break;

			log_dbg("WiFi connected\n\r");
			log_dbg("IP address: ");
			USE_SERIAL.println(WiFi.localIP());

			if (digitalRead(NEURITE_BUTTON) == LOW) {
				ticker_led.detach();
				ota_over_http(ota_url);
			}
#if 0
			neurite_mqtt_connect(nd);
#endif
			update_worker_state(WORKER_ST_2);
			break;
		case WORKER_ST_2:
#if 0
			if (!nd->mqtt_connected)
				break;
			MQTT_Subscribe(&nd->mc, nd->nmcfg.topic_from, 1);
			uint8_t *payload_buf = (uint8_t *)os_malloc(32);
			dbg_assert(payload_buf);
			os_sprintf(payload_buf, "checkin: %s", nd->nmcfg.uid);
			MQTT_Publish(&nd->mc, nd->nmcfg.topic_to, payload_buf, strlen(payload_buf), 1, 0);
			os_free(payload_buf);
#endif
			ticker_led.detach();
			ticker_led.attach_ms(50, ticker_led_breath);
			start_child_worker();
			update_worker_state(WORKER_ST_3);
			break;
		case WORKER_ST_3:
			break;
		default:
			log_err("unknown worker state: %d\n\r", worker_st);
			break;
	}
}

void setup()
{
	USE_SERIAL.begin(115200);
//	USE_SERIAL.setDebugOutput(true);
	USE_SERIAL.printf("\n\r");
	USE_SERIAL.flush();
	pinMode(NEURITE_LED, OUTPUT);
	digitalWrite(NEURITE_LED, HIGH);
	pinMode(NEURITE_BUTTON, INPUT);
}

void loop()
{
	neurite_worker();
}
