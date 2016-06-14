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

#define USER_LOOP_INTERVAL 5000

Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_LOW, 12345);
Adafruit_BME280 bme;
#define SEALEVELPRESSURE_HPA (1013.25)
#define BME280_ADDR 0x76

#ifdef NEURITE_ENABLE_USER_POWERSAVE
static bool b_user_loop_run = true;
#else
static bool b_user_loop_run = false;
#endif

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

#ifdef NEURITE_ENABLE_USER_ONESHOT
static bool b_accomplished = false;
#endif
void neurite_user_worker(void)
{
	/* add user stuff here */
	struct neurite_data_s *nd = &g_nd;
	char topic_to[MQTT_TOPIC_LEN] = {0};
	nd->cfg.get("topic_to", topic_to, MQTT_TOPIC_LEN);

	String json = "{";

	/* lux */
	sensors_event_t event;
	if (!tsl.getEvent(&event))
		event.light = -1;
	json += "\"light\":" + String(event.light);

	/* 'C */
	int t = (int)(bme.readTemperature()*100);
	int t_i = t/100;
	int t_d = t%100;
	json += ",\"temp\":" + String(t_i) + "." + String(t_d);

	/* hPa */
	int p = (int)(bme.readPressure());
	int p_i = p/100;
	int p_d = p%100;
	json += ",\"pres\":" + String(p_i) + "." + String(p_d);

	/* % */
	int h = (int)(bme.readHumidity()*100);
	int h_i = h/100;
	int h_d = h%100;
	json += ",\"humi\":" + String(h_i) + "." + String(h_d);

	/* mA, mV, 'C, mWH, minutes */
	int ac = (256 * __read8(0x55, 0x15) + __read8(0x55, 0x14)) * 357 / 2000;
	int volt = 256 * __read8(0x55, 0x09) + __read8(0x55, 0x08);
	int temp = (25 * (256 * __read8(0x55, 0x07) + __read8(0x55, 0x06)) - 27315)/100;
	int sae = (256 * __read8(0x55, 0x23) + __read8(0x55, 0x22)) * 292 / 200;
	int tte = 256 * __read8(0x55, 0x17) + __read8(0x55, 0x16);
	json += ",\"ac\":" + String(ac);
	json += ",\"volt\":" + String(volt);
	json += ",\"sae\":" + String(sae);
	json += ",\"tte\":" + String(tte);

	json += "}";

	if (nd->mqtt_connected) {
		mqtt_cli.publish(topic_to, (const char *)json.c_str());
#ifdef NEURITE_ENABLE_USER_ONESHOT
		b_accomplished = true;
#endif
	}
	json = String();
}

void neurite_user_loop(void)
{
	struct neurite_data_s *nd = &g_nd;
	static uint32_t prev_time = 0;
	char worktime_str[16] = {0};
	char sleeptime_str[16] = {0};
	uint32_t worktime;
	uint32_t sleeptime;
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
#ifdef NEURITE_ENABLE_USER_POWERSAVE
			nd->cfg.get("worktime", worktime_str, 16);
			nd->cfg.get("sleeptime", sleeptime_str, 16);
			worktime = atoi(worktime_str);
			sleeptime = atoi(sleeptime_str);
			if (worktime <= 0 || sleeptime <= 0) {
				log_warn("illegal worktime %s, or sleeptime %s\n\r", worktime_str, sleeptime_str);
			} else {
				if (millis() > worktime) {
					log_info("ready to sleep %u ms\r\n", sleeptime);
					ESP.deepSleep((sleeptime)*1000UL, RF_DEFAULT);
					while(1);
				}
			}
#endif
#ifdef NEURITE_ENABLE_USER_ONESHOT
			if (!b_accomplished)
				neurite_user_worker();
#else
			neurite_user_worker();
#endif
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
	char topic_to[MQTT_TOPIC_LEN] = {0};
	nd->cfg.get("topic_to", topic_to, MQTT_TOPIC_LEN);
	if (time_ms >= 50) {
		/* do something on button event */
		static int val = 0;
		char buf[4];
		val = 1 - val;
		if (val) {
			b_user_loop_run = true;
			if (nd->mqtt_connected)
				mqtt_cli.publish(topic_to, "user loop on");
		} else {
			b_user_loop_run = false;
			if (nd->mqtt_connected)
				mqtt_cli.publish(topic_to, "user loop off");
		}
	}
}

#if 1
void static __write8(uint8_t _addr, uint8_t reg, uint32_t value)
{
	Wire.beginTransmission(_addr);
	Wire.write(reg);
	Wire.write(value & 0xFF);
	Wire.endTransmission();
}

uint8_t static __read8(uint8_t _addr, uint8_t reg)
{
	Wire.beginTransmission(_addr);
	Wire.write(reg);
	Wire.endTransmission();
	Wire.requestFrom(_addr, 1);
	return Wire.read();
}
#endif

/* will be called after neurite system setup */
void neurite_user_setup(void)
{
	log_dbg("called\n\r");
#if 1
	Wire.begin(12, 14);
	log_dbg("0x0a: 0x%02x\n\r", __read8(0x55, 0x0a));
#endif
	if (!bme.begin(BME280_ADDR))
		log_err("Could not find a valid BME280 sensor, check wiring!\n\r");
	if(!tsl.begin()) {
		log_err("Ooops, no TSL2561 detected ... Check your wiring or I2C ADDR!");
	} else {
#if 0
		sensor_t sensor;
		tsl.getSensor(&sensor);
#endif
		tsl.enableAutoRange(true);
		tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);
	}
}
#endif
