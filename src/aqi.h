#pragma once
#include <Arduino.h>

int calcAqi(float Cp, float Clow, float Chigh, int Ilow, int Ihigh);

int pm25ToAqi(float pm);
int pm10ToAqi(float pm);

uint16_t aqiToColor(int aqi);

bool isAqiFresh(time_t t);