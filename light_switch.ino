#include <optional>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

#define WIFI_LED 2
#define WIFI_LED_ON LOW
#define WIFI_LED_OFF HIGH
#define STATUS_LED 0
#define STATUS_LED_ON LOW
#define STATUS_LED_OFF HIGH
#define BUTTON_PIN 12
#define BUTTON_PRESSED 0
#define DEBOUNCE_DELAY 10
#define LONG_PRESS_DELAY 200

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
unsigned int commandId = 0;

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
}

void setup() {
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, WIFI_LED_OFF);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, STATUS_LED_OFF);
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

bool waitForButtonPress() {
  for (int i = 0; i < 10; i++) {
    delay(1);
    if (digitalRead(BUTTON_PIN) == BUTTON_PRESSED) {
      return true;
    }
  }

  return false;
}

std::optional<ButtonPress> readButtonPress() {
  // Wait until button not pressed
  while (digitalRead(BUTTON_PIN) == BUTTON_PRESSED) {
    delay(1);
  }

  if (waitForButtonPress()) {
    // Debounce
    delay(DEBOUNCE_DELAY);

    // Determine if long press
    for (int i = 0; i < LONG_PRESS_DELAY; i++) {
      delay(1);
      if (digitalRead(BUTTON_PIN) != BUTTON_PRESSED) {
        return BUTTON_PRESS_SHORT;
      }
    }

    return BUTTON_PRESS_LONG;
  } else {
    return std::nullopt;
  }
}

std::optional<String> readCommandResponse(WiFiClient &client) {
  String expectedPrefix = "{\"id\":";
  expectedPrefix += commandId;
  expectedPrefix += ",";

  while (true) {
    String response = client.readStringUntil('\n');

    if (response.startsWith(expectedPrefix)) {
      return response;
    } else if (response == "") {
      return std::nullopt;
    } else {
      Serial.print("Ignoring response: ");
      Serial.println(response);
    }
  }
}

void sendCommand(WiFiClient &client, const char *method, const char *params) {
  commandId++;

  String command = "{\"id\":";
  command += commandId;
  command += ",\"method\":\"";
  command += method;
  command += "\",\"params\":[";
  command += params;
  command += "]}";

  Serial.print("Sending command: ");
  Serial.println(command);

  client.print(command);
  client.write("\r\n");
}

void flushCommands(WiFiClient &client) {
  client.flush();
}

std::optional<String> command(WiFiClient &client, const char *method, const char *params) {
  sendCommand(client, method, params);
  flushCommands(client);

  if (auto response = readCommandResponse(client)) {
    Serial.print("Received response: ");
    Serial.println(*response);

    return response;
  } else {
    Serial.println("Received no response");

    return std::nullopt;
  }
}

std::optional<bool> parsePowerResponse(String &response) {
  const char *ptr = response.c_str();

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

  // Parse power
  if (strncmp(ptr, "\"on\"", 4) == 0) {
    return true;
  } else if (strncmp(ptr, "\"off\"", 5) == 0) {
    return false;
  } else {
    return std::nullopt;
  }
}

std::optional<bool> getPower(WiFiClient &client) {
  if (auto response = command(client, "get_prop", "\"power\"")) {
    if (auto power = parsePowerResponse(*response)) {
      return power;
    } else {
      Serial.println("Failed to parse power response");
    }
  } else {
    Serial.println("Failed to get power");
  }
}

std::optional<int> parseBrightnessResponse(String &response) {
  const char *ptr = response.c_str();

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

std::optional<int> getBrightness(WiFiClient &client) {
  if (auto response = command(client, "get_prop", "\"bright\"")) {
    if (auto brightness = parseBrightnessResponse(*response)) {
      return brightness;
    } else {
      Serial.println("Failed to parse brightness response");
    }
  } else {
    Serial.println("Failed to get brightness");
  }

  return std::nullopt;
}

void sendTogglePowerCommand(WiFiClient &client) {
  if (!getPower(client).value_or(true)) {
    sendCommand(client, "set_power", "\"on\", \"smooth\", 500");
    command(client, "set_bright", "1, \"smooth\", 500");
  } else {
    command(client, "toggle", "");
  }
}

void sendToggleBrightnessCommand(WiFiClient &client) {
  if (!getPower(client).value_or(true)) {
    sendCommand(client, "set_power", "\"on\", \"smooth\", 500");
    command(client, "set_bright", "100, \"smooth\", 500");
  } else if (auto brightness = getBrightness(client)) {
    command(client, "set_bright", brightness < 50 ? "100, \"smooth\", 500" : "1, \"smooth\", 500");
  }
}

void loop() {
  digitalWrite(STATUS_LED, STATUS_LED_ON);

  Serial.println("Discovering...");
  SocketAddr address = discover();
  Serial.print("Discovered: ");
  Serial.print(address.ip);
  Serial.print(":");
  Serial.println(address.port);

  Serial.println("Connecting...");
  WiFiClient client;
  if (client.connect(address.ip, address.port)) {
    client.setTimeout(1000);
    Serial.println("Connected");
  } else {
    Serial.println("Failed to connect");
    return; // Rediscover and reconnect
  }

  digitalWrite(STATUS_LED, STATUS_LED_OFF);

  while (true) {
    if (!client.connected()) {
      return; // Rediscover and reconnect
    } else if (WiFi.status() != WL_CONNECTED) {
      ESP.restart(); // Restart and reconnect to WiFi
    } else if (auto buttonPress = readButtonPress()) {
      digitalWrite(STATUS_LED, STATUS_LED_ON);

      if (buttonPress == BUTTON_PRESS_SHORT) {
        Serial.println("Sending toggle power command...");
        sendTogglePowerCommand(client);
      } else {
        Serial.println("Sending toggle brightness command...");
        sendToggleBrightnessCommand(client);
      }
      Serial.println("Sent");

      digitalWrite(STATUS_LED, STATUS_LED_OFF);
    }
  }
}
