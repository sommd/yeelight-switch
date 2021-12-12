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

struct SocketAddr {
  IPAddress ip;
  int port;
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

void sendToggleCommand(SocketAddr address) {
  WiFiClient client;
  client.connect(address.ip, address.port);

  client.write("{\"id\": 1, \"method\": \"toggle\", \"params\": []}\r\n");
  client.flush();

  client.stop();
}

void loop() {
  digitalWrite(COMMAND_LED, COMMAND_LED_OFF);

  // Debounce
  for (int i = 0; i < 50; i++) {
    delay(10);
    if (!digitalRead(BUTTON_PIN)) {
      i = 0;
    }
  }

  while (true) {
    if (!digitalRead(BUTTON_PIN)) break;
    if (WiFi.status() != WL_CONNECTED) ESP.restart();
    delay(10);
  }

  digitalWrite(COMMAND_LED, COMMAND_LED_ON);

  Serial.println("Discovering...");
  SocketAddr address = discover();
  Serial.print("Discovered: ");
  Serial.print(address.ip);
  Serial.print(":");
  Serial.println(address.port);

  Serial.println("Sending toggle command...");
  sendToggleCommand(address);
  Serial.println("Sent");
}
