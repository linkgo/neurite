[English](#) | [中文](#)

# Neurite [![Linux build status](https://travis-ci.org/linkgo/neurite.svg)](https://travis-ci.org/linkgo/neurite)

A serial to MQTT bridge, an easier way to build IoT product with esp8266 Arduino.

![pcb-n-bread](https://raw.githubusercontent.com/linkgo/neurite/master/hardware/neurite-pcb-n-bread.png)

## Contents
- [Introduction](#introduction)
  - [What Is It](#what-is-it)
  - [Who Need It](#who-need-it)
  - [Features](#features)
  - [How It Works](#how-it-works)
- [User Tutorial](#user-tutorial)
- [Developer Tutorial](#developer-tutorial)
- [Credits](#credits)
- [License](#license)


## Introduction

Neurite is named from the neural network, who takes charge of signal and information transmitting.  
In the world of IoT, the amount of smart units may be explosively large.  
Thus we are introducing Neurite, to simplify your next smart design.


### What Is It


* Neurite is an esp8266 Arduino.
* Neurite is a serial to MQTT bridge.
* Neurite is a WiFi module running corresponding firmware.
* Neurite is simply designed to be Plug-n-Play.


### Who Need It

**Neurite is great for you if:**

* You just wish to connect existing hardware to the internet and publish/subscribe data.
* You just want an easy way for all your devices to communicate with each other.

**Neurite can do a lot help if:**

* You'd like to build some MQTT ready product quickly.
* You need a hardware to integrate into your web service without pain.
* You are looking for an easier IoT solution.
* You are neat hackers. ;)


### Features

**Basic:**

* Neurite natively supports MQTT.
* Neurite publishes data from serial port to the internet.
* Neurite subscribes internet data and transfers to serial port.
* Neurite can be dynamically configured over the internet.
* Neurite is OTA enabled, for both firmware and filesystem.
* Neurite will automatically reconnect once there's network issue.

**Advanced:**

* Neurite supports all kinds of sensors and peripherals interfaced with SPI/I2C or GPIO
* Neurite benefits from 3rd party libraries thanks to esp8266 Arduino.
* Neurite is fully customizable and hackable.


### How It Works

Neurite is powered by [esp8266 Arduino](https://github.com/esp8266/Arduino), developed with [PlatformIO](http://platformio.org/).

Typical work flow:

1. Get Neurite connected to local WiFi Access Point.
2. Neurite connects to [linkgo MQTT broker](#), and subscribes to a configurable topic.
3. Neurite listens to serial port and MQTT, transparently bridges them two.

Advanced work flow besides above:

4. Neurite loops user task within a configurable interval.
5. MQTT message callbacks are registered to user logic.


## User Tutorial

1. First time setup.
    > Power on your Neurite. Switch to AP mode for the first time config, by pressing the button (over 5 seconds) until LED stops flashing.  

2. Configure Neurite to connect to your local WiFi Access Point.
    > Connect your phone, pad or laptop to `neurite-*`, navigate your browser to `linkgo.io`, choose the WiFi AP should Neurite connect to. Then Neurite will automatically reboot. 

3. Wait until Neurite gets online.
    > Neurite LED flashes fast indicates trying hard to connect according to the WiFi ssid and psk you just provided. It turns to be breathing once connected.

4. All set.
    > Now you can send/receive messages through Neurite serial port.  
    > And Neurite will automatically connect to the WiFi AP next time.


## Developer Tutorial

TODO


## Credits

[igrr](https://github.com/igrr) and all the brilliant contributors for [esp8266 Arduino](https://github.com/esp8266/Arduino).


## License

> The MIT License (MIT)
>
> Copyright (c) 2015-2016 Linkgo LLC
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.
