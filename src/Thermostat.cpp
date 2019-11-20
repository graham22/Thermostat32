#include "Arduino.h"
#include "WiFi.h"
#include "Thermostat.h"
#include "Log.h"

ESPThermostat::Thermostat _thermostat = ESPThermostat::Thermostat();

const uint8_t wifi_Symbol[33] PROGMEM = { // WIFI symbol
    0x00, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0x1C, 0x38,
    0x07, 0x60, 0xE1, 0xC7, 0x78, 0x1E, 0x0C, 0x30,
    0x80, 0x01, 0xE0, 0x07, 0x30, 0x0C, 0x00, 0x00,
    0x80, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00};

namespace ESPThermostat
{

#define WATCHDOG_TIMER 60000 //time in ms to trigger the watchdog

hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule()
{
    ets_printf("watchdog timer expired - rebooting\n");
    esp_restart();
}

void init_watchdog()
{
    if (timer == NULL)
    {
        timer = timerBegin(0, 80, true);                      //timer 0, div 80
        timerAttachInterrupt(timer, &resetModule, true);      //attach callback
        timerAlarmWrite(timer, WATCHDOG_TIMER * 1000, false); //set time in us
        timerAlarmEnable(timer);                              //enable interrupt
    }
}

void feed_watchdog()
{
    if (timer != NULL)
    {
        timerWrite(timer, 0); // feed the watchdog
    }
}

void _runTFT()
{
    _thermostat.runTFT();
}

void _runHeater()
{
    _thermostat.runHeater();
}

Thermostat::Thermostat()
{
}

void Thermostat::Init()
{
    pinMode(KCT_CS, OUTPUT); // disable MAX6675 if installed, using Thermistor
    digitalWrite(KCT_CS, HIGH);
    initTFT();
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);
    _current_mode = undefined;
    _thermometer.Init();
    _workerThreadTFT->onRun(_runTFT);
    _workerThreadTFT->setInterval(250);
    _controller.add(_workerThreadTFT);
    _workerThreadHeat->onRun(_runHeater);
    _workerThreadHeat->setInterval(1000);
    _controller.add(_workerThreadHeat);
    showTargetTemperature();
    init_watchdog();
}

void Thermostat::Run()
{
    _controller.run();
}

void Thermostat::Up()
{
    if (_targetTemperature < MAX_TEMPERATURE)
    {
        _targetTemperature += TEMP_PRECISION;
        showTargetTemperature();
    }
}

void Thermostat::Down()
{
    if (_targetTemperature > MIN_TEMPERATURE)
    {
        _targetTemperature -= TEMP_PRECISION;
        showTargetTemperature();
    }
}

void Thermostat::initTFT()
{ // Initialise the TFT screen
    _tft.init();
    _tft.setRotation(1); // Set the rotation before we calibrate
    uint16_t calData[5] = {452, 3097, 460, 3107, 4};
    _tft.setTouch(calData);
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, HIGH);
    _display_timer = DISPLAY_TIMOUT;
    _tft.fillScreen(TFT_BLACK);
    int xpos = ARROW_X;
    int ypos = ARROW_Y;
    _tft.fillTriangle(
        xpos, ypos,           // peak
        xpos - 40, ypos + 60, // bottom left
        xpos + 40, ypos + 60, // bottom right
        TFT_RED);
    ypos += 200;
    _tft.fillTriangle(
        xpos, ypos,           // peak
        xpos - 40, ypos - 60, // bottom left
        xpos + 40, ypos - 60, // bottom right
        TFT_BLUE);
}

void Thermostat::showTargetTemperature()
{
    _tft.setTextSize(4);
    _tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    if (_current_mode == heat)
    {
        _tft.drawFloat(_targetTemperature, 1, SET_TEMPERATURE_X, SET_TEMPERATURE_Y);
        _iot.publish("TEMPERATURE", "SET_TEMPERATURE", _targetTemperature, true);
    }
    else
    {
        _tft.drawString("Off   ", SET_TEMPERATURE_X, SET_TEMPERATURE_Y);
    }
    wakeScreen();
}

void Thermostat::wakeScreen()
{
    digitalWrite(TFT_LED_PIN, HIGH);
    _display_timer = DISPLAY_TIMOUT;
}

void Thermostat::runTFT()
{
    uint16_t x = 0, y = 0;
    if (_tft.getTouch(&x, &y))
    {
        if (_display_timer > 0)
        {
            if (x > 200)
            {
                if (_current_mode == heat)
                {
                    if (y < 120)
                    {
                        Up();
                    }
                    else
                    {
                        Down();
                    }
                }
            }
            else if (y > 200)
            {
                setMode(_current_mode == heat ? off : heat); // toggle on/off
            }
        }
        wakeScreen();
    }
    if (_wifi_on == false && WiFi.isConnected())
    {
        _wifi_on = true;
        _tft.drawXBitmap(WIFI_LED_X, WIFI_LED_Y, wifi_Symbol, 16, 16, TFT_GREEN);
    }
    else if (_wifi_on == true && !WiFi.isConnected())
    {
        _wifi_on = false;
        _tft.drawXBitmap(WIFI_LED_X, WIFI_LED_Y, wifi_Symbol, 16, 16, TFT_BLACK);
    }
}

void Thermostat::actionHeater()
{
    logd("_heating_element_on: %d", _heating_element_on);

    if (_heating_element_on)
    {
            digitalWrite(SSR_PIN, HIGH);
        _tft.fillCircle(POWER_LED_X, POWER_LED_Y, 10, TFT_RED);
        _iot.publish("ACTION", "heating");
    }
    else
    {
        digitalWrite(SSR_PIN, LOW);
        _tft.fillCircle(POWER_LED_X, POWER_LED_Y, 10, TFT_BLACK);
        _iot.publish("ACTION", _current_mode == heat ? "idle" : "off");
    }
}

void Thermostat::runHeater()
{
    _currentTemperature = _thermometer.Temperature();
    _tft.setTextSize(8);
    _tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft.drawFloat(_currentTemperature, 1, CURRENT_TEMPERATURE_X, CURRENT_TEMPERATURE_Y);
    if (_current_mode != _requested_mode)
    {
        _current_mode = _requested_mode;
        _iot.publish("MODE", _current_mode == heat ? "heat" : "off");
        _heating_element_on = false;
        actionHeater();
        showTargetTemperature();
    }
    if (_current_mode == heat)
    {
        if (_targetTemperature > _currentTemperature)
        {
            if (_heating_element_on == false)
            {
                _heating_element_on = true;
                actionHeater();
            }
        }
        else
        {
            if (_heating_element_on)
            {
                _heating_element_on = false;
                actionHeater();
            }
        }
    }
    float currentTemperature = _thermometer.Temperature();
    currentTemperature = roundf(currentTemperature * 10.0f) / 10.0f; // round to one decimal place
    if (abs(_lastTemperatureReading - currentTemperature) > 0.2)     // publish changes greater than .2 degrees in temperature
    {
        _iot.publish("TEMPERATURE", "CURRENT_TEMPERATURE", currentTemperature);
        _lastTemperatureReading = currentTemperature;
    }
    if (_lastTargetTemperature != _targetTemperature) // save new target temperature into EEPROM if changed
    {
        _lastTargetTemperature = _targetTemperature;
        _iot.SaveTargetTemperature(_targetTemperature);
        showTargetTemperature();
    }
    // screen saver timer
    if (_display_timer == 0)
    {
        digitalWrite(TFT_LED_PIN, LOW);
    }
    else
    {
        _display_timer--;
    }
    feed_watchdog();
}

void Thermostat::setMode(Mode v, boolean persist) // persist true saves to eeprom
{
    _requested_mode = v;
    if (persist)
    {
        _iot.SaveMode(v);
    }
}
}