#include "Arduino.h"
#include "SPI.h"
#include "Thermostat.h"
#include "log.h"
#include "IOT.h"

using namespace ESPThermostat;

void setup(void)
{
	Serial.begin(115200);
	while (!Serial)
	{
		; // wait for serial port to connect. Needed for native USB port only
	}
	_thermostat.Init();
	_iot.Init();
}

void loop(void)
{
	_thermostat.Run();
	_iot.Run();
}