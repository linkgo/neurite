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

#ifndef __NEURITE_PRIV_H__
#define __NEURITE_PRIV_H__

#define NEURITE_CFG_PATH	"/config.json"
#define NEURITE_CFG_SIZE	1024
#define NEURITE_CFG_ITEM_SIZE	64
#define NEURITE_CMD_BUF_SIZE	256
#define NEURITE_UID_LEN		32
#define NEURITE_SSID_LEN	64
#define NEURITE_PSK_LEN		64

#define NEURITE_CFG_ITEM_LEN	64

#define MQTT_TOPIC_LEN		64
#define MQTT_MSG_LEN		256

#define NEURITE_LED		5
#define NEURITE_BUTTON		0

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
	char json_buf[NEURITE_CFG_SIZE];
	bool (*get)(const char *str, char *buf, size_t size);
	bool (*set)(const char *str, const char *buf, size_t size);
	bool (*save)(const char *file);
	bool (*load)(const char *file);
	bool (*dump)(void);
};

typedef char *(*p_get_cfg)(char *str);

struct neurite_data_s {
	bool wifi_connected;
	bool mqtt_connected;
	struct neurite_cfg_s cfg;
	struct cmd_parser_s *cp;
	char uid[NEURITE_UID_LEN];
	char topic_private[MQTT_TOPIC_LEN];
};

struct neurite_data_s g_nd;
WiFiClient wifi_cli;
PubSubClient mqtt_cli(wifi_cli);

#endif /* __NEURITE_PRIV_H__ */
