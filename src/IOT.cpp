#include "IOT.h"
#include "Log.h"

ESPThermostat::IOT _iot = ESPThermostat::IOT();

namespace ESPThermostat
{
AsyncMqttClient _mqttClient;
TimerHandle_t mqttReconnectTimer;
DNSServer _dnsServer;
WebServer _webServer(80);
HTTPUpdateServer _httpUpdater;
IotWebConf _iotWebConf(TAG, &_dnsServer, &_webServer, TAG, CONFIG_VERSION);
char _mqttServer[IOTWEBCONF_WORD_LEN];
char _mqttPort[5];
char _mqttUserName[IOTWEBCONF_WORD_LEN];
char _mqttUserPassword[IOTWEBCONF_WORD_LEN];
char _mqttTemperatureCmndTopic[STR_LEN];
char _mqttModeCmndTopic[STR_LEN];
char _mqttRootTopic[STR_LEN];
char _willTopic[STR_LEN];
char _savedTargetTemperature[5];
char _savedMode[5];
u_int _uniqueId = 0;
IotWebConfSeparator seperatorParam = IotWebConfSeparator("MQTT");
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", _mqttServer, IOTWEBCONF_WORD_LEN);
IotWebConfParameter mqttPortParam = IotWebConfParameter("MQTT port", "mqttSPort", _mqttPort, 5, "text", NULL, "8080");
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", _mqttUserName, IOTWEBCONF_WORD_LEN);
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", _mqttUserPassword, IOTWEBCONF_WORD_LEN, "password");
IotWebConfParameter savedTargetTemperatureParam = IotWebConfParameter("Target temperature", "savedTargetTemperature", _savedTargetTemperature, 5, "number", NULL, NULL, "min='10' max='27'", false);
IotWebConfParameter savedModeParam = IotWebConfParameter("Current Mode", "savedMode", _savedMode, 5, "number", NULL, NULL, NULL, false);

void IOT::publishDiscovery()
{
	char buffer[STR_LEN];
	StaticJsonDocument<1024> doc; // MQTT discovery
	doc["name"] = _iotWebConf.getThingName();
	sprintf(buffer, "%X", _uniqueId);
	doc["unique_id"] = buffer;
	doc["mode_cmd_t"] = "~/cmnd/MODE";
	doc["mode_stat_t"] = "~/stat/MODE";
	doc["avty_t"] = "~/tele/LWT";
	doc["pl_avail"] = "Online";
	doc["pl_not_avail"] = "Offline";
	doc["temp_cmd_t"] = "~/cmnd/TEMPERATURE";
	doc["temp_stat_t"] = "~/stat/TEMPERATURE";
	doc["temp_stat_tpl"] = "{{value_json.SET_TEMPERATURE}}";
	doc["curr_temp_t"] = "~/stat/TEMPERATURE";
	doc["curr_temp_tpl"] = "{{value_json.CURRENT_TEMPERATURE}}";
	doc["action_topic"] = "~/stat/ACTION";
	doc["min_temp"] = MIN_TEMPERATURE;
	doc["max_temp"] = MAX_TEMPERATURE;
	doc["temp_step"] = TEMP_PRECISION;
	JsonArray array = doc.createNestedArray("modes");
	array.add("off");
	array.add("heat");
	JsonObject device = doc.createNestedObject("device");
	device["name"] = _iotWebConf.getThingName();
	device["sw_version"] = CONFIG_VERSION;
	device["manufacturer"] = "SkyeTracker";
	sprintf(buffer, "ESP32-Bit (%X)", _uniqueId);
	device["model"] = buffer;
	JsonArray identifiers = device.createNestedArray("identifiers");
	identifiers.add(_uniqueId);
	doc["~"] = _mqttRootTopic;
	String s;
	serializeJson(doc, s);
	char configurationTopic[64];
	sprintf(configurationTopic, "%s/climate/%X/config", HOME_ASSISTANT_PREFIX, _uniqueId);
	if (_mqttClient.publish(configurationTopic, 0, true, s.c_str(), s.length()) == 0)
	{
		loge("**** Configuration payload exceeds MAX MQTT Packet Size");
	}
}

void onMqttConnect(bool sessionPresent)
{
	logi("Connected to MQTT. Session present: %d", sessionPresent);
	_mqttClient.subscribe(_mqttModeCmndTopic, 1);
	_mqttClient.subscribe(_mqttTemperatureCmndTopic, 1);
	_iot.publishDiscovery();
	_mqttClient.publish(_willTopic, 0, false, "Online");
	_iot.publish("MODE", _thermostat.getMode() == heat ? "heat" : "off");
	_iot.publish("TEMPERATURE", "CURRENT_TEMPERATURE", _thermostat.getCurrentTemperature());
	_iot.publish("TEMPERATURE", "SET_TEMPERATURE", _thermostat.getTargetTemperature(), true);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
	logi("Disconnected from MQTT. Reason: %d", (int8_t)reason);
	if (WiFi.isConnected())
	{
		xTimerStart(mqttReconnectTimer, 0);
	}
}

void connectToMqtt()
{
	logi("Connecting to MQTT...");
	if (WiFi.isConnected())
	{
		_mqttClient.connect();
	}
}

void WiFiEvent(WiFiEvent_t event)
{
	logi("[WiFi-event] event: %d", event);
	switch (event)
	{
	case SYSTEM_EVENT_STA_GOT_IP:
		logi("WiFi connected, IP address: %s", WiFi.localIP().toString().c_str());
		xTimerStart(mqttReconnectTimer, 0);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		logi("WiFi lost connection");
		xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		break;
	default:
		break;
	}
}

void onMqttPublish(uint16_t packetId)
{
	logi("Publish acknowledged.  packetId: %d", packetId);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
	logd("MQTT Message arrived [%s]  qos: %d len: %d index: %d total: %d", topic, properties.qos, len, index, total);
	printHexString(payload, len);
	if (strcmp(_mqttTemperatureCmndTopic, topic) == 0)
	{
		if (strncmp(payload, "UP", len) == 0)
		{
			_thermostat.Up();
		}
		else if (strncmp(payload, "DOWN", len) == 0)
		{
			_thermostat.Down();
		}
		else
		{
			String inString = payload;
			float target = inString.toFloat();
			if (target < MIN_TEMPERATURE)
			{
				_thermostat.setTargetTemperature(MIN_TEMPERATURE);
			}
			else if (target > MAX_TEMPERATURE)
			{
				_thermostat.setTargetTemperature(MAX_TEMPERATURE);
			}
			else
			{
				_thermostat.setTargetTemperature(target);
			}
		}
	}
	else if (strcmp(_mqttModeCmndTopic, topic) == 0)
	{
		if (strncmp(payload, "heat", len) == 0)
		{
			_thermostat.setMode(heat);
		}
		else if (strncmp(payload, "off", len) == 0)
		{
			_thermostat.setMode(off);
		}
	}

}

IOT::IOT()
{
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
	s += "<title>ESPThermostat</title></head><body>";
	s += _iotWebConf.getThingName();
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

void configSaved()
{
	Serial.println("Configuration was updated.");
}

boolean formValidator()
{
	boolean valid = true;
	int l = _webServer.arg(mqttServerParam.getId()).length();
	if (l < 3)
	{
		mqttServerParam.errorMessage = "Please provide at least 3 characters!";
		valid = false;
	}
	return valid;
}

void IOT::Init()
{
	pinMode(WIFI_AP_PIN, INPUT_PULLUP);
	pinMode(WIFI_STATUS_PIN, OUTPUT);
	mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
	WiFi.onEvent(WiFiEvent);
	_iotWebConf.setStatusPin(WIFI_STATUS_PIN);
	_iotWebConf.setConfigPin(WIFI_AP_PIN);
	// setup EEPROM parameters
	_iotWebConf.addParameter(&savedTargetTemperatureParam);
	_iotWebConf.addParameter(&savedModeParam);
	_iotWebConf.addParameter(&seperatorParam);
	_iotWebConf.addParameter(&mqttServerParam);
	_iotWebConf.addParameter(&mqttPortParam);
	_iotWebConf.addParameter(&mqttUserNameParam);
	_iotWebConf.addParameter(&mqttUserPasswordParam);
	// setup callbacks for IotWebConf
	_iotWebConf.setConfigSavedCallback(&configSaved);
	_iotWebConf.setFormValidator(&formValidator);
	_iotWebConf.setupUpdateServer(&_httpUpdater);
	boolean validConfig = _iotWebConf.init();
	if (!validConfig)
	{
		loge("!invalid configuration!");
		_mqttServer[0] = '\0';
		_mqttPort[0] = '\0';
		_mqttUserName[0] = '\0';
		_mqttUserPassword[0] = '\0';
		dtostrf(DEFAULT_TARGET_TEMPERATURE, 2, 1, _savedTargetTemperature);
		_thermostat.setMode(off, false);
		_thermostat.setTargetTemperature(DEFAULT_TARGET_TEMPERATURE);
		ltoa(off, _savedMode, 10);
	}
	else
	{
		_iotWebConf.setApTimeoutMs(AP_TIMEOUT);
		_thermostat.setTargetTemperature(atof(_savedTargetTemperature));
		_thermostat.setMode((Mode)atoi(_savedMode), false);
	}
	// Set up required URL handlers on the web server.
	_webServer.on("/", handleRoot);
	_webServer.on("/config", [] { _iotWebConf.handleConfig(); });
	_webServer.onNotFound([]() { _iotWebConf.handleNotFound(); });
	_mqttClient.onConnect(onMqttConnect);
	_mqttClient.onDisconnect(onMqttDisconnect);
	_mqttClient.onMessage(onMqttMessage);
	_mqttClient.onPublish(onMqttPublish);
	// generate unique id from mac address NIC segment
	uint8_t chipid[6];
	esp_efuse_mac_get_default(chipid);
	_uniqueId = chipid[3] << 16;
	_uniqueId += chipid[4] << 8;
	_uniqueId += chipid[5];
	sprintf(_mqttRootTopic, "%s/%X/climate", _iotWebConf.getThingName(), _uniqueId);
	sprintf(_mqttTemperatureCmndTopic, "%s/cmnd/TEMPERATURE", _mqttRootTopic);
	sprintf(_mqttModeCmndTopic, "%s/cmnd/MODE", _mqttRootTopic);
	IPAddress ip;
	if (ip.fromString(_mqttServer))
	{
		int port = atoi(_mqttPort);
		_mqttClient.setServer(ip, port);
		_mqttClient.setCredentials(_mqttUserName, _mqttUserPassword);
		sprintf(_willTopic, "%s/tele/LWT", _mqttRootTopic);
		_mqttClient.setWill(_willTopic, 0, false, "Offline");
	}
}

void IOT::Run()
{
	_iotWebConf.doLoop();
}

void IOT::SaveMode(int m)
{
	ltoa(m, _savedMode, 10);
	_iotWebConf.configSave();
}

void IOT::SaveTargetTemperature(float t)
{
	dtostrf(t, 2, 1, _savedTargetTemperature);
	_iotWebConf.configSave();
}

void IOT::publish(const char *subtopic, const char *value, boolean retained)
{
	if (_mqttClient.connected())
	{
		char buf[64];
		sprintf(buf, "%s/stat/%s", _mqttRootTopic, subtopic);
		_mqttClient.publish(buf, 0, retained, value);
	}
}

void IOT::publish(const char *topic, const char *subtopic, float value, boolean retained)
{
	char str_temp[6];
	dtostrf(value, 2, 1, str_temp);
	char buf[256];
	snprintf_P(buf, sizeof(buf), "{\"%s\":\"%s\"}", subtopic, str_temp);
	publish(topic, buf, retained);
}

} // namespace ESPThermostat