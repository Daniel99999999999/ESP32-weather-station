#include "aqi.h"
#include <TFT_eSPI.h>

int calcAqi(float Cp, float Clow, float Chigh, int Ilow, int Ihigh) {
  return (int)((Ihigh - Ilow) * (Cp - Clow) / (Chigh - Clow) + Ilow);
}

int pm25ToAqi(float pm) {
  if (pm <= 12.0)   return calcAqi(pm, 0.0, 12.0, 0, 50);
  if (pm <= 35.4)   return calcAqi(pm, 12.1, 35.4, 51, 100);
  if (pm <= 55.4)   return calcAqi(pm, 35.5, 55.4, 101, 150);
  if (pm <= 150.4)  return calcAqi(pm, 55.5, 150.4, 151, 200);
  if (pm <= 250.4)  return calcAqi(pm, 150.5, 250.4, 201, 300);
  return calcAqi(pm, 250.5, 500.4, 301, 500);
}

int pm10ToAqi(float pm) {
  if (pm <= 54)    return calcAqi(pm, 0, 54, 0, 50);
  if (pm <= 154)   return calcAqi(pm, 55, 154, 51, 100);
  if (pm <= 254)   return calcAqi(pm, 155, 254, 101, 150);
  if (pm <= 354)   return calcAqi(pm, 255, 354, 151, 200);
  if (pm <= 424)   return calcAqi(pm, 355, 424, 201, 300);
  return calcAqi(pm, 425, 604, 301, 500);
}

uint16_t aqiToColor(int aqi) {
  if (aqi <= 50) return TFT_GREEN;
  if (aqi <= 100) return TFT_YELLOW;
  if (aqi <= 150) return TFT_ORANGE;
  if (aqi <= 200) return TFT_RED;
  if (aqi <= 300) return TFT_PURPLE;
  return TFT_MAROON;
}

bool isAqiFresh(time_t t) {
  return (time(nullptr) - t) <= 1800;
}