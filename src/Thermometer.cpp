#include "Thermometer.h"

#ifdef MAX6675
Thermometer::Thermometer(int ktcCLK, int ktcCS, int ktcSO, float offset)
{
	_ktc = MAX6675(ktcCLK, ktcCS, ktcSO);
	_offset = offset;
	_sensorPin = -1;
	_numberOfSummations = 0;
	_rollingSum = 0;
}
#else
Thermometer::Thermometer(int sensorPin, int vRefPin, float voltRef)
{
	_voltRef = voltRef;
	_sensorPin = sensorPin;
	_vRefPin = vRefPin;
	_numberOfSummations = 0;
	_rollingSum = 0;

}
#endif

Thermometer::~Thermometer()
{
}

void Thermometer::Init()
{
	adcAttachPin(_sensorPin);
	pinMode(_vRefPin, OUTPUT);
	digitalWrite(_vRefPin, LOW);
}

double Thermometer::ReadVoltage()
{
	digitalWrite(_vRefPin, HIGH);
	delay(10);
	double reading = analogRead(_sensorPin); // Reference voltage is 3v3 so maximum reading is 3v3 = 4095 in range 0 to 4095
	digitalWrite(_vRefPin, LOW);
	if(reading < 1 || reading > ADC_Resolution) return 0;
	return -0.000000000000016 * pow(reading,4) + 0.000000000118171 * pow(reading,3)- 0.000000301211691 * pow(reading,2)+ 0.001109019271794 * reading + 0.034143524634089;
}

float Thermometer::AddReading(float val)
{
	float currentAvg = 0.0;
	if (_numberOfSummations > 0) {
		currentAvg = _rollingSum / _numberOfSummations;
	}
	if (_numberOfSummations < SAMPLESIZE) {
		_numberOfSummations++;
	}
	else {
		_rollingSum -= currentAvg;
	}
	_rollingSum += val;
	return _rollingSum / _numberOfSummations;
}

#ifdef MAX6675
float Thermometer::Temperature()
{
	float temperature = 0.0;
	temperature = AddReading(_ktc.readCelsius());
	return temperature + _offset;
}
#else
float Thermometer::Temperature()
{
	double voltage = ReadVoltage();
	// Serial.print("Analog reading "); 
	// Serial.println(voltage);
	double average = _voltRef / voltage - 1;
	average = SERIESRESISTOR / average;
	double steinhart;
	steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
	steinhart = log(steinhart);                  // ln(R/Ro)
	steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
	steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
	steinhart = 1.0 / steinhart;                 // Invert
	steinhart -= 273.15;                         // convert to C
	return AddReading(steinhart);
}
#endif

