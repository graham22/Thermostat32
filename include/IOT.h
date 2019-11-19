#pragma once

#include "WiFi.h"
#include "ArduinoJson.h"
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include "AsyncMqttClient.h"
#include "IotWebConf.h"

#define CONFIG_VERSION "V1.3"
#define HOME_ASSISTANT_PREFIX "homeassistant" // MQTT prefix used in autodiscovery
#define STR_LEN 64                            // general string buffer size
#define CONFIG_LEN 32                         // configuration string buffer size

#define WIFI_STATUS_PIN 17 //First it will light up (kept LOW), on Wifi connection it will blink, when connected to the Wifi it will turn off (kept HIGH).
#define WIFI_AP_PIN 16     // pull down to force WIFI AP mode
#define AP_TIMEOUT 30000

const char S_JSON_COMMAND_NVALUE[] PROGMEM = "{\"%s\":%d}";
const char S_JSON_COMMAND_LVALUE[] PROGMEM = "{\"%s\":%lu}";
const char S_JSON_COMMAND_SVALUE[] PROGMEM = "{\"%s\":\"%s\"}";
const char S_JSON_COMMAND_HVALUE[] PROGMEM = "{\"%s\":\"#%X\"}";

namespace ESPThermostat
{
class IOT
{
public:
    IOT();
    void Init();
    void Run();
    void publish(const char *subtopic, const char *value, boolean retained = false);
    void publish(const char *topic, const char *subtopic, float value, boolean retained = false);
    void publishDiscovery();
private:
};


} // namespace ESPThermostat

extern ESPThermostat::IOT _iot;