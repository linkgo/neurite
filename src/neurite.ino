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
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "user_interface.h"
}

#include "neurite_priv.h"

/* global */
extern struct neurite_data_s g_nd;

/* static */
static struct cmd_parser_s g_cp;
static char cmd_buf[NEURITE_CMD_BUF_SIZE];
static bool b_cfg_ready;

static Ticker ticker_led;
static Ticker ticker_cmd;
static Ticker ticker_mon;
static Ticker ticker_but;
static ESP8266WebServer *server;
static File fsUploadFile;
#ifdef NEURITE_ENABLE_WIFIMULTI
static ESP8266WiFiMulti WiFiMulti;
#endif
static char cfg_buf[NEURITE_CFG_SIZE];

const char STR_READY[] PROGMEM = "neurite ready";

static void reboot(struct neurite_data_s *nd);

enum {
	WORKER_ST_0 = 0,
	WORKER_ST_1,
	WORKER_ST_2,
	WORKER_ST_3,
	WORKER_ST_4
};
static int worker_st = WORKER_ST_0;

static inline void update_worker_state(int st)
{
	log_dbg("-> WORKER_ST_%d\n\r", st);
	worker_st = st;
}

static void file_dump(const char *file)
{
	if (!file) {
		log_warn("invalid pointer!\n\r");
		return;
	}
	File configFile = SPIFFS.open(file, "r");
	if (!configFile) {
		log_err("open failed\n\r");
		return;
	}
	log_dbg("%s:\n\r", file);
	while (configFile.available())
		LOG_SERIAL.write(configFile.read());
	LOG_SERIAL.println();
	configFile.close();
}

static void ota_hold(void)
{
	struct neurite_data_s *nd = &g_nd;
#ifdef NEURITE_ENABLE_USER
	neurite_user_hold();
#endif
	stop_ticker_but(nd);
	stop_ticker_mon(nd);
	stop_ticker_led(nd);
	stop_ticker_cmd(nd);
}

static void ota_release(void)
{
	struct neurite_data_s *nd = &g_nd;
	start_ticker_cmd(nd);
	start_ticker_led_breath(nd);
	start_ticker_mon(nd);
	start_ticker_but(nd);
}

static int ota_over_http(const char *url)
{
	if (!url) {
		log_err("invalid url\n\r");
		return -1;
	}
	CMD_SERIAL.print("ota: ");
	CMD_SERIAL.println(url);

	ota_hold();
	t_httpUpdate_return ret;
	ret = ESPhttpUpdate.update(url);

	switch (ret) {
		case HTTP_UPDATE_FAILED:
			log_err("failed\n\r");
			break;

		case HTTP_UPDATE_NO_UPDATES:
			log_warn("no updates\n\r");
			break;

		case HTTP_UPDATE_OK:
			log_info("ok\n\r");
			break;
	}
	ota_release();
	return ret;
}

static int otafs_over_http(const char *url)
{
	if (!url) {
		log_err("invalid url\n\r");
		return -1;
	}
	CMD_SERIAL.print("otafs: ");
	CMD_SERIAL.println(url);

	ota_hold();
	t_httpUpdate_return ret;
	ret = ESPhttpUpdate.updateSpiffs(url);

	switch (ret) {
		case HTTP_UPDATE_FAILED:
			log_err("failed\n\r");
			break;

		case HTTP_UPDATE_NO_UPDATES:
			log_warn("no updates\n\r");
			break;

		case HTTP_UPDATE_OK:
			log_info("ok\n\r");
			break;
	}
	ota_release();
	return ret;
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
	static int val = 700;
	analogWrite(NEURITE_LED, val);
	val = (val == 700) ? 200 : 700;
}

static bool cfg_get(const char *str, char *buf, size_t size)
{
	struct neurite_data_s *nd = &g_nd;
	if (!buf || !str) {
		log_warn("invalid pointer!\n\r");
		return false;
	}

	StaticJsonBuffer<NEURITE_CFG_SIZE> json_buf;
	JsonObject& json = json_buf.parseObject((const char *)nd->cfg.json_buf);
	if (!json.success()) {
		log_err("parse failed\n\r");
		return false;
	}
	const char *p = json[str];
	if (p) {
		strncpy(buf, p, size);
		//log_dbg("'%s': %s\n\r", str, p);
		return true;
	} else {
		log_warn("no cfg '%s' found\n\r", str, p);
		return false;
	}
}

static bool cfg_set(const char *str, const char *buf, size_t size)
{
	struct neurite_data_s *nd = &g_nd;
	if (!buf || !str) {
		log_warn("invalid pointer!\n\r");
		return false;
	}
	StaticJsonBuffer<NEURITE_CFG_SIZE> json_buf;
	JsonObject& json = json_buf.parseObject((const char *)nd->cfg.json_buf);
	if (!json.success()) {
		log_err("parse failed\n\r");
		return false;
	}
	json[str] = buf;
	json.printTo(nd->cfg.json_buf, NEURITE_CFG_SIZE);
	//log_dbg("'%s': %s\n\r", str, buf);
	return true;
}

static bool cfg_save(const char *file)
{
	struct neurite_data_s *nd = &g_nd;
	if (!file) {
		log_warn("invalid pointer!\n\r");
		return false;
	}
	StaticJsonBuffer<NEURITE_CFG_SIZE> json_buf;
	JsonObject& json = json_buf.parseObject((const char *)nd->cfg.json_buf);
	if (!json.success()) {
		log_err("parse failed\n\r");
		return false;
	}
	File configFile = SPIFFS.open(file, "w");
	if (!configFile) {
		log_err("open failed\n\r");
		return false;
	} else {
		log_dbg("open %s successfully\n\r", file);
	}
	size_t size = json.printTo(configFile);
	log_dbg("wrote %d bytes\n\r", size);
	configFile.close();
	file_dump(file);
	return true;
}

static bool cfg_load(const char *file)
{
	struct neurite_data_s *nd = &g_nd;
	if (!file) {
		log_warn("invalid pointer!\n\r");
		return false;
	}
	File configFile = SPIFFS.open(file, "r");
	if (!configFile) {
		log_err("open failed\n\r");
		return false;
	}
	size_t size = configFile.size();
	if (size > NEURITE_CFG_SIZE) {
		log_err("file too large\n\r");
		configFile.close();
		return false;
	}
	__bzero(nd->cfg.json_buf, NEURITE_CFG_SIZE);
	size = configFile.readBytes(nd->cfg.json_buf, size);
	log_info("load %s size: %d\n\r", file, size);
	configFile.close();
	cfg_dump();
	return true;
}

static bool cfg_dump(void)
{
	struct neurite_data_s *nd = &g_nd;
	StaticJsonBuffer<NEURITE_CFG_SIZE> json_buf;
	JsonObject& json = json_buf.parseObject((const char *)nd->cfg.json_buf);
	if (!json.success()) {
		log_err("parse failed\n\r");
		return false;
	}
	log_dbg("");
	json.printTo(LOG_SERIAL);
	LOG_SERIAL.println();
	return true;
}

static bool cfg_validate(struct neurite_data_s *nd)
{
	log_dbg("in\n\r");
	char ssid[NEURITE_SSID_LEN] = {0};
	nd->cfg.get("ssid", ssid, NEURITE_SSID_LEN);
	/* FIXME How if the real SSID just equals to SSID1? */
	if (strcmp(ssid, SSID1) == 0) {
		log_warn("no ssid cfg\n\r");
		return false;
	}

	log_dbg("out\n\r");
	return true;
}

static void wifi_connect(struct neurite_data_s *nd)
{
	char ssid[NEURITE_SSID_LEN] = {0};
	char psk[NEURITE_PSK_LEN] = {0};
	nd->cfg.get("ssid", ssid, NEURITE_SSID_LEN);
	nd->cfg.get("psk", psk, NEURITE_PSK_LEN);
	log_dbg("Connecting to %s <%s>\n\r", ssid, psk);
	WiFi.mode(WIFI_STA);
	WiFi.hostname(nd->uid);
#ifdef NEURITE_ENABLE_WIFIMULTI
	WiFiMulti.addAP(ssid, psk);
	WiFiMulti.addAP(SSID2, PSK2);
#else
	WiFi.begin(ssid, psk);
#endif
}

static inline bool wifi_check_status(struct neurite_data_s *nd)
{
	return (WiFi.status() == WL_CONNECTED);
}

static bool config_process(struct neurite_data_s *nd, char *token, char *msg, uint32_t size)
{
	char value[NEURITE_CFG_ITEM_LEN];
	if (token == NULL) {
		log_dbg("hit config, nothing\n\r");
	} else {
		log_dbg("hit config: %s\n\r", token);
		__bzero(value, NEURITE_CFG_ITEM_LEN);
		strncpy(value, msg, NEURITE_CFG_ITEM_LEN);
		nd->cfg.set(token, value, NEURITE_CFG_ITEM_LEN);
	}
	nd->cfg.save(NEURITE_CFG_PATH);
	return true;
}

static void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
	struct neurite_data_s *nd = &g_nd;
	log_dbg("topic (%d): %s\n\r", strlen(topic), topic);
	log_dbg("data (%d): ", length);
	for (int i = 0; i < length; i++)
		LOG_SERIAL.print((char)payload[i]);
	LOG_SERIAL.println();
	if (strncmp(topic, nd->topic_private, strlen(nd->topic_private) - 2) == 0) {
		char *subtopic = topic + strlen(nd->topic_private) - 2;
		char *token = NULL;
		token = strtok(subtopic, "/");
		if (token == NULL) {
			log_warn("no subtopic, ignore\n\r");
		} else if (strcmp(token, "config") == 0) {
			token = strtok(NULL, "/");
			char *msg = (char *)malloc(MQTT_MSG_LEN + 1);
			__bzero(msg, sizeof(msg));
			for (int i = 0; i < length; i++)
				msg[i] = payload[i];
			msg[length] = '\0';
			config_process(nd, token, msg, length);
			free(msg);
		} else if (strcmp(token, "ota") == 0) {
			log_dbg("hit ota\n\r");
			char url[MQTT_MSG_LEN];
			__bzero(url, sizeof(url));
			for (int i = 0; i < length; i++)
				url[i] = payload[i];
			ota_over_http(url);
		} else if (strcmp(token, "otafs") == 0) {
			log_dbg("hit otafs\n\r");
			char url[MQTT_MSG_LEN];
			__bzero(url, sizeof(url));
			for (int i = 0; i < length; i++)
				url[i] = payload[i];
			otafs_over_http(url);
		} else if (strcmp(token, "reboot") == 0) {
			reboot(nd);
		} else {
			log_warn("unsupported %s, leave to user\n\r", token);
		}
	}
	char topic_from[MQTT_TOPIC_LEN] = {0};
	nd->cfg.get("topic_from", topic_from, MQTT_TOPIC_LEN);
	if (strcmp(topic, topic_from) == 0) {
		for (int i = 0; i < length; i++)
			CMD_SERIAL.print((char)payload[i]);
		/* FIXME here follows '\r' and '\n' */
		CMD_SERIAL.println();
	}
#ifdef NEURITE_ENABLE_USER
	neurite_user_mqtt(topic, payload, length);
#endif
}

static inline bool mqtt_check_status(struct neurite_data_s *nd)
{
	return mqtt_cli.connected();
}

static void mqtt_config(struct neurite_data_s *nd)
{
	mqtt_cli.setServer(MQTT_SERVER, 1883);
	mqtt_cli.setCallback(mqtt_callback);
}

static void mqtt_connect(struct neurite_data_s *nd)
{
	mqtt_cli.connect(nd->uid);
}

static void ticker_monitor_task(struct neurite_data_s *nd)
{
	nd->wifi_connected = wifi_check_status(nd);
	nd->mqtt_connected = mqtt_check_status(nd);
	if (!nd->wifi_connected && worker_st >= WORKER_ST_2) {
		log_warn("WiFi disconnected\n\r");
		update_worker_state(WORKER_ST_0);
	}
	if (!nd->mqtt_connected && worker_st >= WORKER_ST_4) {
		log_warn("MQTT disconnected\n\r");
		update_worker_state(WORKER_ST_0);
	}
}

static void button_release_handler(struct neurite_data_s *nd, int dts)
{
	log_dbg("button pressed for %d ms\n\r", dts);
#ifdef NEURITE_ENABLE_USER
	neurite_user_button(dts);
#endif
}

static void button_hold_handler(struct neurite_data_s *nd, int dts)
{
	if (dts > 5000) {
		if (SPIFFS.remove(NEURITE_CFG_PATH)) {
			log_info("%s removed\n\r", NEURITE_CFG_PATH);
			reboot(nd);
		} else {
			log_err("%s failed to remove\n\r", NEURITE_CFG_PATH);
		}
	}
}

static void ticker_button_task(struct neurite_data_s *nd)
{
	static int ts = 0;
	static int r_curr = HIGH;
	static int r_prev = HIGH;
	int dts;
	r_curr = digitalRead(NEURITE_BUTTON);
	if ((r_curr == LOW) && (r_prev == HIGH)) {
		ts = millis();
		r_prev = r_curr;
	} else {
		if (r_prev == LOW) {
			dts = millis() - ts;
			r_prev = r_curr;
			if (r_curr == HIGH)
				button_release_handler(nd, dts);
			else
				button_hold_handler(nd, dts);
		}
	}
}

inline void stop_ticker_led(struct neurite_data_s *nd)
{
	ticker_led.detach();
}
inline void start_ticker_led_breath(struct neurite_data_s *nd)
{
	ticker_led.detach();
	stop_ticker_led(nd);
	ticker_led.attach_ms(50, ticker_led_breath);
}
inline void start_ticker_led_blink(struct neurite_data_s *nd)
{
	ticker_mon.detach();
	stop_ticker_led(nd);
	ticker_led.attach_ms(50, ticker_led_blink);
}
inline void stop_ticker_mon(struct neurite_data_s *nd)
{
	ticker_mon.detach();
}
inline void start_ticker_mon(struct neurite_data_s *nd)
{
	ticker_mon.detach();
	stop_ticker_mon(nd);
	ticker_mon.attach_ms(100, ticker_monitor_task, nd);
}
inline void stop_ticker_but(struct neurite_data_s *nd)
{
	ticker_but.detach();
}
inline void start_ticker_but(struct neurite_data_s *nd)
{
	ticker_but.detach();
	stop_ticker_but(nd);
	ticker_but.attach_ms(50, ticker_button_task, nd);
}

static void cmd_completed_cb(struct cmd_parser_s *cp)
{
	struct neurite_data_s *nd = &g_nd;
	dbg_assert(cp);
	if (cp->data_len > 0) {
		if (nd->mqtt_connected) {
			char topic_to[MQTT_TOPIC_LEN] = {0};
			nd->cfg.get("topic_to", topic_to, MQTT_TOPIC_LEN);
			mqtt_cli.publish(topic_to, cp->buf);
		}
		log_dbg("msg %slaunched(len %d): %s\n\r",
			nd->mqtt_connected?"":"not ", cp->data_len, cp->buf);
	}
	__bzero(cp->buf, cp->buf_size);
	cp->data_len = 0;
}

static int cmd_parser_init(struct cmd_parser_s *cp, char *buf, uint16_t buf_size)
{
	dbg_assert(cp);
	cp->buf = buf;
	cp->buf_size = buf_size;
	cp->data_len = 0;
	cp->callback = cmd_completed_cb;
	return 0;
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

inline void stop_ticker_cmd(struct neurite_data_s *nd)
{
	ticker_cmd.detach();
}

inline void start_ticker_cmd(struct neurite_data_s *nd)
{
	ticker_cmd.detach();
	stop_ticker_cmd(nd);
	ticker_cmd.attach_ms(1, ticker_cmd_task, nd);
}

static void reboot(struct neurite_data_s *nd)
{
	log_info("rebooting...\n\r");
	stop_ticker_but(nd);
	stop_ticker_mon(nd);
	stop_ticker_led(nd);
	stop_ticker_cmd(nd);
	ESP.restart();
}

static void fs_init(struct neurite_data_s *nd)
{
	if (!SPIFFS.begin()) {
		log_err("failed to mount fs\n\r");
		dbg_assert(0);
	}
	FSInfo info;
	if (!SPIFFS.info(info)) {
		log_err("failed to mount fs\n\r");
		dbg_assert(0);
	}
	log_dbg("Total: %u\n\r", info.totalBytes);
	log_dbg("Used: %u\n\r", info.usedBytes);
	log_dbg("Block: %u\n\r", info.blockSize);
	log_dbg("Page: %u\n\r", info.pageSize);
	log_dbg("Max open files: %u\n\r", info.maxOpenFiles);
	log_dbg("Max path len: %u\n\r", info.maxPathLength);

	Dir dir = SPIFFS.openDir("/");
	while (dir.next()) {
		String fileName = dir.fileName();
		size_t fileSize = dir.fileSize();
		log_dbg("file: %s, size: %s\n\r",
			fileName.c_str(), formatBytes(fileSize).c_str());
		fileName = String();
	}
}

static void cfg_init(struct neurite_data_s *nd)
{
	__bzero(&nd->cfg, sizeof(struct neurite_cfg_s));
	sprintf(nd->uid, "neurite-%08x", ESP.getChipId());
	log_dbg("uid: %s\n\r", nd->uid);
	sprintf(nd->topic_private, "%s/%s/#", TOPIC_HEADER, nd->uid);
	log_dbg("topic_private: %s\n\r", nd->topic_private);

	nd->cfg.get = cfg_get;
	nd->cfg.set = cfg_set;
	nd->cfg.save = cfg_save;
	nd->cfg.load = cfg_load;
	nd->cfg.dump = cfg_dump;

	if (!nd->cfg.load(NEURITE_CFG_PATH)) {
		log_warn("load cfg failed, try to create\n\r");
		StaticJsonBuffer<NEURITE_CFG_SIZE> json_buf;
		JsonObject& json = json_buf.createObject();
		if (!json.success()) {
			log_fatal("create failed\n\r");
			return;
		}
		json["ssid"] = SSID1;
		json["psk"] = PSK1;
		json["topic_to"] = TOPIC_TO_DEFAULT;
		json["topic_from"] = TOPIC_FROM_DEFAULT;
		json.printTo(nd->cfg.json_buf, NEURITE_CFG_SIZE);
	} else {
		char tmp[NEURITE_CFG_ITEM_SIZE];
		if (!nd->cfg.get("ssid", tmp, NEURITE_CFG_ITEM_SIZE))
			nd->cfg.set("ssid", SSID1, NEURITE_SSID_LEN);
		if (!nd->cfg.get("psk", tmp, NEURITE_CFG_ITEM_SIZE))
			nd->cfg.set("psk", PSK1, NEURITE_PSK_LEN);
		if (!nd->cfg.get("topic_to", tmp, NEURITE_CFG_ITEM_SIZE))
			nd->cfg.set("topic_to", TOPIC_TO_DEFAULT, MQTT_TOPIC_LEN);
		if (!nd->cfg.get("topic_from", tmp, NEURITE_CFG_ITEM_SIZE))
			nd->cfg.set("topic_from", TOPIC_FROM_DEFAULT, MQTT_TOPIC_LEN);
	}
	nd->cfg.save(NEURITE_CFG_PATH);
}

static void handleNotFound(void)
{
	log_dbg("in\r\n");
#ifdef NEURITE_ENABLE_DNSPORTAL
	if (!handleFileRead(server->uri().c_str())) {
		if (!handleFileRead("/index.html"))
			server->send(404, "text/plain", "FileNotFound");
	}
#else
	if (!handleFileRead(server->uri().c_str())) {
		String message = "File Not Found\n\n";
		message += "URI: ";
		message += server->uri();
		message += "\nMethod: ";
		message += (server->method() == HTTP_GET)?"GET":"POST";
		message += "\nArguments: ";
		message += server->args();
		message += "\n";
		for (int i = 0; i < server->args(); i++) {
			message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
		}
		server->send(404, "text/plain", message);
		message = String();
	}
#endif
	log_dbg("out\r\n");
}

static String formatBytes(size_t bytes)
{
	if (bytes < 1024) {
		return String(bytes)+"B";
	} else if (bytes < (1024 * 1024)) {
		return String(bytes/1024.0)+"KB";
	} else if (bytes < (1024 * 1024 * 1024)) {
		return String(bytes/1024.0/1024.0)+"MB";
	} else {
		return String(bytes/1024.0/1024.0/1024.0)+"GB";
	}
}

static String getContentType(String filename)
{
	if (server->hasArg("download")) return "application/octet-stream";
	else if (filename.endsWith(".htm")) return "text/html";
	else if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".json")) return "text/json";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".png")) return "image/png";
	else if (filename.endsWith(".gif")) return "image/gif";
	else if (filename.endsWith(".jpg")) return "image/jpeg";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".xml")) return "text/xml";
	else if (filename.endsWith(".pdf")) return "application/x-pdf";
	else if (filename.endsWith(".zip")) return "application/x-zip";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
	return "text/plain";
}

static bool handleFileRead(const char *p)
{
	String path = String(p);
	log_dbg("in %s\n\r", path.c_str());
	if (path.endsWith("/"))
		path += "index.html";
	String contentType = getContentType(path);
	String pathWithGz = path + ".gz";
	if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
		if (SPIFFS.exists(pathWithGz))
			path += ".gz";
		File file = SPIFFS.open(path, "r");
		log_dbg("streaming size: %u\n\r", file.size());
		size_t sent = server->streamFile(file, contentType);
		log_dbg("stream return sent: %u\n\r", sent);
		file.close();
		path = String();
		contentType = String();
		pathWithGz = String();
		log_dbg("out\r\n");
		return true;
	}
	path = String();
	contentType = String();
	pathWithGz = String();
	log_dbg("out not exists\r\n");
	return false;
}

static void handleFileUpload(void)
{
	log_dbg("in\r\n");
	if (server->uri() != "/edit")
		return;
	HTTPUpload& upload = server->upload();
	if (upload.status == UPLOAD_FILE_START) {
		String filename = upload.filename;
		if (!filename.startsWith("/"))
			filename = "/"+filename;
		log_info("");
		LOG_SERIAL.println(filename);
		fsUploadFile = SPIFFS.open(filename, "w");
		filename = String();
	} else if (upload.status == UPLOAD_FILE_WRITE) {
		//LOG_SERIAL.print("handleFileUpload Data: ");
		//LOG_SERIAL.println(upload.currentSize);
		if (fsUploadFile)
			fsUploadFile.write(upload.buf, upload.currentSize);
	} else if (upload.status == UPLOAD_FILE_END) {
		if (fsUploadFile)
			fsUploadFile.close();
		log_info("done, size: ");
		LOG_SERIAL.println(upload.totalSize);
	}
	log_dbg("out\r\n");
}

static void handleFileDelete(void)
{
	log_dbg("in\r\n");
	if (server->args() == 0)
		return server->send(500, "text/plain", "BAD ARGS");
	String path = server->arg(0);
	log_info("");
	LOG_SERIAL.println(path);
	if (path == "/") {
		log_dbg("out 500\r\n");
		return server->send(500, "text/plain", "BAD PATH");
	}
	if (!SPIFFS.exists(path)) {
		log_dbg("out 404\r\n");
		return server->send(404, "text/plain", "FileNotFound");
	}
	SPIFFS.remove(path);
	server->send(200, "text/plain", "");
	path = String();
	log_dbg("out\r\n");
}

static void handleFileCreate(void)
{
	log_dbg("in\r\n");
	if (server->args() == 0)
		return server->send(500, "text/plain", "BAD ARGS");
	String path = server->arg(0);
	log_info("");
	LOG_SERIAL.println(path);
	if (path == "/") {
		log_dbg("out 500 bad path\r\n");
		return server->send(500, "text/plain", "BAD PATH");
	}
	if (SPIFFS.exists(path)) {
		log_dbg("out 500 file exists\r\n");
		return server->send(500, "text/plain", "FILE EXISTS");
	}
	File file = SPIFFS.open(path, "w");
	if (file) {
		file.close();
	} else {
		log_dbg("out 500 failed\r\n");
		return server->send(500, "text/plain", "CREATE FAILED");
	}
	server->send(200, "text/plain", "");
	path = String();
	log_dbg("out\r\n");
}

static void handleFileList(void)
{
	String path;
	log_dbg("in\r\n");
	if (!server->hasArg("dir")) {
		path = String("/");
	} else {
		path = String(server->arg("dir"));
	}
	log_dbg("");
	LOG_SERIAL.println(path);
	Dir dir = SPIFFS.openDir(path);

	String output = "[";
	while (dir.next()) {
		File entry = dir.openFile("r");
		if (output != "[") output += ',';
		bool isDir = false;
		output += "{\"type\":\"";
		output += (isDir)?"dir":"file";
		output += "\",\"name\":\"";
		output += String(entry.name()).substring(1);
		output += "\"}";
		entry.close();
	}
	output += "]";
	server->send(200, "text/json", output);
	path = String();
	log_dbg("out\r\n");
}

static void handleRoot(void)
{
	log_dbg("in\r\n");
	if (b_cfg_ready) {
		String message = String((const char *)g_nd.uid);
		message += " alive ";
		message += millis();
		message += " ms";
		server->send(200, "text/plain", message);
		message = String();
	} else {
		if (!handleFileRead("/index.html"))
			server->send(404, "text/plain", "FileNotFound");
	}
	log_dbg("out\r\n");
}

static void handleSave(void)
{
	struct neurite_data_s *nd = &g_nd;
	String message;
	log_dbg("in\n\r");
	message += "URI: ";
	message += server->uri();
	message += "\nMethod: ";
	message += (server->method() == HTTP_GET)?"GET":"POST";
	message += "\nArguments: ";
	message += server->args();
	message += "\n\n";
	for (int i = 0; i<server->args(); i++) {
		message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
		if (server->argName(i).equals("ssid")) {
			if (server->arg(i).length() > NEURITE_SSID_LEN) {
				log_warn("ssid too long %d (>%d)\n\r",
					 server->arg(i).length(), NEURITE_SSID_LEN);
				goto err_handle_save;
			} else {
				nd->cfg.set("ssid", server->arg(i).c_str(), NEURITE_SSID_LEN);
			}
		} else if (server->argName(i).equals("password")) {
			if (server->arg(i).length() > NEURITE_PSK_LEN) {
				log_warn("password too long %d (>%d)\n\r",
					 server->arg(i).length(), NEURITE_PSK_LEN);
				goto err_handle_save;
			} else {
				nd->cfg.set("psk", server->arg(i).c_str(), NEURITE_PSK_LEN);
			}
		} else {
			log_warn("%s not handled\n\r", server->arg(i).c_str());
		}
	}
	message += "Jolly good config!\n";
	nd->cfg.save(NEURITE_CFG_PATH);
	server->send(200, "text/plain", message);
	message = String();
	log_dbg("out ok\n\r");
	reboot(nd);
	return;
err_handle_save:
	message += "Bad request\n";
	server->send(400, "text/plain", message);
	message = String();
	log_dbg("out bad\n\r");
	return;
}

static void server_config(struct neurite_data_s *nd)
{
	server = new ESP8266WebServer(80);
	server->on("/list", HTTP_GET, handleFileList);
	server->on("/edit", HTTP_PUT, handleFileCreate);
	server->on("/edit", HTTP_DELETE, handleFileDelete);
	server->on("/edit", HTTP_POST, []() {
		server->send(200, "text/plain", "");
	}, handleFileUpload);
	server->on("/save", HTTP_POST, handleSave);
	server->onNotFound(handleNotFound);

	server->on("/all", HTTP_GET, []() {
		String json = "{";
		json += "\"heap\":"+String(ESP.getFreeHeap());
		json += ", \"analog\":"+String(analogRead(A0));
		json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
		json += "}";
		server->send(200, "text/json", json);
		json = String();
	});
	server->on("/wifiscan", HTTP_GET, []() {
		static bool scanning = false;
		if (scanning) {
			server->send(202, "text/plain", "");
			log_dbg("busy scanning, return\r\n");
			return;
		}
		scanning = true;
		log_dbg("scan start\r\n");
		int n = WiFi.scanNetworks();
		log_dbg("scan done\r\n");
		if (n > 0) {
#if 0
			String json = "{";
			for (int i = 0; i < n; i++) {
				if (i > 0)
					json += ", ";
				json += "\"" + WiFi.SSID(i) + "\":" + String(WiFi.RSSI(i));
			}
			json += "}";
#else
			String json = "[";
			for (int i = 0; i < n; i++) {
				if (i > 0)
					json += ",";
				json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
			}
			json += "]";
#endif
			server->send(200, "text/json", json);
			log_dbg("");
			LOG_SERIAL.println(json);
			json = String();
		} else {
			server->send(204, "text/plain", "No WLAN found");
			log_dbg("No WLAN found\r\n");
		}
		scanning = false;
	});
	server->on("/ip", HTTP_GET, []() {
		String ipstr = String();
		if((WiFi.getMode() & WIFI_STA))
			ipstr = WiFi.localIP().toString();
		else
			ipstr = WiFi.softAPIP().toString();
		server->send(200, "text/plain", ipstr);
		log_dbg("");
		LOG_SERIAL.println(ipstr);
		ipstr = String();
	});

	server->on("/", handleRoot);
}

enum {
	CFG_ST_0 = 0,
	CFG_ST_1,
	CFG_ST_2
};
static int cfg_st = CFG_ST_0;

static inline void update_cfg_state(int st)
{
	log_dbg("-> CFG_ST_%d\n\r", st);
	cfg_st = st;
}

#ifdef NEURITE_ENABLE_MDNS
const char *host = "neurite";
#endif
#ifdef NEURITE_ENABLE_DNSPORTAL
DNSServer dnsServer;
#endif
IPAddress apIP(192, 168, 4, 1);
inline void neurite_cfg_worker(void)
{
	struct neurite_data_s *nd = &g_nd;
	switch (cfg_st) {
		case CFG_ST_0:
			analogWrite(NEURITE_LED, 300);
			WiFi.mode(WIFI_AP);
			WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
			WiFi.softAP(nd->uid);
			LOG_SERIAL.print("AP IP address: ");
			LOG_SERIAL.println(apIP);
			update_cfg_state(CFG_ST_1);
			break;
		case CFG_ST_1:
#ifdef NEURITE_ENABLE_MDNS
			MDNS.begin(host);
			LOG_SERIAL.print("Open http://");
			LOG_SERIAL.print(host);
			LOG_SERIAL.println(".local to get started");
#endif
#ifdef NEURITE_ENABLE_DNSPORTAL
			dnsServer.start(53, "linkgo.io", apIP);
			log_dbg("DNS server started\n\r");
#endif
			server_config(nd);
			server->begin();
			log_dbg("HTTP server started\n\r");
			log_dbg("heap free: %d\n\r", ESP.getFreeHeap());
			update_cfg_state(CFG_ST_2);
			break;
		case CFG_ST_2:
#ifdef NEURITE_ENABLE_DNSPORTAL
			dnsServer.processNextRequest();
#endif
			server->handleClient();
			break;
		default:
			log_err("unknown cfg state: %d\n\r", cfg_st);
			break;
	}
}

inline void neurite_worker(void)
{
	struct neurite_data_s *nd = &g_nd;
	switch (worker_st) {
		case WORKER_ST_0:
			stop_ticker_mon(nd);
			start_ticker_led_blink(nd);
			wifi_connect(nd);
			update_worker_state(WORKER_ST_1);
			break;
		case WORKER_ST_1:
			if (!wifi_check_status(nd))
				break;
			nd->wifi_connected = true;
			log_dbg("WiFi connected\n\r");
			log_info("STA IP address: ");
			LOG_SERIAL.println(WiFi.localIP());
			update_worker_state(WORKER_ST_2);
			break;
		case WORKER_ST_2:
			if (digitalRead(NEURITE_BUTTON) == LOW)
				ota_over_http(OTA_URL_DEFAULT);
			mqtt_config(nd);
			update_worker_state(WORKER_ST_3);
			break;
		case WORKER_ST_3:
			if (!mqtt_check_status(nd)) {
				if (!wifi_check_status(nd))
					update_worker_state(WORKER_ST_0);
				else
					mqtt_connect(nd);
				break;
			}
			nd->mqtt_connected = true;

			char topic_from[MQTT_TOPIC_LEN];
			nd->cfg.get("topic_from", topic_from, MQTT_TOPIC_LEN);
			char topic_to[MQTT_TOPIC_LEN];
			nd->cfg.get("topic_to", topic_to, MQTT_TOPIC_LEN);

			mqtt_cli.subscribe(topic_from);
			log_info("subscribe: %s\n\r", topic_from);
			mqtt_cli.subscribe(nd->topic_private);
			log_info("subscribe: %s\n\r", nd->topic_private);
			char payload_buf[32];
			dbg_assert(payload_buf);
			sprintf(payload_buf, "checkin: %s", nd->uid);
			mqtt_cli.publish(topic_to, (const char *)payload_buf);

			start_ticker_led_breath(nd);
			start_ticker_mon(nd);
			CMD_SERIAL.println(FPSTR(STR_READY));
			log_dbg("heap free: %d\n\r", ESP.getFreeHeap());
#ifdef NEURITE_ENABLE_SERVER
			server_config(nd);
			server->begin();
			log_dbg("HTTP server started\n\r");
			log_dbg("heap free: %d\n\r", ESP.getFreeHeap());
#endif
			update_worker_state(WORKER_ST_4);
			break;
		case WORKER_ST_4:
			mqtt_cli.loop();
#ifdef NEURITE_ENABLE_SERVER
			server->handleClient();
#endif
			break;
		default:
			log_err("unknown worker state: %d\n\r", worker_st);
			break;
	}
}

void neurite_init(void)
{
	struct neurite_data_s *nd = &g_nd;
	log_dbg("in\n\r");

	__bzero(cfg_buf, NEURITE_CFG_SIZE);
	__bzero(nd, sizeof(struct neurite_data_s));
	__bzero(&g_cp, sizeof(struct cmd_parser_s));
	nd->cp = &g_cp;
	__bzero(cmd_buf, NEURITE_CMD_BUF_SIZE);
	cmd_parser_init(nd->cp, cmd_buf, NEURITE_CMD_BUF_SIZE);

	fs_init(nd);
	cfg_init(nd);

	if (cfg_validate(nd)) {
		b_cfg_ready = true;
		log_dbg("cfg ready\n\r");
	} else {
		b_cfg_ready = false;
		log_dbg("cfg not ready\n\r");
	}

	start_ticker_but(nd);
	start_ticker_cmd(nd);
	log_dbg("out\n\r");
}

void setup()
{
	Serial.begin(115200);
	Serial.setDebugOutput(false);
	Serial.printf("\n\r");
	Serial.flush();
	Serial1.begin(115200);
	Serial1.setDebugOutput(false);
	Serial1.printf("\n\r");
	Serial1.flush();
	pinMode(NEURITE_LED, OUTPUT);
	digitalWrite(NEURITE_LED, HIGH);
	pinMode(NEURITE_BUTTON, INPUT);
	log_dbg("heap free: %d\n\r", ESP.getFreeHeap());
	log_dbg("flash size: %d\n\r", ESP.getFlashChipRealSize());
	log_dbg("sketch size: %d\n\r", ESP.getSketchSize());
	log_dbg("sketch free: %d\n\r", ESP.getFreeSketchSpace());

	neurite_init();
}

void loop()
{
	if (b_cfg_ready)
		neurite_worker();
	else
		neurite_cfg_worker();
#ifdef NEURITE_ENABLE_USER
	if (worker_st == WORKER_ST_4)
		neurite_user_loop();
#endif
}
