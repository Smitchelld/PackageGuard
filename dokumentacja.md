
### **Raport Projektu IoT: System Monitorowania Przesyłek "PackageGuard Pro"**

**Autor:** [Twoje Imię i Nazwisko]
**Data:** 26.01.2026

---

#### **1. Założenia Projektu i Funkcjonalności**

Projekt "PackageGuard Pro" to zaawansowany system IoT zaprojektowany do monitorowania i zabezpieczania wartościowych przesyłek w czasie rzeczywistym. Głównym celem było stworzenie niezawodnego, energooszczędnego urządzenia, które informuje użytkownika o potencjalnych zagrożeniach dla paczki, takich jak wstrząsy, nieautoryzowane otwarcie czy nieodpowiednie warunki termiczne.

**Kluczowe funkcjonalności systemu:**

*   **Monitoring Środowiskowy:** Ciągły pomiar temperatury, wilgotności, ciśnienia atmosferycznego oraz natężenia światła.
*   **Detekcja Wstrząsów:** Wykrywanie upadków i uderzeń za pomocą akcelerometru.
*   **System Alarmowy:** Natychmiastowe generowanie i wysyłanie alarmów po przekroczeniu zdefiniowanych progów (np. zbyt silny wstrząs, za wysoka temperatura).
*   **Podwójny Tryb Komunikacji (Hybrydowy):**
    *   **Tryb Online (WiFi/MQTT):** Gdy urządzenie jest w zasięgu sieci WiFi, przesyła dane w czasie rzeczywistym do serwera.
    *   **Tryb Offline (Bluetooth LE):** Umożliwia bezpośrednią kontrolę i konfigurację urządzenia za pomocą aplikacji mobilnej, nawet bez dostępu do internetu.
*   **Buforowanie Danych:** W przypadku utraty połączenia z internetem, dane telemetryczne i alarmy są buforowane na karcie SD, a następnie automatycznie synchronizowane z serwerem po odzyskaniu łączności.
*   **Zarządzanie Użytkownikami i Urządzeniami:** System serwerowy pozwala na rejestrację wielu użytkowników i bezpieczne przypisywanie urządzeń do ich kont.
*   **Zdalna Konfiguracja:** Użytkownik może zdalnie (przez webowy dashboard lub aplikację mobilną) zmieniać progi alarmowe, częstotliwość raportowania i zachowanie urządzenia.

---

#### **2. Wykorzystane Czujniki i Elementy Wykonawcze**

Urządzenie zbudowane jest w oparciu o mikrokontroler ESP32 i komunikuje się z peryferiami za pomocą różnych interfejsów.

| Komponent             | Funkcja                                 | Interfejs Komunikacji | Typ I/O           |
| --------------------- | --------------------------------------- | --------------------- | ----------------- |
| **MPU6050**           | Akcelerometr i Żyroskop (detekcja wstrząsu) | I2C                   | Cyfrowa ( magistrala) |
| **BME280**            | Czujnik temp., wilgotności i ciśnienia  | I2C                   | Cyfrowa (magistrala) |
| **Czytnik Kart MicroSD** | Pamięć masowa (logi, bufor offline)     | SPI                   | Cyfrowa (magistrala) |
| **Wyświetlacz OLED**  | Interfejs użytkownika, status           | I2C                   | Cyfrowa (magistrala) |
| **Dzielnik Napięcia** | Pomiar napięcia baterii Li-Ion/Li-Po    | Wejście ADC           | Analogowa         |
| **Przycisk (Button)** | Interakcja z użytkownikiem (parowanie, reset) | Wejście Cyfrowe (GPIO) | Cyfrowa           |
| **Buzzer**            | Sygnalizacja dźwiękowa (alarm)          | Wyjście Cyfrowe (PWM) | Cyfrowa           |
| **Silnik Wibracyjny** | Sygnalizacja haptyczna (alarm)          | Wyjście Cyfrowe (GPIO) | Cyfrowa           |
| **Dioda LED RGB**     | Wskaźnik statusu urządzenia             | Wyjście Cyfrowe (RMT) | Cyfrowa           |

---

#### **3. Opis Wykorzystania BLE w Projekcie**

Bluetooth Low Energy (BLE) pełni w projekcie dwie kluczowe, strategiczne role, a urządzenie ESP32 działa jako **Peripheral**.

**Rola 1: Parowanie i Inicjalizacja Urządzenia (Provisioning)**
Po resecie fabrycznym urządzenie nie jest przypisane do żadnego użytkownika. Proces parowania odbywa się w pełni przez BLE, co jest bezpieczne i intuicyjne:
1.  Użytkownik wciska przycisk na obudowie, co aktywuje tryb parowania (advertising).
2.  Aplikacja mobilna skanuje otoczenie, znajduje urządzenie i nawiązuje z nim połączenie.
3.  Aplikacja wysyła do urządzenia komendę `PAIR:{userID}` (np. `PAIR:admin`), informując je, do kogo należy.
4.  ESP32 zapisuje `userID` w pamięci NVS i od tego momentu wie, na jaki temat MQTT ma wysyłać dane.

**Rola 2: Sterowanie Offline (Hybrid Control)**
Jest to kluczowa funkcjonalność zapewniająca niezawodność systemu. Jeśli urządzenie jest poza zasięgiem WiFi (np. w transporcie, w piwnicy), użytkownik może:
1.  Podejść do paczki i nawiązać bezpośrednie połączenie BLE z aplikacją.
2.  Wysyłać komendy `ARM`/`DISARM` bezpośrednio do urządzenia.
3.  Zmieniać pełną konfigurację (progi alarmowe, akcje) poprzez wysłanie komendy `CFG:{...}` zawierającej obiekt JSON.
4.  Odczytywać aktualny status czujników.

*Uwaga: Wymaganie dotyczące implementacji roli **Central** nie zostało zrealizowane, ponieważ w architekturze projektu ESP32 jest urządzeniem końcowym (serwerem BLE dla telefonu). Rolę Central pełni aplikacja mobilna. Implementacja roli Central w ESP32 byłaby możliwa np. do skanowania dodatkowych, zewnętrznych czujników BLE (np. pastylek temperatury).*

---

#### **4. Opis Komunikacji MQTT**

Komunikacja z serwerem odbywa się za pośrednictwem brokera MQTT (np. Mosquitto) i ustandaryzowanych tematów oraz formatu JSON.

**Struktura Tematów:** `packageguard/{userID}/{macAddress}/{typ_komunikatu}`

*   `{userID}`: Dynamiczny identyfikator właściciela urządzenia (np. `admin`), ustawiany podczas parowania BLE. Po resecie fabrycznym jest to `unassigned`.
*   `{macAddress}`: Unikalny adres MAC karty WiFi urządzenia.
*   `{typ_komunikatu}`: `data`, `event` lub `cmd`.

**Format Komunikatów (JSON):**

1.  **`.../data` (Urządzenie -> Serwer):** Cykliczny raport telemetryczny.
    ```json
    {
      "temp": 22.5, "hum": 45.1, "pres": 101325, "lux": 150.0,
      "bat": 3.81, "g": 1.02, "armed": true, "alarms": 5, "ts": 1674684000
    }
    ```

2.  **`.../event` (Urządzenie -> Serwer):** Wiadomość alarmowa, wysyłana natychmiast po zdarzeniu.
    ```json
    {
      "type": "ALARM_SHOCK", "val": 2.5, "ts": 1674684005
    }
    ```

3.  **`.../cmd` (Serwer -> Urządzenie):** Komenda sterująca lub konfiguracyjna.
    *   Sterowanie: `{"set": "ARM"}` lub `{"set": "DISARM"}`
    *   Konfiguracja:
        ```json
        {
          "config": {
            "shock_threshold_g": 0.5, "temp_max_c": 50.0, "stealth_mode_enabled": true, ...
          }
        }
        ```

---

#### **5. Funkcjonalności Aplikacji Serwerowej**

Aplikacja serwerowa została zaimplementowana w Pythonie z wykorzystaniem frameworka Flask i biblioteki Paho-MQTT.

*   **System Użytkowników:** Pełna obsługa rejestracji i logowania z bezpiecznym hashowaniem haseł.
*   **Webowy Dashboard:** Interfejs użytkownika dostępny przez przeglądarkę, który wizualizuje dane w czasie rzeczywistym. Wyświetla listę urządzeń, ich aktualny status (telemetrię) oraz historię ostatnich alarmów.
*   **Zdalna Konfiguracja:** Umożliwia edycję wszystkich parametrów urządzenia (progów, akcji, interwałów) przez dedykowany formularz. Zmiany są natychmiast wysyłane do urządzenia przez MQTT.
*   **Obsługa Cyklu Życia Urządzenia:** Serwer automatycznie wykrywa, kiedy urządzenie zostało zresetowane do ustawień fabrycznych (gdy zaczyna nadawać jako `unassigned`) i odłącza je od konta poprzedniego właściciela.
*   **API dla Aplikacji Mobilnej:** Wystawia endpointy RESTful do logowania, pobierania listy urządzeń i przypisywania nowych.

---

#### **6. Funkcjonalności Aplikacji Mobilnej**

Aplikacja na system Android stanowi mobilne centrum sterowania.

*   **System Logowania:** Umożliwia logowanie do konta użytkownika na serwerze.
*   **Parowanie BLE:** Główna metoda dodawania nowego urządzenia do systemu.
*   **Tryb Hybrydowy:** Aplikacja inteligentnie zarządza komunikacją. Jeśli jest połączona z urządzeniem przez BLE, wszystkie komendy (Uzbrój/Rozbrój, Konfiguracja) wysyła bezpośrednio, z pominięciem internetu. W przeciwnym razie korzysta z API serwera.
*   **Pełna Wizualizacja:** Wyświetla wszystkie kluczowe parametry telemetryczne (temperatura, bateria, wstrząsy, itp.) oraz historię ostatnich alarmów, analogicznie do dashboardu webowego.
*   **Pełna Konfiguracja:** Dedykowany ekran ustawień pozwala na modyfikację wszystkich parametrów urządzenia w trybie offline (przez BLE).

---