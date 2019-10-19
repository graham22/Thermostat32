#include <Arduino.h>
#include <ThreadController.h>
#include <Thread.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <IotWebConf.h>
#include <PubSubClient.h>
#include <Thermometer.h>

// PIN assignments
#define THERMISTOR_SENSOR_PIN 35
#define THERMISTOR_POWER_PIN 25
#define SSR_PIN 5
#define TFT_LED_PIN 32
#define WIFI_STATUS_PIN 17 //First it will light up (kept LOW), on Wifi connection it will blink, when connected to the Wifi it will turn off (kept HIGH).
#define WIFI_AP_PIN 16	 // pull down to force WIFI AP mode

// display coordinates
#define CURRENT_TEMPERATURE_X 20
#define CURRENT_TEMPERATURE_Y 90
#define SET_TEMPERATURE_X 20
#define SET_TEMPERATURE_Y 200
#define ARROW_X 260
#define ARROW_Y 20
#define POWER_LED_X 30
#define POWER_LED_Y 20
#define WIFI_LED_X 300
#define WIFI_LED_Y 10

// Limits
#define MAX_TEMPERATURE 27
#define MIN_TEMPERATURE 10
#define DISPLAY_TIMOUT 60
#define STR_LEN IOTWEBCONF_WORD_LEN
#define AP_TIMEOUT 10000
#define WATCHDOG_TIMER 10000 //time in ms to trigger the watchdog

//WIFI
// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "V1"
// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char _thingName[] = "ESPThermostat";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char _wifiInitialApPassword[] = "ESPThermostat";

DNSServer _dnsServer;
WebServer _webServer(80);
HTTPUpdateServer _httpUpdater;
IotWebConf _iotWebConf(_thingName, &_dnsServer, &_webServer, _wifiInitialApPassword, CONFIG_VERSION);

char _mqttServer[STR_LEN];
char _mqttPort[5];
char _mqttUserName[STR_LEN];
char _mqttUserPassword[STR_LEN];
char _mqttRootTopic[STR_LEN];

IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", _mqttServer, STR_LEN);
IotWebConfParameter mqttPortParam = IotWebConfParameter("MQTT port", "mqttSPort", _mqttPort, 5, "text", NULL, "8080");
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", _mqttUserName, STR_LEN);
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", _mqttUserPassword, STR_LEN, "password");
IotWebConfParameter mqttRootTopicParam = IotWebConfParameter("MQTT root topic", "mqttSRootTopic", _mqttRootTopic, STR_LEN);

hw_timer_t *timer = NULL;
ThreadController _controller = ThreadController();
Thread *_workerThreadTFT = new Thread();
Thread *_workerThreadHeat = new Thread();
Thread *_workerThreadMQTT = new Thread();
Thread *_workerThreadPublish = new Thread();
WiFiClient _EspClient;
PubSubClient _MqttClient(_EspClient);
TFT_eSPI _tft = TFT_eSPI();
float _targetTemperature = 21.5;
Thermometer _thermometer(THERMISTOR_SENSOR_PIN, THERMISTOR_POWER_PIN, 3.38);
boolean _power_on = false;
boolean _wifi_on = false;
bool _setTemperatureChanged = false;
u_int _display_timer;
const char S_JSON_COMMAND_NVALUE[] PROGMEM = "{\"%s\":%d}";
const char S_JSON_COMMAND_LVALUE[] PROGMEM = "{\"%s\":%lu}";
const char S_JSON_COMMAND_SVALUE[] PROGMEM = "{\"%s\":\"%s\"}";
const char S_JSON_COMMAND_HVALUE[] PROGMEM = "{\"%s\":\"#%X\"}";

const uint8_t wifi_Symbol[33] PROGMEM={ // WIFI symbol
  	0x00, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0x1C, 0x38,
	0x07, 0x60, 0xE1, 0xC7, 0x78, 0x1E, 0x0C, 0x30, 
	0x80, 0x01, 0xE0, 0x07, 0x30, 0x0C, 0x00, 0x00, 
  	0x80, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00 };

void IRAM_ATTR resetModule()
{
	ets_printf("watchdog timer expired - rebooting\n");
	esp_restart();
}

void init_watchdog()
{
	if (timer == NULL)
	{
		timer = timerBegin(0, 80, true);					  //timer 0, div 80
		timerAttachInterrupt(timer, &resetModule, true);	  //attach callback
		timerAlarmWrite(timer, WATCHDOG_TIMER * 1000, false); //set time in us
		timerAlarmEnable(timer);							  //enable interrupt
		Serial.println("watchdog timer initialized");
	}
}

void feed_watchdog()
{
	if (timer != NULL)
	{
		timerWrite(timer, 0); // feed the watchdog
	}
}

void initTFT()
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
		xpos, ypos,			  // peak
		xpos - 40, ypos + 60, // bottom left
		xpos + 40, ypos + 60, // bottom right
		TFT_RED);
	ypos += 200;
	_tft.fillTriangle(
		xpos, ypos,			  // peak
		xpos - 40, ypos - 60, // bottom left
		xpos + 40, ypos - 60, // bottom right
		TFT_BLUE);
}

void showTargetTemperature()
{
	_tft.setTextSize(4);
	_tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
	_tft.drawFloat(_targetTemperature, 1, SET_TEMPERATURE_X, SET_TEMPERATURE_Y);
}

void up()
{
	if (_targetTemperature < MAX_TEMPERATURE)
	{
		_targetTemperature += 0.5;
		showTargetTemperature();
		_setTemperatureChanged = true;
	}
}

void down()
{
	if (_targetTemperature > MIN_TEMPERATURE)
	{
		_targetTemperature -= 0.5;
		showTargetTemperature();
		_setTemperatureChanged = true;
	}
}

void wakeScreen()
{
	digitalWrite(TFT_LED_PIN, HIGH);
	_display_timer = DISPLAY_TIMOUT;
}

void runTFT()
{
	uint16_t x = 0, y = 0;
	if (_tft.getTouch(&x, &y))
	{
		if (_display_timer > 0)
		{
			if (x > 200)
			{
				if (y < 120)
				{
					up();
				}
				else
				{
					down();
				}
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

void trimSlashes(const char *input, char *result)
{
	int i, j = 0;
	for (i = 0; input[i] != '\0'; i++)
	{
		if (input[i] != '/' && input[i] != '\\')
		{
			result[j++] = input[i];
		}
	}
	result[j] = '\0';
}

void publish(const char *topic, const char *value, boolean retained = false)
{
	if (_MqttClient.connected())
	{
		char buf[64];
		sprintf(buf, "/%s/stat/%s", _mqttRootTopic, topic);
		_MqttClient.publish(buf, value, retained);
		Serial.print("Topic: ");
		Serial.print(buf);
		Serial.print(" Data: ");
		Serial.println(value);
	}
}

void publish(const char *topic, const char *subtopic, float value, boolean retained = false)
{
	char str_temp[6];
	dtostrf(value, 2, 1, str_temp);
	char buf[256];
	snprintf_P(buf, sizeof(buf), S_JSON_COMMAND_SVALUE, subtopic, str_temp);
	publish(topic, buf);
}

void runHeater()
{
	float deg = _thermometer.Temperature();
	_tft.setTextSize(8);
	_tft.setTextColor(TFT_YELLOW, TFT_BLACK);
	_tft.drawFloat(deg, 1, CURRENT_TEMPERATURE_X, CURRENT_TEMPERATURE_Y);
	if (_targetTemperature > deg)
	{
		if (_power_on == false)
		{
			_power_on = true;
			digitalWrite(SSR_PIN, HIGH);
			_tft.fillCircle(POWER_LED_X, POWER_LED_Y, 10, TFT_RED);
			publish("HEAT", "ON");
		}
	}
	else
	{
		if (_power_on)
		{
			_power_on = false;
			digitalWrite(SSR_PIN, LOW);
			_tft.fillCircle(POWER_LED_X, POWER_LED_Y, 10, TFT_BLACK);
			publish("HEAT", "OFF");
		}
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

void MQTT_callback(char *topic, byte *payload, unsigned int data_len)
{
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (unsigned int i = 0; i < data_len; i++)
	{
		Serial.print((char)payload[i]);
	}
	Serial.println();

	char *data = (char *)payload;
	if (strncmp(data, "UP", data_len) == 0)
	{
		up();
	}
	else if (strncmp(data, "DOWN", data_len) == 0)
	{
		down();
	}
	else
	{
		String inString = data;
		float target = inString.toFloat();
		if (target < MIN_TEMPERATURE)
		{
			_targetTemperature = MIN_TEMPERATURE;
		}
		else if (target > MAX_TEMPERATURE)
		{
			_targetTemperature = MAX_TEMPERATURE;
		}
		else
		{
			_targetTemperature = target;
		}
		showTargetTemperature();
		_setTemperatureChanged = true;
	}
	wakeScreen();
}

void runMQTT()
{
	if (!_MqttClient.connected())
	{
		int port = atoi(_mqttPort);
		_MqttClient.setServer(_mqttServer, port);
		if (_MqttClient.connect("ESP_Thermostat", _mqttUserName, _mqttUserPassword))
		{
			Serial.println("MQTT connected");
			publish("DEVICE", "ONLINE");
			char buf[STR_LEN * 2];
			sprintf(buf, "/%s/cmnd/TEMPERATURE", _mqttRootTopic);
			_MqttClient.subscribe(buf);
			_MqttClient.setCallback(MQTT_callback);
			publish("POWER", _power_on ? "ON" : "OFF");
			publish("TEMPERATURE", "CURRENT_TEMPERATURE", _thermometer.Temperature());
			publish("TEMPERATURE", "SET_TEMPERATURE", _targetTemperature, true);
		}
		else
		{
			Serial.println("Failed to connect to MQTT");
		}
	}
	else
	{
		if (_setTemperatureChanged)
		{
			_setTemperatureChanged = false;
			publish("TEMPERATURE", "SET_TEMPERATURE", _targetTemperature, true);
		}
	}
}

void publishTemperature()
{
	if (_MqttClient.connected())
	{
		publish("TEMPERATURE", "CURRENT_TEMPERATURE", _thermometer.Temperature());
	}
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
	// -- Let IotWebConf test and handle captive portal requests.
	if (_iotWebConf.handleCaptivePortal())
	{
		// -- Captive portal request were already served.
		return;
	}
	String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
	s += "<title>ESPThermostat</title></head><body>ESPThermostat";
	s += "<ul>";
	s += "<li>MQTT server: ";
	s += _mqttServer;
	s += "</ul>";
	s += "<ul>";
	s += "<li>MQTT port: ";
	s += _mqttPort;
	s += "</ul>";
	s += "<ul>";
	s += "<li>MQTT user: ";
	s += _mqttUserName;
	s += "</ul>";
	s += "<ul>";
	s += "<li>MQTT root topic: ";
	s += _mqttRootTopic;
	s += "</ul>";
	s += "Go to <a href='config'>configure page</a> to change values.";
	s += "</body></html>\n";

	_webServer.send(200, "text/html", s);
}

void wifiConnected()
{
	_workerThreadMQTT->onRun(runMQTT);
	_workerThreadMQTT->setInterval(5000);
	_controller.add(_workerThreadMQTT);
	_workerThreadPublish->onRun(publishTemperature);
	_workerThreadPublish->setInterval(60000);
	_controller.add(_workerThreadPublish);
	init_watchdog();
}

void configSaved()
{
	Serial.println("Configuration was updated.");
	char buf1[64];
	trimSlashes(_mqttRootTopic, buf1);
	strcpy(_mqttRootTopic, buf1);
	//   needReset = true;
}

boolean formValidator()
{
	Serial.println("Validating form.");
	boolean valid = true;

	int l = _webServer.arg(mqttServerParam.getId()).length();
	if (l < 3)
	{
		mqttServerParam.errorMessage = "Please provide at least 3 characters!";
		valid = false;
	}
	return valid;
}

void setup(void)
{
	Serial.begin(115200);
	while (!Serial)
	{
		; // wait for serial port to connect. Needed for native USB port only
	}
	Serial.println();
	Serial.println("Booting");
	pinMode(WIFI_AP_PIN, INPUT_PULLUP);
	pinMode(WIFI_STATUS_PIN, OUTPUT);

	initTFT();
	pinMode(SSR_PIN, OUTPUT);
	digitalWrite(SSR_PIN, HIGH);

	_iotWebConf.setStatusPin(WIFI_STATUS_PIN);
	_iotWebConf.setConfigPin(WIFI_AP_PIN);
	_iotWebConf.addParameter(&mqttServerParam);
	_iotWebConf.addParameter(&mqttPortParam);
	_iotWebConf.addParameter(&mqttUserNameParam);
	_iotWebConf.addParameter(&mqttUserPasswordParam);
	_iotWebConf.addParameter(&mqttRootTopicParam);
	_iotWebConf.setConfigSavedCallback(&configSaved);
	_iotWebConf.setFormValidator(&formValidator);
	_iotWebConf.setWifiConnectionCallback(&wifiConnected);
	_iotWebConf.setupUpdateServer(&_httpUpdater);

	boolean validConfig = _iotWebConf.init();
	if (!validConfig)
	{
		Serial.println("!invalid configuration!");
		_mqttServer[0] = '\0';
		_mqttPort[0] = '\0';
		_mqttUserName[0] = '\0';
		_mqttUserPassword[0] = '\0';
		_mqttRootTopic[0] = '\0';
	}
	else
	{
		_iotWebConf.setApTimeoutMs(AP_TIMEOUT);
	}

	// -- Set up required URL handlers on the web server.
	_webServer.on("/", handleRoot);
	_webServer.on("/config", [] { _iotWebConf.handleConfig(); });
	_webServer.onNotFound([]() { _iotWebConf.handleNotFound(); });

	_thermometer.Init();
	_targetTemperature = 21.5;
	_workerThreadTFT->onRun(runTFT);
	_workerThreadTFT->setInterval(250);
	_controller.add(_workerThreadTFT);
	_workerThreadHeat->onRun(runHeater);
	_workerThreadHeat->setInterval(1000);
	_controller.add(_workerThreadHeat);
	showTargetTemperature();
}

void loop(void)
{
	_iotWebConf.doLoop();
	_controller.run();
	if (WiFi.isConnected() && _MqttClient.connected())
	{
		_MqttClient.loop();
	}
}