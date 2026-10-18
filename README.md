# ESP32 Weather Station

Stacja pogodowa dla ESP32 z ekranem dotykowym 320x480 opartym na kontrolerze ST7796. Firmware wyświetla bieżącą pogodę, prognozę, jakość powietrza, promieniowanie oraz historię temperatur dla trzech miast.

## Funkcje

- bieżąca pogoda i prognoza OpenWeatherMap,
- AQI, PM2.5 i PM10 z WAQI,
- promieniowanie z PAA dla Polski i BfS dla Niemiec,
- wykres 24 próbek temperatury dla Kielc, Dauchingen i Tarnowitz,
- dotykowe menu ustawień,
- konfiguracja Wi-Fi, jasności, kolorów miast i widoczności sekcji,
- automatyczna regulacja jasności oraz tryb nocny,
- aktualizacje OTA,
- własny, lekki renderer UI z fontami smooth i ikonami pogodowymi.

## Wymagania

- PlatformIO,
- ESP32,
- ekran ST7796 320x480,
- kontroler dotyku GT911,
- konto OpenWeatherMap,
- token WAQI.

## Konfiguracja sekretów

Skopiuj `src/secrets.example.h` jako `src/secrets.h`, a następnie wpisz lokalne dane Wi-Fi oraz klucze API. Plik `src/secrets.h` jest ignorowany przez Git i nie powinien być publikowany.

## Budowanie

Przykładowy build dla środowiska szeregowego:

```powershell
pio run -e esp32-3248s035c-serial-com10
```

Przed pierwszym uruchomieniem wgraj także zawartość systemu plików LittleFS z fontami i bitmapami:

```powershell
pio run -e esp32-3248s035c-serial-com10 -t uploadfs
```

## Bezpieczeństwo

Pełne kopie flash mogą zawierać hasła Wi-Fi i tokeny API. Pliki `factory_backup*.bin` oraz lokalne pliki `src/secrets.h` są wykluczone z repozytorium.

## Licencja

Projekt zawiera kod wywodzący się z projektu ThingPulse i jest udostępniany na licencji MIT. Szczegóły znajdują się w pliku `LICENSE`.
