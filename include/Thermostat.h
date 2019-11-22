#pragma once

#include "Arduino.h"
#include "ArduinoJson.h"
#include "ThreadController.h"
#include "Thread.h"
#include "SPI.h"
#include "TFT_eSPI.h"
#include "IOT.h"
#include "Thermometer.h"

// display coordinates
#define CURRENT_TEMPERATURE_X 20
#define CURRENT_TEMPERATURE_Y 90
#define SET_TEMPERATURE_X 20
#define SET_TEMPERATURE_Y 200
#define ARROW_X 260
#define ARROW_Y 20
#define POWER_LED_X 30
#define POWER_LED_Y 20
#define WIFI_LED_X 50
#define WIFI_LED_Y 10

// Limits
#define DISPLAY_TIMOUT 60
#define DEFAULT_TARGET_TEMPERATURE 21.0

enum Mode
{
    undefined,
    off,
    heat
};

namespace ESPThermostat
{

class Thermostat
{
public:
    Thermostat();
    void Init();
    void Run();
    void Up();
    void Down();
    void setTargetTemperature(float v)
    {
        _targetTemperature = v;
    }
    float getTargetTemperature()
    {
        return _targetTemperature;
    }
    float getCurrentTemperature()
    {
        return _currentTemperature;
    }
    void setMode(Mode v, boolean persist = true); // persist true saves to eeprom
    Mode getMode()
    {
        return _current_mode;
    }

    void runTFT();
    void runHeater();
private:
    void initTFT();
    void showTargetTemperature();
    void actionHeater();
    void wakeScreen();
    float _currentTemperature;
    float _targetTemperature = DEFAULT_TARGET_TEMPERATURE;
    float _lastTargetTemperature = 0;
    float _lastTemperatureReading = 0;
    boolean _heating_element_on = false;
    Mode _requested_mode = undefined;
    Mode _current_mode = undefined;
    boolean _wifi_on = false;
    u_int _display_timer;
    Thermometer _thermometer = Thermometer(THERMISTOR_SENSOR_PIN, THERMISTOR_POWER_PIN, ESP_VOLTAGE_REFERENCE);
    ThreadController _controller = ThreadController();
    Thread *_workerThreadTFT = new Thread();
    Thread *_workerThreadHeat = new Thread();
    TFT_eSPI _tft = TFT_eSPI();
};
} // namespace ESPThermostat
extern ESPThermostat::Thermostat _thermostat;