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
#include <PubSubClient.h>
#include <Ticker.h>

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "user_interface.h"
}

#define NEURITE_CMD_BUF_SIZE		256
#define NEURITE_UID_LEN			32
#define NEURITE_TOPIC_LEN		64

#define NEURITE_LED	13
#define NEURITE_BUTTON	0

#define __bzero(m,s)	memset(m,0,s)

struct cmd_parser_s;
typedef void (*cmd_parser_cb_fp)(struct cmd_parser_s *cp);

struct cmd_parser_s {
	char *buf;
	uint16_t buf_size;
	uint16_t data_len;
	bool cmd_begin;
	cmd_parser_cb_fp callback;
};

struct neurite_mqtt_cfg_s {
	char uid[NEURITE_UID_LEN];
	char topic_to[NEURITE_TOPIC_LEN];
	char topic_from[NEURITE_TOPIC_LEN];
};

struct neurite_data_s {
	bool wifi_connected;
	bool mqtt_connected;
	struct neurite_mqtt_cfg_s nmcfg;
	struct cmd_parser_s *cp;
};

struct neurite_data_s g_nd;
struct cmd_parser_s g_cp;

#if 0
RINGBUF cmd_rx_rb;
char cmd_rx_buf[256];
#endif
char cmd_buf[NEURITE_CMD_BUF_SIZE];

#ifdef WIFI_CONFIG_MULTI
ESP8266WiFiMulti WiFiMulti;
#endif
Ticker ticker_worker;
Ticker ticker_led;
Ticker ticker_cmd;
WiFiClient wifi_cli;
PubSubClient mqtt_cli(wifi_cli);

static char uid[32];

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
static int wifi_connect(char *ssid, char *password)
{
	if (!ssid || !password) {
		log_err("invalid args\n\r");
		return -1;
	}

	log_dbg("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);
}
#endif

static inline void wifi_config(void)
{
#ifdef WIFI_CONFIG_MULTI
	WiFiMulti.addAP(SSID1, PSK1);
	WiFiMulti.addAP(SSID2, PSK2);
#else
	wifi_connect(SSID1, PSK1);
#endif
}

static inline bool wifi_check_status(void)
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

static void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
	log_dbg("topic (%d): %s\n\r", strlen(topic), topic);
	log_dbg("data (%d): ", length);
	for (int i = 0; i < length; i++)
		LOG_SERIAL.print((char)payload[i]);
	LOG_SERIAL.println();
}

static inline bool mqtt_check_status(void)
{
	return mqtt_cli.connected();
}

static void mqtt_config(void)
{
	mqtt_cli.setServer(MQTT_SERVER, 1883);
	mqtt_cli.setCallback(mqtt_callback);
}

static void mqtt_connect(struct neurite_data_s *nd)
{
	mqtt_config();
	mqtt_cli.connect(nd->nmcfg.uid);
}

static void cmd_completed_cb(struct cmd_parser_s *cp)
{
	dbg_assert(cp);
	if (cp->data_len > 0) {
		log_dbg("msg launch(len %d): %s\n\r", cp->data_len, cp->buf);
		mqtt_cli.publish(g_nd.nmcfg.topic_to, cp->buf);
	}
	__bzero(cp->buf, cp->buf_size);
	cp->data_len = 0;
}

int cmd_parser_init(struct cmd_parser_s *cp, char *buf, uint16_t buf_size)
{
	dbg_assert(cp);
	cp->buf = buf;
	cp->buf_size = buf_size;
	cp->data_len = 0;
	cp->callback = cmd_completed_cb;
	return 0;
}

void neurite_child_worker(void)
{
	struct neurite_data_s *nd = &g_nd;
	char tmp[64];
	sprintf(tmp, "%s time: %d ms", nd->nmcfg.uid, millis());
	mqtt_cli.publish(nd->nmcfg.topic_to, tmp);
}

static inline void start_child_worker(void)
{
	ticker_worker.attach(1, neurite_child_worker);
}

static inline void stop_child_worker(void)
{
	ticker_worker.detach();
}

inline void neurite_worker(void)
{
	struct neurite_data_s *nd = &g_nd;
	switch (worker_st) {
		case WORKER_ST_0:
			ticker_led.attach_ms(50, ticker_led_blink);
			wifi_config();
			update_worker_state(WORKER_ST_1);
			break;
		case WORKER_ST_1:
			if (!wifi_check_status())
				break;

			log_dbg("WiFi connected\n\r");
			log_dbg("IP address: ");
			LOG_SERIAL.println(WiFi.localIP());

			if (digitalRead(NEURITE_BUTTON) == LOW) {
				ticker_led.detach();
				ota_over_http(OTA_URL_DEFAULT);
			}
			update_worker_state(WORKER_ST_2);
			break;
		case WORKER_ST_2:
			if (!mqtt_check_status()) {
				mqtt_connect(nd);
				break;
			}
			mqtt_cli.subscribe(nd->nmcfg.topic_from);
			char payload_buf[32];
			dbg_assert(payload_buf);
			sprintf(payload_buf, "checkin: %s", nd->nmcfg.uid);
			mqtt_cli.publish(nd->nmcfg.topic_to, (const char *)payload_buf);

			ticker_led.detach();
			ticker_led.attach_ms(50, ticker_led_breath);
			start_child_worker();
			update_worker_state(WORKER_ST_3);
			break;
		case WORKER_ST_3:
			mqtt_cli.loop();
			break;
		default:
			log_err("unknown worker state: %d\n\r", worker_st);
			break;
	}
}

static void cmd_parse_byte(struct cmd_parser_s *cp, char value)
{
	dbg_assert(cp);
	switch (value) {
		case '\r':
			if (cp->callback != NULL)
				cp->callback(cp);
			break;
		default:
			if (cp->data_len < cp->buf_size)
				cp->buf[cp->data_len++] = value;
			break;
	}
}

static void ticker_cmd_task(struct neurite_data_s *nd)
{
	char c;
	if (CMD_SERIAL.available() <= 0)
		return;
	c = CMD_SERIAL.read();
	LOG_SERIAL.printf("%c", c);
	cmd_parse_byte(nd->cp, c);
}

void neurite_cmd_init(struct neurite_data_s *nd)
{
	ticker_cmd.attach_ms(1, ticker_cmd_task, nd);
}

void neurite_init(void)
{
	struct neurite_data_s *nd = &g_nd;
	log_dbg("in\n\r");

	__bzero(nd, sizeof(struct neurite_data_s));
	__bzero(&g_cp, sizeof(struct cmd_parser_s));
	nd->cp = &g_cp;
	__bzero(cmd_buf, NEURITE_CMD_BUF_SIZE);
	cmd_parser_init(nd->cp, cmd_buf, NEURITE_CMD_BUF_SIZE);

	/* TODO these items need to be configured at runtime */
	__bzero(&nd->nmcfg, sizeof(struct neurite_mqtt_cfg_s));
	sprintf(nd->nmcfg.uid, "neurite-%08x", ESP.getChipId());
#if 0
	sprintf(nd->nmcfg.topic_to, "/neuro/%s/to", nd->nmcfg.uid);
	sprintf(nd->nmcfg.topic_from, "/neuro/%s/to", nd->nmcfg.uid);
#else
	sprintf(nd->nmcfg.topic_to, "/neuro/chatroom", nd->nmcfg.uid);
	sprintf(nd->nmcfg.topic_from, "/neuro/chatroom", nd->nmcfg.uid);
#endif
#if 0
	sprintf(nd->cfg->sta_ssid, "%s", STA_SSID);
	sprintf(nd->cfg->sta_pwd, "%s", STA_PASS);
#endif
	log_dbg("chip id: %08x\n\r", system_get_chip_id());
	log_dbg("uid: %s\n\r", nd->nmcfg.uid);
	log_dbg("topic_to: %s\n\r", nd->nmcfg.topic_to);
	log_dbg("topic_from: %s\n\r", nd->nmcfg.topic_from);
#if 0
	log_dbg("device id: %s\n\r", nd->cfg->device_id);
	log_dbg("ssid: %s\n\r", nd->cfg->sta_ssid);
	log_dbg("psk : %s\n\r", nd->cfg->sta_pwd);
	log_dbg("mqtt user: %s\n\r", nd->cfg->mqtt_user);
	log_dbg("mqtt pass: %s\n\r", nd->cfg->mqtt_pass);
#endif
	neurite_cmd_init(nd);

	log_dbg("out\n\r");
}

void setup()
{
	LOG_SERIAL.begin(115200);
	LOG_SERIAL.setDebugOutput(false);
	LOG_SERIAL.printf("\n\r");
	LOG_SERIAL.flush();
	pinMode(NEURITE_LED, OUTPUT);
	digitalWrite(NEURITE_LED, HIGH);
	pinMode(NEURITE_BUTTON, INPUT);
	neurite_init();
}

void loop()
{
	neurite_worker();
}
