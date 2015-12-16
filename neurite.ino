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
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "FS.h"

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "user_interface.h"
}

#define NEURITE_CMD_BUF_SIZE	256
#define NEURITE_UID_LEN		32
#define NEURITE_TOPIC_LEN	64
#define NEURITE_SSID_LEN	32
#define NEURITE_PSK_LEN		32

#define NEURITE_LED	13
#define NEURITE_BUTTON	0

const char STR_READY[] PROGMEM = "neurite ready";

struct cmd_parser_s;
typedef void (*cmd_parser_cb_fp)(struct cmd_parser_s *cp);

struct cmd_parser_s {
	char *buf;
	uint16_t buf_size;
	uint16_t data_len;
	bool cmd_begin;
	cmd_parser_cb_fp callback;
};

struct neurite_cfg_s {
	char uid[NEURITE_UID_LEN];
	char topic_to[NEURITE_TOPIC_LEN];
	char topic_from[NEURITE_TOPIC_LEN];
	char sta_ssid[NEURITE_SSID_LEN];
	char sta_psk[NEURITE_PSK_LEN];
};

struct neurite_data_s {
	bool wifi_connected;
	bool mqtt_connected;
	struct neurite_cfg_s cfg;
	struct cmd_parser_s *cp;
};

static struct neurite_data_s g_nd;
static struct cmd_parser_s g_cp;
static char cmd_buf[NEURITE_CMD_BUF_SIZE];
static bool b_cfg_ready;

#ifdef WIFI_CONFIG_MULTI
ESP8266WiFiMulti WiFiMulti;
#endif
Ticker ticker_worker;
Ticker ticker_led;
Ticker ticker_cmd;
Ticker ticker_mon;
Ticker ticker_but;
WiFiClient wifi_cli;
PubSubClient mqtt_cli(wifi_cli);

static char uid[32];

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

static int cfg_load_item(struct neurite_data_s *nd)
{
}

static int cfg_save_item(struct neurite_data_s *nd)
{
}

static int cfg_load(struct neurite_data_s *nd)
{
}

static int cfg_save(struct neurite_data_s *nd)
{
}

static bool cfg_check_empty(struct neurite_data_s *nd)
{
	return false;
}

static void wifi_connect(struct neurite_data_s *nd)
{
	log_dbg("Connecting to ");
	Serial.println(SSID1);
#ifdef WIFI_CONFIG_MULTI
	WiFiMulti.addAP(SSID1, PSK1);
	WiFiMulti.addAP(SSID2, PSK2);
#else
	WiFi.begin(SSID1, PSK1);
#endif
}

static inline bool wifi_check_status(struct neurite_data_s *nd)
{
	return (WiFi.status() == WL_CONNECTED);
}

static int ota_over_http(char *url)
{
	if (!url) {
		log_err("invalid url\n\r");
		return -1;
	}
	log_info("ota firmware: %s\n\r", url);

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
	mqtt_cli.connect(nd->cfg.uid);
}

static void ticker_monitor_task(struct neurite_data_s *nd)
{
	nd->wifi_connected = wifi_check_status(nd);
	nd->mqtt_connected = mqtt_check_status(nd);
	if (!nd->wifi_connected && worker_st >= WORKER_ST_2) {
		log_warn("WiFi diconnected\n\r");
		update_worker_state(WORKER_ST_0);
	}
	if (!nd->mqtt_connected && worker_st >= WORKER_ST_4) {
		log_warn("MQTT diconnected\n\r");
		update_worker_state(WORKER_ST_3);
	}
}

static void ticker_worker_task(void)
{
#if 0
	struct neurite_data_s *nd = &g_nd;
	char tmp[64];
	sprintf(tmp, "%s time: %d ms", nd->cfg.uid, millis());
	mqtt_cli.publish(nd->cfg.topic_to, tmp);
#endif
}

static void button_handler(struct neurite_data_s *nd, int dts)
{
	log_dbg("button pressed for %d ms\n\r", dts);
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
		if ((r_curr == HIGH) && (r_prev == LOW)) {
			dts = millis() - ts;
			r_prev = r_curr;
			if (dts > 89)
				button_handler(nd, dts);
		}
	}
}

static inline void stop_ticker_led(struct neurite_data_s *nd)
{
	ticker_led.detach();
}
static inline void start_ticker_led_breath(struct neurite_data_s *nd)
{
	stop_ticker_led(nd);
	ticker_led.attach_ms(50, ticker_led_breath);
}

static inline void start_ticker_led_blink(struct neurite_data_s *nd)
{
	stop_ticker_led(nd);
	ticker_led.attach_ms(50, ticker_led_blink);
}

static inline void stop_ticker_mon(struct neurite_data_s *nd)
{
	ticker_mon.detach();
}

static inline void start_ticker_mon(struct neurite_data_s *nd)
{
	stop_ticker_mon(nd);
	ticker_mon.attach_ms(100, ticker_monitor_task, nd);
}

static inline void stop_ticker_worker(struct neurite_data_s *nd)
{
	ticker_worker.detach();
}

static inline void start_ticker_worker(struct neurite_data_s *nd)
{
	stop_ticker_worker(nd);
	ticker_worker.attach(1, ticker_worker_task);
}

static inline void stop_ticker_but(struct neurite_data_s *nd)
{
	ticker_but.detach();
}

static inline void start_ticker_but(struct neurite_data_s *nd)
{
	stop_ticker_but(nd);
	ticker_but.attach_ms(50, ticker_button_task, nd);
}

static void cmd_completed_cb(struct cmd_parser_s *cp)
{
	struct neurite_data_s *nd = &g_nd;
	dbg_assert(cp);
	if (cp->data_len > 0) {
		if (nd->mqtt_connected)
			mqtt_cli.publish(nd->cfg.topic_to, cp->buf);
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

static inline void stop_ticker_cmd(struct neurite_data_s *nd)
{
	ticker_cmd.detach();
}

static void start_ticker_cmd(struct neurite_data_s *nd)
{
	stop_ticker_cmd(nd);
	ticker_cmd.attach_ms(1, ticker_cmd_task, nd);
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
	log_dbg("Total: %u\n\r"
		"Used: %u\n\r"
		"Block: %u\n\r"
		"Page: %u\n\r"
		"Max open files: %u\n\r"
		"Max path len: %u\n\r",
		info.totalBytes,
		info.usedBytes,
		info.blockSize,
		info.pageSize,
		info.maxOpenFiles,
		info.maxPathLength
	);
}

static void cfg_init(struct neurite_data_s *nd)
{
	__bzero(&nd->cfg, sizeof(struct neurite_cfg_s));
	sprintf(nd->cfg.uid, "neurite-%08x", ESP.getChipId());
#if 0
	sprintf(nd->cfg.topic_to, "/neuro/%s/to", nd->cfg.uid);
	sprintf(nd->cfg.topic_from, "/neuro/%s/to", nd->cfg.uid);
#else
	sprintf(nd->cfg.topic_to, "/neuro/chatroom", nd->cfg.uid);
	sprintf(nd->cfg.topic_from, "/neuro/chatroom", nd->cfg.uid);
#endif
#if 0
	sprintf(nd->cfg->sta_ssid, "%s", STA_SSID);
	sprintf(nd->cfg->sta_pwd, "%s", STA_PASS);
#endif
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

	fs_init(nd);
	cfg_init(nd);

	if (cfg_check_empty(nd)) {
		b_cfg_ready = false;
	} else {
		cfg_load(nd);
		b_cfg_ready = true;
	}

	log_info("chip id: %08x\n\r", system_get_chip_id());
	log_info("uid: %s\n\r", nd->cfg.uid);
	log_info("topic_to: %s\n\r", nd->cfg.topic_to);
	log_info("topic_from: %s\n\r", nd->cfg.topic_from);
#if 0
	log_dbg("device id: %s\n\r", nd->cfg->device_id);
	log_dbg("ssid: %s\n\r", nd->cfg->sta_ssid);
	log_dbg("psk : %s\n\r", nd->cfg->sta_pwd);
	log_dbg("mqtt user: %s\n\r", nd->cfg->mqtt_user);
	log_dbg("mqtt pass: %s\n\r", nd->cfg->mqtt_pass);
#endif
	start_ticker_but(nd);
	start_ticker_cmd(nd);
	log_dbg("out\n\r");
}

/* TODO server test */

ESP8266WebServer *server;
File fsUploadFile;

void handleRoot() {
	server->send(200, "text/plain", "hello from esp8266!");
}

void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server->uri();
	message += "\nMethod: ";
	message += (server->method() == HTTP_GET)?"GET":"POST";
	message += "\nArguments: ";
	message += server->args();
	message += "\n";
	for (uint8_t i=0; i<server->args(); i++){
		message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
	}
	server->send(404, "text/plain", message);
}

static String formatBytes(size_t bytes){
  if (bytes < 1024) {
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)) {
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)) {
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server->hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  LOG_SERIAL.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server->streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server->uri() != "/edit") return;
  HTTPUpload& upload = server->upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    LOG_SERIAL.print("handleFileUpload Name: "); LOG_SERIAL.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //LOG_SERIAL.print("handleFileUpload Data: "); LOG_SERIAL.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    LOG_SERIAL.print("handleFileUpload Size: "); LOG_SERIAL.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server->args() == 0) return server->send(500, "text/plain", "BAD ARGS");
  String path = server->arg(0);
  LOG_SERIAL.println("handleFileDelete: " + path);
  if(path == "/")
    return server->send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server->send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server->send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server->args() == 0)
    return server->send(500, "text/plain", "BAD ARGS");
  String path = server->arg(0);
  LOG_SERIAL.println("handleFileCreate: " + path);
  if(path == "/")
    return server->send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server->send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server->send(500, "text/plain", "CREATE FAILED");
  server->send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server->hasArg("dir")) {server->send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server->arg("dir");
  LOG_SERIAL.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
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
}

/* server test end */

inline void neurite_worker(void)
{
	struct neurite_data_s *nd = &g_nd;
	switch (worker_st) {
		case WORKER_ST_0:
			start_ticker_led_blink(nd);
			wifi_connect(nd);
			update_worker_state(WORKER_ST_1);
			break;
		case WORKER_ST_1:
			if (!wifi_check_status(nd))
				break;
			nd->wifi_connected = true;
			log_dbg("WiFi connected\n\r");
			log_info("IP address: ");
			LOG_SERIAL.println(WiFi.localIP());
			update_worker_state(WORKER_ST_2);
			break;
		case WORKER_ST_2:
			if (digitalRead(NEURITE_BUTTON) == LOW) {
				stop_ticker_led(nd);
				stop_ticker_but(nd);
				ota_over_http(OTA_URL_DEFAULT);
				start_ticker_but(nd);
				start_ticker_led_blink(nd);
			}
			mqtt_config(nd);
			update_worker_state(WORKER_ST_3);
			break;
#if 0
		case WORKER_ST_3:
			if (!mqtt_check_status(nd)) {
				mqtt_connect(nd);
				break;
			}

			nd->mqtt_connected = true;
			mqtt_cli.subscribe(nd->cfg.topic_from);
			char payload_buf[32];
			dbg_assert(payload_buf);
			sprintf(payload_buf, "checkin: %s", nd->cfg.uid);
			mqtt_cli.publish(nd->cfg.topic_to, (const char *)payload_buf);

			start_ticker_led_breath(nd);
			start_ticker_worker(nd);
			start_ticker_mon(nd);
			CMD_SERIAL.println(FPSTR(STR_READY));
			update_worker_state(WORKER_ST_4);
			break;
		case WORKER_ST_4:
			mqtt_cli.loop();
			break;
#else
		case WORKER_ST_3:
			/*
			Dir dir = SPIFFS.openDir("/");
			while (dir.next()) {
				String fileName = dir.fileName();
				size_t fileSize = dir.fileSize();
				log_dbg("file: %s, size: %s\n\r",
					fileName.c_str(), formatBytes(fileSize).c_str());
			}
			*/
			server = new ESP8266WebServer(80);
			server->on("/list", HTTP_GET, handleFileList);
			server->on("/edit", HTTP_GET, [](){
					if(!handleFileRead("/edit.htm")) server->send(404, "text/plain", "FileNotFound");
					});
			server->on("/edit", HTTP_PUT, handleFileCreate);
			server->on("/edit", HTTP_DELETE, handleFileDelete);
			server->on("/edit", HTTP_POST, [](){ server->send(200, "text/plain", ""); }, handleFileUpload);

			server->onNotFound([](){
				if (!handleFileRead(server->uri())) {
					String message = "File Not Found\n\n";
					message += "URI: ";
					message += server->uri();
					message += "\nMethod: ";
					message += (server->method() == HTTP_GET)?"GET":"POST";
					message += "\nArguments: ";
					message += server->args();
					message += "\n";
					for (uint8_t i=0; i<server->args(); i++)
						message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
					server->send(404, "text/plain", message);
				}
			});

			server->on("/all", HTTP_GET, [](){
					String json = "{";
					json += "\"heap\":"+String(ESP.getFreeHeap());
					json += ", \"analog\":"+String(analogRead(A0));
					json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
					json += "}";
					server->send(200, "text/json", json);
					json = String();
					});

			server->on("/", handleRoot);
			server->begin();
			log_dbg("HTTP server started\n\r");
			update_worker_state(WORKER_ST_4);
			break;
		case WORKER_ST_4:
			server->handleClient();
			break;
#endif
		default:
			log_err("unknown worker state: %d\n\r", worker_st);
			break;
	}
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

inline void neurite_cfg_worker(void)
{
	struct neurite_data_s *nd = &g_nd;
	switch (cfg_st) {
		case CFG_ST_0:
			update_cfg_state(CFG_ST_1);
			break;
		case CFG_ST_1:
			break;
		case CFG_ST_2:
			break;
		default:
			log_err("unknown cfg state: %d\n\r", worker_st);
			break;
	}
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
	if (b_cfg_ready)
		neurite_worker();
	else
		neurite_cfg_worker();
}
