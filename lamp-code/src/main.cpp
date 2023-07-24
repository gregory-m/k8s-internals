#include <Arduino.h>
#include <WireGuard-ESP32.h>
#include <FastLED.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

#define TZ "IST-2IDT,M3.4.4/26,M10.5.0"

#define NUM_LEDS 6
#define DATA_PIN 16
#define BRIGHTNESS 100

#define RED_LED 17
#define GREEN_LED 18

#define LOCAL_IP "198.51.100.11"
#define PRIVATE_KEY "mKn0OkUKexZva0UQHwwhsfqR5+1SHC1/H8QjA4FUPWM="
#define PUBLIC_KEY "F1BQRFFJ8GyP6Lnt10v7Omx859gpqCp6B85AjtIXcm8="
#define ENDPOINT_ADDRESS "k8s-internals.gregory.beer"
#define ENDPOINT_PORT 3000

WireGuard wg;
WiFiManager wifiManager;
WebServer server(80);

CRGB leds[NUM_LEDS];
uint current_color;

TaskHandle_t TaskHandle_UpdateColor;

void statusLED(void *parameter)
{
  for (;;)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      int state = !digitalRead(RED_LED);
      digitalWrite(RED_LED, state);
      digitalWrite(GREEN_LED, state);
    }
    else if (WiFi.status() == WL_CONNECTED && !wg.is_initialized())
    {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, !digitalRead(RED_LED));
    }
    else if (WiFi.status() == WL_CONNECTED && wg.is_initialized())
    {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void updateColor(void *parameter)
{
  Serial.printf("Updating led color to %06x\n", current_color);
  for (size_t i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = current_color;
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }

  TaskHandle_UpdateColor = NULL;
  vTaskDelete(NULL);
}

void handle_OnConnect()
{
  digitalWrite(GREEN_LED, LOW);
  Serial.println("handle_OnConnect");
  server.send(200, "text/plain", "OK\n");
  digitalWrite(GREEN_LED, HIGH);
}

void handle_OnUpdate()
{
  digitalWrite(GREEN_LED, LOW);

  Serial.println("handle_OnUpdate");

  String post_body = server.arg("color");
  Serial.println(post_body);

  if (post_body.length() != 6)
  {
    server.send(400, "text/plain", "Bad request\n");
    return;
  }

  char *stopstring;
  uint color;

  color = strtoul(post_body.c_str(), &stopstring, 16);
  if (strlen(stopstring) != 0)
  {
    server.send(400, "text/plain", "Bad request\n");
    return;
  }

  current_color = color;
  Serial.printf("Current color: %06x\n", current_color);

  if (TaskHandle_UpdateColor != NULL)
  {
    vTaskDelete(TaskHandle_UpdateColor);
  }
  xTaskCreate(
      updateColor,
      "Updated status LEDs",
      2000,
      NULL,
      1,
      &TaskHandle_UpdateColor);

  server.send(200, "text/plain", "OK\n");

  digitalWrite(GREEN_LED, HIGH);
}

void handle_OnStatus()
{
  digitalWrite(GREEN_LED, LOW);
  Serial.printf("handle_OnStatus %06x\n", current_color);

  char resp[10];
  sprintf(resp, "%06x\n", current_color);
  server.send(200, "text/plain", resp);

  digitalWrite(GREEN_LED, HIGH);
}

void setTimezone(String timezone)
{
  Serial.printf("Setting Timezone to %s\n", timezone.c_str());
  setenv("TZ", timezone.c_str(), 1);
  tzset();
}

void initTime(String timezone)
{
  Serial.println("Setting up time");
  configTime(0, 0, "0.asia.pool.ntp.org", "pool.ntp.org ", "time.google.com");
  Serial.println("Got the time from NTP");

  setTimezone(timezone);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  char current_time[30];
  strftime(current_time, sizeof(current_time), "%A, %B %d %Y %H:%M:%S zone %Z %z", &timeinfo);
  Serial.printf("Current time: %s\n", current_time);
}

void setup()
{
  Serial.begin(115200);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS); // GRB ordering is typical
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setCorrection(TypicalSMD5050);

  xTaskCreate(
      statusLED,
      "Updated status LEDs",
      1000,
      NULL,
      1,
      NULL);

  wifiManager.autoConnect("K8S Internals");

  initTime(TZ);

  Serial.println("Initializing WireGuard...");
  IPAddress local_ip;
  if (!local_ip.fromString(LOCAL_IP))
  {
    Serial.println("UnParsable IP");
    return;
  };

  Serial.printf("Local IP: %s\n", local_ip.toString());
  wg.begin(
      local_ip,
      PRIVATE_KEY,
      ENDPOINT_ADDRESS,
      PUBLIC_KEY,
      ENDPOINT_PORT);

  server.on("/", handle_OnConnect);
  server.on("/update", HTTP_POST, handle_OnUpdate);
  server.on("/status", HTTP_GET, handle_OnStatus);
  server.begin();
  Serial.println("HTTP server started");
}

void loop()
{
  FastLED.show();
  server.handleClient();
}
