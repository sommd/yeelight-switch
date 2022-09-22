# Yeelight Switch

Network light switch for a Yeelight, made for Adafruit Feather HUZZAH ESP8266 (but will probably work on any Arduino-compatible ESP8266-based board).

Note: Will only work with one Yeelight on the network because I only have one and that's all I need.

## Setup

Create a `config.h` file with:

```c
#define WIFI_SSID "wifi ssid"
#define WIFI_PASSWD "wifi password"
```

Button should be connected to pin 12 and ground.

## Usage

When light is off:
- Short press to turn on at minimum brightness.
- Long press to turn on at maximum brightness.

When light is on:
- Short press to turn off.
- Long press to toggle minimum/maximum brightness.
