ARCHITECTURE.md

Cel
Ten dokument opisuje aktualną architekturę firmware rescue / serwisowego dla stacji pogodowej ESP32 z TFT i GT911.

To jest minimalny, stabilny build serwisowy, którego celem jest:

- pewny boot bez resetów
- szybkie uruchomienie TFT i podświetlenia
- działające Wi-Fi
- działające OTA
- działające NTP
- prosty UI diagnostyczny
- podstawowy test dotyku

To nie jest pełna wersja stacji pogodowej.


Dlaczego osobny firmware rescue
Aktualny historyczny projekt zawierał:

- dużo globali
- dużo `extern`
- mocne sprzężenia między modułami
- ciężki `setup()`
- blokujące operacje sieciowe w ścieżce startowej

Firmware rescue ma być:

- mały
- przewidywalny
- szybki
- bezpieczny do OTA
- łatwy do diagnozowania w terenie


Aktualny zakres firmware rescue
MUSI być:

- TFT init
- backlight PWM
- LDR + auto dim
- wakeup window przy przyciemnionym ekranie
- Wi-Fi z 2 profilami
- static IP + DHCP fallback
- OTA
- NTP
- boot screen
- ekran główny
- ekran debug
- test dotyku z kursorem i współrzędnymi

NIE MA być:

- pogoda
- forecast
- AQI
- PAA / BfS
- wykresy
- ciężkie JSON
- HTTP w `setup()`


Główne założenia
1. Lekki setup()
`setup()` uruchamia tylko:

- boot core
- hardware
- runtime state
- display stack
- boot screen
- Wi-Fi
- OTA
- NTP

Bez pogody, AQI i innych ciężkich requestów.

2. UI po init TFT
TFT, filesystem, font renderer, sprite i `GfxUi` są inicjalizowane przed użyciem.

3. Szybki boot
Po starcie:

- podświetlenie ON
- boot screen
- Wi-Fi / OTA / NTP
- przejście do ekranu rescue

4. OTA jest krytyczne
Jeśli Wi-Fi jest aktywne, OTA ma działać zawsze.


Aktualna kolejność startu
Etap 1 - Boot Core

- `Serial.begin()`
- `logBanner()`
- `logMemoryStats()`

Etap 2 - Hardware Base

- `setupPwmOutputs()`
- `setLcdBrightness()`
- `setBlueLedBrightness()`
- `setupAmbientSensor()`
- `ambientServiceInit()`
- `touchServiceInit()`
- `TAMC_GT911`
- `initTouchScreen()`

Etap 3 - Runtime State

- reset flag runtime
- reset statusów Wi-Fi / OTA / NTP
- reset stanu touch debug
- reset znaczników odświeżania UI

Etap 4 - Display Stack

- `initJpegDecoder()`
- `initTft()`
- `initFileSystem()`
- smooth font stack (`.vlw` z LittleFS przez `TFT_eSPI`)
- `timeSprite`
- `GfxUi`

Etap 5 - Boot Screen

- czarne tło
- logo
- nazwa
- wersja
- progress bar

Etap 6 - Connectivity

- `startWiFi()`
- `setupOTA()`
- `syncTime()`

Etap 7 - Rescue UI

- ekran `Main`
- lub później `Debug` po dotknięciu


Aktualna pętla loop()
`loop()` jest celowo lekki i składa się z małych serwisów:

- `serviceOta()`
- `serviceRescueUi()`
- krótki `delay(10)`

Wewnątrz `serviceRescueUi()`:

- `serviceAmbientAndWake(nowMs)`
- `serviceRescueTouch(nowMs)`
- `serviceTimeSync(nowMs)`
- `serviceCurrentPage(nowMs)`


Aktualne ekrany UI
1. Main
Pokazuje:

- czas
- datę
- status Wi-Fi
- IP
- RSSI
- tryb sieci
- aktywny profil
- status NTP
- status OTA

2. Debug
Pokazuje:

- uptime
- status Wi-Fi
- IP
- RSSI
- aktywny profil
- tryb sieci
- status NTP
- timestamp
- ambient
- LCD PWM
- status OTA
- współrzędne dotyku
- test kursora

Na ekranie debug:

- zwykły dotyk służy do testu dotyku
- wyjście odbywa się przez przycisk `BACK`


Touch i wakeup window
Touch jest obsługiwany przez GT911.

Zasada:

- przy normalnej jasności dotyk może przełączać stronę
- przy mocnym przyciemnieniu pierwszy dotyk tylko wybudza ekran
- drugi dotyk w oknie 5 s wykonuje akcję

To zachowanie jest realizowane przez `touch_service.*`.


Ambient i jasność
LDR działa jako lekki serwis runtime:

- odczyt surowy
- proste filtrowanie
- wyznaczenie trybu day / mid / night
- ustawienie PWM LCD

Serwis ambient nie nadpisuje chwilowego wake boost po dotyku.


NTP
Synchronizacja czasu:

- startuje po Wi-Fi
- korzysta z wielu serwerów NTP
- retry działa również po starcie, jeśli pierwszy sync nie zdąży

UI pokazuje stan:

- `Time: pending`
- `Time: synced`


Wi-Fi
Aktualnie:

- 2 profile Wi-Fi
- static IP dla profilu
- fallback do DHCP
- zapamiętywany aktywny profil
- status połączenia widoczny na UI


PSRAM
Rescue firmware zakłada obecnie wariant bez PSRAM.

Z konfiguracji build zostały usunięte flagi wymuszające PSRAM, ponieważ testowana płytka nie inicjalizowała tej pamięci poprawnie.


Pliki aktywne w rescue path
Najważniejsze:

- `main.cpp`
- `connectivity.h`
- `settings.*`
- `display.h`
- `ambient.*`
- `ambient_service.*`
- `touch_service.*`
- `util.*`
- `persistence.h`
- `GfxUi.*`
- `gt911.*`


Świadomie wycięte z aktywnej ścieżki rescue

- weather update
- forecast update
- AQI update
- PAA / BfS update
- ciężkie requesty HTTP
- render legacy weather UI

Te elementy mogą nadal istnieć w repo, ale nie należą do aktywnej ścieżki startu rescue firmware.


Aktualny status
Firmware rescue obecnie zapewnia:

- stabilny boot
- działający TFT
- działający touch
- działające Wi-Fi
- działające OTA
- działające NTP
- ekran główny
- ekran debug
- test dotyku


Priorytet dalszych prac

1. utrzymać stabilność rescue path
2. nie dopuścić ciężkich operacji do `setup()`
3. dokumentować aktywną architekturę
4. dopiero potem rozwijać kolejne funkcje


Podsumowanie
To nie jest wersja finalna stacji pogodowej.

To jest:

- firmware ratunkowy
- stabilna baza serwisowa
- bezpieczny punkt do OTA i dalszego refactoru
