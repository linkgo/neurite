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

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

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

#define USER_LOOP_INTERVAL 1000

#define TFT_CS     15 // tied to ground
#define TFT_RST    3
#define TFT_DC     1

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);

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

void neurite_user_worker(void)
{
	/* add user stuff here */
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
	log_dbg("\n\r");

	// Use this initializer (uncomment) if you're using a 1.44" TFT
	tft.initR(INITR_144GREENTAB);   // initialize a ST7735S chip, black tab

	//Serial.println("Initialized");

	uint16_t time = millis();
	tft.fillScreen(ST7735_BLACK);
	time = millis() - time;

	//Serial.println(time, DEC);
	delay(500);

	// large block of text
	tft.fillScreen(ST7735_BLACK);

	//bmpDraw("/logo.bmp", 39, 0);

	tft.setCursor(0, 8*7);
	tft.setTextColor(ST7735_WHITE);
	tft.print("  Made with love by  ");
	tft.setCursor(0, 8*8);
	tft.print("      linkgo.io");
	tft.setCursor(0, 8*10);

	delay(2000);
}

#define BUFFPIXEL 20

void bmpDraw(char *filename, uint8_t x, uint8_t y) {

	File     bmpFile;
	int      bmpWidth, bmpHeight;   // W+H in pixels
	uint8_t  bmpDepth;              // Bit depth (currently must be 24)
	uint32_t bmpImageoffset;        // Start of image data in file
	uint32_t rowSize;               // Not always = bmpWidth; may have padding
	uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
	uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
	boolean  goodBmp = false;       // Set to true on valid header parse
	boolean  flip    = true;        // BMP is stored bottom-to-top
	int      w, h, row, col;
	uint8_t  r, g, b;
	uint32_t pos = 0, startTime = millis();

	if((x >= tft.width()) || (y >= tft.height())) return;

	LOG_SERIAL.println();
	LOG_SERIAL.print("Loading image '");
	LOG_SERIAL.print(filename);
	LOG_SERIAL.println('\'');

	// Open requested file on SD card
	if ((bmpFile = SPIFFS.open(filename, "r")) == NULL) {
		LOG_SERIAL.print("File not found");
		//mqtt_cli.publish("/neuro/chatroom", "file not found");
		return;
	}

	// Parse BMP header
	if(read16(bmpFile) == 0x4D42) { // BMP signature
		//mqtt_cli.publish("/neuro/chatroom", "bmp signature");
		LOG_SERIAL.print("File size: "); LOG_SERIAL.println(read32(bmpFile));
		(void)read32(bmpFile); // Read & ignore creator bytes
		bmpImageoffset = read32(bmpFile); // Start of image data
		LOG_SERIAL.print("Image Offset: "); LOG_SERIAL.println(bmpImageoffset, DEC);
		// Read DIB header
		LOG_SERIAL.print("Header size: "); LOG_SERIAL.println(read32(bmpFile));
		bmpWidth  = read32(bmpFile);
		bmpHeight = read32(bmpFile);
		if(read16(bmpFile) == 1) { // # planes -- must be '1'
			bmpDepth = read16(bmpFile); // bits per pixel
			LOG_SERIAL.print("Bit Depth: "); LOG_SERIAL.println(bmpDepth);
			if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

				goodBmp = true; // Supported BMP format -- proceed!
				LOG_SERIAL.print("Image size: ");
				LOG_SERIAL.print(bmpWidth);
				LOG_SERIAL.print('x');
				LOG_SERIAL.println(bmpHeight);

				// BMP rows are padded (if needed) to 4-byte boundary
				rowSize = (bmpWidth * 3 + 3) & ~3;

				// If bmpHeight is negative, image is in top-down order.
				// This is not canon but has been observed in the wild.
				if(bmpHeight < 0) {
					bmpHeight = -bmpHeight;
					flip      = false;
				}

				// Crop area to be loaded
				w = bmpWidth;
				h = bmpHeight;
				if((x+w-1) >= tft.width())  w = tft.width()  - x;
				if((y+h-1) >= tft.height()) h = tft.height() - y;

				// Set TFT address window to clipped image bounds
				tft.setAddrWindow(x, y, x+w-1, y+h-1);

				for (row=0; row<h; row++) { // For each scanline...

					// Seek to start of scan line.  It might seem labor-
					// intensive to be doing this on every line, but this
					// method covers a lot of gritty details like cropping
					// and scanline padding.  Also, the seek only takes
					// place if the file position actually needs to change
					// (avoids a lot of cluster math in SD library).
					if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
						pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
					else     // Bitmap is stored top-to-bottom
						pos = bmpImageoffset + row * rowSize;
					if(bmpFile.position() != pos) { // Need seek?
						bmpFile.seek(pos, SeekSet);
						buffidx = sizeof(sdbuffer); // Force buffer reload
					}

					for (col=0; col<w; col++) { // For each pixel...
						// Time to read more pixel data?
						if (buffidx >= sizeof(sdbuffer)) { // Indeed
							bmpFile.read(sdbuffer, sizeof(sdbuffer));
							buffidx = 0; // Set index to beginning
						}

						// Convert pixel from BMP to TFT format, push to display
						b = sdbuffer[buffidx++];
						g = sdbuffer[buffidx++];
						r = sdbuffer[buffidx++];
						tft.pushColor(tft.Color565(r,g,b));
					} // end pixel
				} // end scanline
				LOG_SERIAL.print("Loaded in ");
				LOG_SERIAL.print(millis() - startTime);
				LOG_SERIAL.println(" ms");
			} // end goodBmp
		}
	}

	bmpFile.close();
	if(!goodBmp) {
		LOG_SERIAL.println("BMP format not recognized.");
		//mqtt_cli.publish("/neuro/chatroom", "not recognized");
	}
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File f) {
	uint16_t result;
	((uint8_t *)&result)[0] = f.read(); // LSB
	((uint8_t *)&result)[1] = f.read(); // MSB
	return result;
}

uint32_t read32(File f) {
	uint32_t result;
	((uint8_t *)&result)[0] = f.read(); // LSB
	((uint8_t *)&result)[1] = f.read();
	((uint8_t *)&result)[2] = f.read();
	((uint8_t *)&result)[3] = f.read(); // MSB
	return result;
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
		int16_t y = tft.getCursorY();
		if ((y + 8) >= ST7735_TFTHEIGHT_144) {
			y = 0;
			tft.fillScreen(ST7735_BLACK);
		} else {
			y = y + 8;
		}
		tft.setCursor(0, y);
		tft.setTextColor(ST7735_WHITE);
		tft.setTextWrap(true);
		char text[MQTT_MSG_LEN];
		__bzero(text, sizeof(text));
		for (int i = 0; i < length; i++)
			text[i] = payload[i];
		tft.print((char *)text);
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
