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
#include "Thermostat.h"

#define CONFIG_VERSION "V1.3"
#define HOME_ASSISTANT_PREFIX "homeassistant" // MQTT prefix used in autodiscovery
#define STR_LEN 64                            // general string buffer size
#define CONFIG_LEN 32                         // configuration string buffer size

#define WIFI_STATUS_PIN 17 //First it will light up (kept LOW), on Wifi connection it will blink, when connected to the Wifi it will turn off (kept HIGH).
#define WIFI_AP_PIN 16     // pull down to force WIFI AP mode
#define AP_TIMEOUT 30000

namespace ESPThermostat
{
class IOT
{
public:
    IOT();
    void Init();
    void Run();
    void SaveMode(int m);
    void SaveTargetTemperature(float t);
    void publish(const char *subtopic, const char *value, boolean retained = false);
    void publish(const char *topic, const char *subtopic, float value, boolean retained = false);
    void publishDiscovery();
private:
};


} // namespace ESPThermostat

extern ESPThermostat::IOT _iot;