/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2016 Linkgo LLC
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
#ifdef NEURITE_ENABLE_WIFIMULTI
#include <ESP8266WiFiMulti.h>
#endif
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#ifdef NEURITE_ENABLE_MDNS
#include <ESP8266mDNS.h>
#endif
#ifdef NEURITE_ENABLE_DNSPORTAL
#include <DNSServer.h>
#endif
#include <PubSubClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "FS.h"

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "user_interface.h"
}

#include "neurite_priv.h"

extern struct neurite_data_s g_nd;

#ifdef NEURITE_ENABLE_USER
/*
 * User Advanced Development
 *
 * Brief:
 *     Below interfaces are objected to advanced development based on Neurite.
 *     Also these code can be take as a reference. Thus they are extended
 *     features, which are not mandatory in Neurite core service.
 *
 * Benefits:
 *     Thanks to Neurite, it's more easily to use below features:
 *     1. OTA
 *     2. MQTT
 *     3. Peripherals
 */

#define USER_LOOP_INTERVAL	10
#define USER_BIG_BUTTON		14

static bool b_user_loop_run = true;

enum {
	USER_ST_0 = 0,
	USER_ST_1,
};
static int user_st = USER_ST_0;

static inline void update_user_state(int st)
{
	log_dbg("-> USER_ST_%d\n\r", st);
	user_st = st;
}

static void release_handler(int dts)
{
	struct neurite_data_s *nd = &g_nd;
	log_dbg("button pressed for %d ms\n\r", dts);
	if (dts > 50) {
		if (nd->mqtt_connected) {
			char topic_to[MQTT_TOPIC_LEN] = {0};
			nd->cfg.get("topic_to", topic_to, MQTT_TOPIC_LEN);
				mqtt_cli.publish(topic_to, "egg pain!");
		}
	}
}

static void hold_handler(int dts)
{
	if (dts > 5000) {
		log_info("%s\n\r");
	}
}

void neurite_user_worker(void)
{
	static int ts = 0;
	static int r_curr = HIGH;
	static int r_prev = HIGH;
	int dts;
	r_curr = digitalRead(USER_BIG_BUTTON);
	if ((r_curr == LOW) && (r_prev == HIGH)) {
		ts = millis();
		r_prev = r_curr;
	} else {
		if (r_prev == LOW) {
			dts = millis() - ts;
			r_prev = r_curr;
			if (r_curr == HIGH)
				release_handler(dts);
			else
				hold_handler(dts);
		}
	}
}

void neurite_user_loop(void)
{
	static uint32_t prev_time = 0;
	if (b_user_loop_run == false)
		return;
	if ((millis() - prev_time) < USER_LOOP_INTERVAL)
		return;
	prev_time = millis();
	switch (user_st) {
		case USER_ST_0:
			neurite_user_setup();
			update_user_state(USER_ST_1);
			break;
		case USER_ST_1:
			neurite_user_worker();
			break;
		default:
			log_err("unknown user state: %d\n\r", user_st);
			break;
	}
}

/* called on critical neurite behavior such as OTA */
void neurite_user_hold(void)
{
	log_dbg("\n\r");
	update_user_state(USER_ST_0);
}

/* will be called after neurite system setup */
void neurite_user_setup(void)
{
	pinMode(USER_BIG_BUTTON, INPUT);
	log_dbg("\n\r");
}

/* called once on mqtt message received */
void neurite_user_mqtt(char *topic, byte *payload, unsigned int length)
{
	struct neurite_data_s *nd = &g_nd;
	if (strncmp(topic, nd->topic_private, strlen(nd->topic_private) - 2) == 0) {
		char *subtopic = topic + strlen(nd->topic_private) - 2;
		char *token = NULL;
		token = strtok(subtopic, "/");
		if (strcmp(token, "io") == 0) {
			log_dbg("hit io, payload: %c\n\r", payload[0]);
		}
	}
	char topic_from[MQTT_TOPIC_LEN] = {0};
	nd->cfg.get("topic_from", topic_from, MQTT_TOPIC_LEN);
	if (strcmp(topic, topic_from) == 0) {
	}
}

/* time_ms: the time delta in ms of button press/release cycle. */
void neurite_user_button(int time_ms)
{
	struct neurite_data_s *nd = &g_nd;
	if (time_ms >= 50) {
		/* do something on button event */
		if (nd->mqtt_connected) {
			static int val = 0;
			char buf[4];
			val = 1 - val;

			char topic_to[MQTT_TOPIC_LEN] = {0};
			nd->cfg.get("topic_to", topic_to, MQTT_TOPIC_LEN);

			if (val)
				mqtt_cli.publish(topic_to, "light on");
			else
				mqtt_cli.publish(topic_to, "light off");
		}
	}
}
#endif
