#include <optional>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

#define WIFI_LED 2
#define WIFI_LED_ON LOW
#define WIFI_LED_OFF HIGH
#define COMMAND_LED 0
#define COMMAND_LED_ON LOW
#define COMMAND_LED_OFF HIGH
#define BUTTON_PIN 12
#define LONG_PRESS_DELAY 200
#define DOUBLE_PRESS_DELAY 200

struct SocketAddr {
  IPAddress ip;
  int port;
};

enum ButtonPress {
  BUTTON_PRESS_SHORT,
  BUTTON_PRESS_LONG
};

WiFiUDP udp;
char discoveryResponse[4096];

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
}

void setup() {
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, WIFI_LED_OFF);
  pinMode(COMMAND_LED, OUTPUT);
  digitalWrite(COMMAND_LED, COMMAND_LED_OFF);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);

  digitalWrite(WIFI_LED, WIFI_LED_ON);

  Serial.println("Connecting to wifi...");
  connectToWiFi();
  Serial.println("Connected");

  Serial.println("Opening UDP socket...");
  udp.begin(0);
  Serial.println("Opened");

  digitalWrite(WIFI_LED, WIFI_LED_OFF);
}

void sendDiscoveryRequest() {
  udp.beginPacketMulticast(IPAddress(239, 255, 255, 250), 1982, WiFi.localIP());
  udp.write("M-SEARCH * HTTP/1.1\r\nMAN: \"ssdp:discover\"\r\nST: wifi_bulb\r\n");
  udp.endPacket();
}

bool receiveDiscoveryResponse() {
  if (udp.parsePacket()) {
    size_t len = udp.read(discoveryResponse, sizeof(discoveryResponse) - 1);
    discoveryResponse[len] = '\0';
    return true;
  } else {
    return false;
  }
}

std::optional<SocketAddr> parseDiscoveryResponse() {
  char *ptr = discoveryResponse;

  // Find Location header
  ptr = strstr(ptr, "\r\nLocation: ");
  if (!ptr) return std::nullopt;

  //  Find start of IP
  ptr = strstr(ptr, " yeelight://");
  if (!ptr) return std::nullopt;
  ptr += 12;
  char *ipStart = ptr;

  // Find end of IP and start of port
  ptr = strstr(ptr, ":");
  if (!ptr) return std::nullopt;
  char *ipEnd = ptr;
  ptr++;

  // Parse IP
  if (ipEnd - ipStart > 47) return std::nullopt;
  char ipStr[ipEnd - ipStart + 1] = {0};
  memcpy(ipStr, ipStart, ipEnd - ipStart);
  IPAddress ip;
  ip.fromString(ipStr);

  // Parse port
  int port = atoi(ptr);
  if (!port) return std::nullopt;

  return SocketAddr { ip, port };
}

SocketAddr discover() {
  while (true) {
    sendDiscoveryRequest();

    int start = millis();
    bool timeout = false;
    while (!receiveDiscoveryResponse() && !(timeout = millis() > start + 1000));
    if (timeout) continue;

    if (auto address = parseDiscoveryResponse()) {
      return *address;
    }
  }
}

void sendTogglePowerCommand(SocketAddr address) {
  WiFiClient client;
  client.connect(address.ip, address.port);

  client.write("{\"id\": 1, \"method\": \"toggle\", \"params\": []}\r\n");
  client.flush();

  client.stop();
}

std::optional<int> readBrightnessResponse(char *response) {
  char *ptr = response;

  // Find result field
  ptr = strstr(ptr, "\"result\":");
  if (!ptr) return std::nullopt;
  ptr += 9;

  // Find beginning of result array
  ptr = strstr(ptr, "[");
  if (!ptr) return std::nullopt;
  ptr++;

  // Find result string
  ptr = strstr(ptr, "\"");
  if (!ptr) return std::nullopt;
  ptr++;

  // Parse brightness
  int brightness = atoi(ptr);
  if (!brightness) return std::nullopt;

  return brightness;
}

void sendToggleBrightnessCommand(SocketAddr address) {
  WiFiClient client;
  client.connect(address.ip, address.port);
  client.setTimeout(1000);

  client.write("{\"id\": 1, \"method\": \"get_prop\", \"params\": [\"bright\"]}\r\n");
  client.flush();

  char response[32] = {0};
  size_t length = client.readBytesUntil('\n', response, (sizeof response) - 1);
  if (auto brightness = readBrightnessResponse(response)) {
    client.write("{\"id\": 2, \"method\": \"set_bright\", \"params\": [");
    if (brightness < 50) {
      client.write("100");
    } else {
      client.write("1");
    }
    client.write(", \"smooth\", 500]}\r\n");
    client.flush();
  } else {
    Serial.print("Failed to parse brightness response: ");
    Serial.println(response);
  }

  client.stop();
}

ButtonPress readButtonPress() {
  // Ignore double presses
  for (int i = 0; i < DOUBLE_PRESS_DELAY; i++) {
    delay(1);
    if (!digitalRead(BUTTON_PIN)) {
      i = 0;
    }
  }

  while (true) {
    if (!digitalRead(BUTTON_PIN)) break;
    if (WiFi.status() != WL_CONNECTED) ESP.restart();
    delay(10);
  }

  // Debounce
  delay(10);

  for (int i = 0; i < LONG_PRESS_DELAY; i++) {
    delay(1);
    if (digitalRead(BUTTON_PIN)) {
      return BUTTON_PRESS_SHORT;
    }
  }

  return BUTTON_PRESS_LONG;
}

void loop() {
  digitalWrite(COMMAND_LED, COMMAND_LED_OFF);

  ButtonPress buttonPress = readButtonPress();

  digitalWrite(COMMAND_LED, COMMAND_LED_ON);

  Serial.println("Discovering...");
  SocketAddr address = discover();
  Serial.print("Discovered: ");
  Serial.print(address.ip);
  Serial.print(":");
  Serial.println(address.port);

  if (buttonPress == BUTTON_PRESS_SHORT) {
    Serial.println("Sending toggle power command...");
    sendTogglePowerCommand(address);
  } else {
    Serial.println("Sending toggle brightness command...");
    sendToggleBrightnessCommand(address);
  }
  Serial.println("Sent");
}
