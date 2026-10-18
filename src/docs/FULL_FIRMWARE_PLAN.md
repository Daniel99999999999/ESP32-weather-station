FULL_FIRMWARE_PLAN.md

Cel
Ten dokument opisuje plan migracji od działającego firmware rescue do właściwego firmware stacji pogodowej.

Założenie podstawowe:

- rescue zostaje zachowane jako osobny, stabilny wariant
- pełny firmware nie wraca do starego ciężkiego `setup()`
- funkcje są dokładane etapami na bazie rescue runtime


Stan wyjściowy
Aktualnie rescue zapewnia:

- stabilny boot
- TFT
- GT911
- ambient / auto dim
- Wi-Fi
- OTA
- NTP
- ekran `Main`
- ekran `Debug`
- touch test

To jest nowy rdzeń pod pełną stację.


Docelowy podział architektury
1. Core

- boot
- hardware init
- runtime state
- connectivity
- time

2. Services

- weather current
- forecast
- AQI
- radiation
- persistence / cache

3. UI

- main page
- stats / debug
- partial redraw

4. Scheduler

- cykle odświeżania
- retry
- timeouty
- degradacja przy błędach


Zasady migracji
1. Nie blokować bootu
Żaden request danych nie może być wymagany do pojawienia się UI po starcie.

2. Jeden etap naraz
Najpierw dokładamy jedną funkcję, stabilizujemy ją, dopiero potem przechodzimy dalej.

3. Dane mogą być niepełne
Każdy moduł musi wspierać stan:

- brak danych
- dane stare
- dane z backupowego źródła

4. UI nie wymusza danych
UI ma renderować to, co jest dostępne, a nie blokować się na fetch.


Etapy migracji
Etap 1 - Current Weather

- dodać aktualną pogodę dla aktywnego miasta
- osobny network job
- osobny status `idle/running/ok/error/stale`
- brak forecastu
- brak AQI
- brak radiation

Etap 2 - Forecast

- dodać forecast jako osobny job
- osobny cache i osobny timestamp
- partial redraw tylko sekcji forecast

Etap 3 - AQI

- dodać AQI jako niezależny job
- własne timeouty
- własny status jakości danych

Etap 4 - Radiation

4.1. wydzielić stan radiation z globali
4.2. osobny job radiation
4.3. freshness / timestamp / trend
4.4. degradacja do backupowej innej stacji pomiarowej

To oznacza:

- źródło podstawowe ma priorytet
- jeśli źródło podstawowe jest nieświeże lub niedostępne, system przechodzi na źródło backupowe
- UI musi umieć pokazać, że dane są z backupu
- status jakości danych ma rozróżniać:
  - `primary`
  - `backup`
  - `stale`
  - `none`

Etap 5 - Full UI integration

- reintegracja sekcji main page
- partial redraw
- stats page
- zachowanie debug page


Model runtime dla pełnej wersji
`setup()` pozostaje rescue-like:

1. `initBootCore()`
2. `initHardwareBase()`
3. `initRuntimeState()`
4. `initDisplayStack()`
5. `showBootScreen()`
6. `startConnectivityServices()`
7. wejście do UI

`loop()` pozostaje lekki i serwisowy:

- OTA
- touch
- ambient
- time sync
- scheduler jobs
- UI refresh


Model network jobs
Każdy job powinien mieć:

- `enabled`
- `running`
- `lastStartMs`
- `lastSuccessMs`
- `lastErrorMs`
- `retryBackoffMs`
- `staleAfterMs`

Każdy job powinien zwracać stan:

- `none`
- `running`
- `ok`
- `stale`
- `error`
- `backup`


Plan kodowy najbliższych kroków
1. Wprowadzić jawny model źródła danych dla radiation
2. Wydzielić `AppState`
3. Dodać pierwszy pełny job `current weather`
4. Dodać cache timestampów danych
5. Rozszerzyć UI o status źródła danych


Priorytet
Najpierw stabilna architektura danych i jobów.
Potem dopiero pełne odtworzenie funkcji starej stacji pogodowej.
