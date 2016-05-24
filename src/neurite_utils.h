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

#ifndef __NEURITE_UTILS_H__
#define __NEURITE_UTILS_H__

/*
 * Configs
 */

#define OTA_URL_DEFAULT		"http://123.57.208.39:8080/firmware/firmware.bin"
#define TOPIC_HEADER		"/neuro"
#define TOPIC_TO_DEFAULT	"/neuro/chatroom"
#define TOPIC_FROM_DEFAULT	"/neuro/chatroom"
#define SSID1			"ssid1"
#define PSK1			"psk1"
#define SSID2			"ssid2"
#define PSK2			"psk2"
#define MQTT_SERVER		"mqtt.0x61.me"

/*
 * Features
 */
//#define NEURITE_ENABLE_WIFIMULTI
#define NEURITE_ENABLE_SERVER /* typical for dev only */
//#define NEURITE_ENABLE_MDNS
#define NEURITE_ENABLE_DNSPORTAL
#define NEURITE_ENABLE_USER
#define NEURITE_BUILD_DEV

/*
 * Log
 */
#define LOG_ALL		0
#define LOG_DEBUG	1
#define LOG_INFO	2
#define LOG_WARN	3
#define LOG_ERROR	4
#define LOG_FATAL	5
#define LOG_LEVEL	LOG_ALL

#define CMD_SERIAL	Serial
#ifdef NEURITE_BUILD_DEV
#define LOG_SERIAL	Serial
#else
#define LOG_SERIAL	Serial1
#endif

#define __dec		(millis()/1000)
#define __frac		(millis()%1000)
#define APP_PRINTF	LOG_SERIAL.printf

#define log_dbg(msg, args...) \
	do { \
		if (LOG_LEVEL <= LOG_DEBUG) { \
			APP_PRINTF("[%6u.%03u] D/%s: " msg, __dec, __frac, __func__, ##args); \
		} \
	} while (0)
#define log_info(msg, args...) \
	do { \
		if (LOG_LEVEL <= LOG_INFO) { \
			APP_PRINTF("[%6u.%03u] I/%s: " msg, __dec, __frac, __func__, ##args); \
		} \
	} while (0)
#define log_warn(msg, args...) \
	do { \
		if (LOG_LEVEL <= LOG_WARN) { \
			APP_PRINTF("[%6u.%03u] W/%s: " msg, __dec, __frac, __func__, ##args); \
		} \
	} while (0)
#define log_err(msg, args...) \
	do { \
		if (LOG_LEVEL <= LOG_ERROR) { \
			APP_PRINTF("[%6u.%03u] E/%s: " msg, __dec, __frac, __func__, ##args); \
		} \
	} while (0)
#define log_fatal(msg, args...) \
	do { \
		if (LOG_LEVEL <= LOG_FATAL) { \
			APP_PRINTF("[%6u.%03u] F/%s: " msg, __dec, __frac, __func__, ##args); \
		} \
	} while (0)

/*
 * Assertion
 */
#define ASSERT_NONE	0
#define ASSERT_ALL	1
#define ASSERT_LEVEL	ASSERT_ALL

#if (ASSERT_LEVEL >= ASSERT_ALL)
#define _OS_ASSERT(exp) \
	do { \
		if (exp) { \
		} else { \
			log_fatal("assert failed: " #exp " at %s %d!\n", __FILE__, __LINE__); \
			while (1) \
				yield(); \
		} \
	} while (0)
#else
#define _OS_ASSERT(exp)		(void)((0))
#endif

#define dbg_assert(exp)		_OS_ASSERT(exp)

/*
 * Other Utils
 */

#define __bzero(m,s)		memset(m,0,s)

#endif /* __NEURITE_UTILS_H__ */
