/**
 * Miata Wink Mod - ESP32 Firmware (BLE Edition)
 * 
 * Ten kod steruje reflektorami popup w Mazdzie Miata (NA) za pomocą 4-kanałowego modułu przekaźników.
 * Umożliwia mruganie lewym/prawym okiem, mruganie naprzemienne (nieblokująca pętla fali), 
 * ustawianie "sleepy eyes" z regulacją wysokości oraz resetowanie świateł do stanu fabrycznego.
 * 
 * ZABEZPIECZENIE (PIN AUTHENTICATION):
 * Moduł domyślnie uruchamia się zablokowany. Aby nim sterować, należy najpierw
 * przesłać przez BLE komendę "auth_XXXX" z poprawnym kodem PIN zdefiniowanym poniżej.
 * 
 * PAMIĘĆ TRWAŁA (NVS):
 * Zmiany konfiguracji (czas trwania kroku, wysokość sleepy eyes) są zapisywane w pamięci
 * flash za pomocą biblioteki Preferences, dzięki czemu urządzenie pamięta je po odłączeniu zasilania.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ==========================================
// KONFIGURACJA BEZPIECZEŃSTWA
// ==========================================
#define AUTH_PIN "1234" // Zmień ten PIN na własny przed wgraniem programu!

// ==========================================
// USTAWIENIA SPRZĘTOWE (ESP32-C3)
// ==========================================
#define RELAY_LEFT_PIN    4  // Przekaźnik 1: Odcięcie zasilania lewego silniczka (NC - Normally Closed)
#define RELAY_RIGHT_PIN   5  // Przekaźnik 2: Odcięcie zasilania prawego silniczka (NC - Normally Closed)
#define RELAY_SPARE_PIN   6  // Przekaźnik 3: Rezerwowy
#define RELAY_TRIGGER_PIN 7  // Przekaźnik 4: Wirtualny przycisk podnoszenia (NO - Normally Open)

// Ustawienie logiki przekaźnika (Active Low dla większości chińskich modułów Arduino)
#define RELAY_ACTIVE_LOW true

// ==========================================
// USTAWIENIA BLE (NORDIC UART SERVICE - NUS)
// ==========================================
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // NUS Service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // RX (Zapis z telefonu do ESP32)
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // TX (Wysyłanie danych z ESP32 do telefonu)

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Stan autoryzacji urządzenia (blokowany przy starcie i rozłączeniu)
volatile bool isAuthenticated = false;

// Zmienna trwałego zapisu
Preferences preferences;

// ==========================================
// ZMIENNE KONTROLNE (ŁADOWANE Z NVS)
// ==========================================
volatile int sleepyDelayMs = 220;         // Czas odcięcia dla Sleepy Eyes (ms)
volatile unsigned long stepDuration = 800; // Czas jednego kroku w ms (fala/wink)

enum ModMode {
  MODE_NORMAL,
  MODE_WAVE_LOOP
};

volatile ModMode currentMode = MODE_NORMAL;
volatile int waveStep = 0;
volatile unsigned long lastStepTime = 0;

// ==========================================
// FUNKCJE POMOCNICZE PRZEKAŹNIKÓW
// ==========================================

/**
 * Steruje stanem przekaźnika z uwzględnieniem logiki Active Low/High.
 */
void setRelay(int pin, bool active) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, active ? LOW : HIGH);
  } else {
    digitalWrite(pin, active ? HIGH : LOW);
  }
}

/**
 * Przywraca fabryczny stan świateł (zasilanie obu silników włączone, wyzwalacz wyłączony)
 */
void resetSystem() {
  setRelay(RELAY_LEFT_PIN, false);   // Zamknięty styk NC -> Silnik ma zasilanie
  setRelay(RELAY_RIGHT_PIN, false);  // Zamknięty styk NC -> Silnik ma zasilanie
  setRelay(RELAY_TRIGGER_PIN, false); // Otwarty styk NO -> Przycisk nie jest wciśnięty
}

// ==========================================
// AKCJE POPUPÓW
// ==========================================

// Mrugnięcie lewym okiem (lewe stoi, prawe się rusza)
void winkLeft() {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_LEFT_PIN, true);     // Odetnij zasilanie lewego silnika (otwórz styk NC)
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij wirtualny przycisk podnoszenia
  delay(stepDuration);                // Używamy skonfigurowanego czasu kroku
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk (prawe oko zaczyna opadać)
  delay(stepDuration);
  
  resetSystem();                      // Przywróć zasilanie lewemu oku
}

// Mrugnięcie prawym okiem (prawe stoi, lewe się rusza)
void winkRight() {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_RIGHT_PIN, true);    // Odetnij zasilanie prawego silnika
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij przycisk
  delay(stepDuration);
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk (lewe oko zaczyna opadać)
  delay(stepDuration);
  
  resetSystem();                      // Przywróć zasilanie prawemu oku
}

// Mrugnięcie oboma oczami jednocześnie (zwykły cykl góra-dół)
void winkBoth() {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij przycisk
  delay(stepDuration);
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk
  delay(stepDuration);
  
  resetSystem();
}

// Sleepy Eyes (Otwarcie reflektorów do połowy)
void sleepyEyes(int delayMs) {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij przycisk (światła ruszają w górę)
  delay(delayMs);                     // Czekaj określony czas
  
  setRelay(RELAY_LEFT_PIN, true);     // Odetnij lewy silnik
  setRelay(RELAY_RIGHT_PIN, true);    // Odetnij prawy silnik (światła zamarzają w pozycji półotwartej)
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk (sygnał wyłączony)
}

// ==========================================
// CALLBACKI BLUETOOTH (BLE)
// ==========================================

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      isAuthenticated = false; // Każde nowe połączenie zaczyna bez autoryzacji
      Serial.println("Połączono z telefonem! Oczekiwanie na autoryzację...");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      isAuthenticated = false; // Rozłączenie resetuje stan autoryzacji
      Serial.println("Rozłączono z telefonem! Blokada aktywna.");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue().c_str();

      if (rxValue.length() > 0) {
        Serial.print("Otrzymano komendę: ");
        // Z przyczyn bezpieczeństwa nie drukujemy pełnego PINu w konsoli Serial w czystej postaci
        if (rxValue.rfind("auth_", 0) == 0) {
          Serial.println("auth_****");
        } else {
          Serial.println(rxValue.c_str());
        }

        // 1. OBSŁUGA AUTORYZACJI
        if (rxValue.rfind("auth_", 0) == 0) {
          std::string pinAttempt = rxValue.substr(5);
          if (pinAttempt == AUTH_PIN) {
            isAuthenticated = true;
            Serial.println("Autoryzacja udana!");
            pTxCharacteristic->setValue("ACK: Unlocked");
            pTxCharacteristic->notify();
          } else {
            isAuthenticated = false;
            Serial.println("Autoryzacja NIEUDANA!");
            pTxCharacteristic->setValue("ERR: Auth failed");
            pTxCharacteristic->notify();
          }
          return;
        }

        // 2. BLOKADA DLA NIEAUTORYZOWANYCH ZAPYTAŃ
        if (!isAuthenticated) {
          Serial.println("Odmowa dostępu: Urządzenie zablokowane!");
          pTxCharacteristic->setValue("ERR: Locked");
          pTxCharacteristic->notify();
          return;
        }

        // Jeśli tryb fali w pętli jest włączony, wyłączamy go przy każdej innej komendzie ruchowej
        if (rxValue != "wink_a" && currentMode == MODE_WAVE_LOOP) {
          currentMode = MODE_NORMAL;
          resetSystem();
          Serial.println("Pętla fali zatrzymana przez inne żądanie.");
        }

        // 3. OBSŁUGA KOMEND STEROWANIA I KONFIGURACJI (DOSTĘPNE PO ODBLOKOWANIU)
        if (rxValue == "wink_l") {
          pTxCharacteristic->setValue("ACK: Wink L");
          pTxCharacteristic->notify();
          winkLeft();
        } 
        else if (rxValue == "wink_r") {
          pTxCharacteristic->setValue("ACK: Wink R");
          pTxCharacteristic->notify();
          winkRight();
        } 
        else if (rxValue == "wink_b") {
          pTxCharacteristic->setValue("ACK: Wink Both");
          pTxCharacteristic->notify();
          winkBoth();
        } 
        else if (rxValue == "wink_a") {
          if (currentMode == MODE_WAVE_LOOP) {
            currentMode = MODE_NORMAL;
            resetSystem();
            pTxCharacteristic->setValue("ACK: Wave Loop OFF");
            pTxCharacteristic->notify();
          } else {
            resetSystem();
            currentMode = MODE_WAVE_LOOP;
            waveStep = 0;
            pTxCharacteristic->setValue("ACK: Wave Loop ON");
            pTxCharacteristic->notify();
          }
        } 
        else if (rxValue == "reset") {
          pTxCharacteristic->setValue("ACK: Reset");
          pTxCharacteristic->notify();
          resetSystem();
        } 
        // Konfiguracja Sleepy Eyes z suwaka na karcie głównej
        else if (rxValue.rfind("sleepy_", 0) == 0) { 
          std::string msStr = rxValue.substr(7);
          int customDelay = atoi(msStr.c_str());
          if (customDelay >= 50 && customDelay <= 600) {
            sleepyDelayMs = customDelay;
            Serial.printf("Tymczasowa zmiana: Sleepy (%d ms)\n", sleepyDelayMs);
            char reply[30];
            snprintf(reply, sizeof(reply), "ACK: Sleepy %d ms", sleepyDelayMs);
            pTxCharacteristic->setValue(reply);
            pTxCharacteristic->notify();
            sleepyEyes(sleepyDelayMs);
          } else {
            pTxCharacteristic->setValue("ERR: Invalid sleepy time");
            pTxCharacteristic->notify();
          }
        }
        // Zapisywanie ustawień w pamięci NVS z panelu konfiguracyjnego (zębatka)
        else if (rxValue.rfind("set_sleepy_", 0) == 0) { 
          std::string msStr = rxValue.substr(11);
          int val = atoi(msStr.c_str());
          if (val >= 50 && val <= 600) {
            sleepyDelayMs = val;
            preferences.putInt("sleepy_delay", val);
            Serial.printf("Zapisano NVS: Sleepy delay = %d ms\n", val);
            char reply[35];
            snprintf(reply, sizeof(reply), "ACK: Saved Sleepy %d ms", val);
            pTxCharacteristic->setValue(reply);
            pTxCharacteristic->notify();
          } else {
            pTxCharacteristic->setValue("ERR: Invalid sleepy range");
            pTxCharacteristic->notify();
          }
        }
        else if (rxValue.rfind("set_step_", 0) == 0) { 
          std::string msStr = rxValue.substr(9);
          int val = atoi(msStr.c_str());
          if (val >= 400 && val <= 1500) {
            stepDuration = val;
            preferences.putInt("step_duration", val);
            Serial.printf("Zapisano NVS: Step duration = %d ms\n", val);
            char reply[35];
            snprintf(reply, sizeof(reply), "ACK: Saved Step %d ms", val);
            pTxCharacteristic->setValue(reply);
            pTxCharacteristic->notify();
          } else {
            pTxCharacteristic->setValue("ERR: Invalid step range");
            pTxCharacteristic->notify();
          }
        }
        else {
          pTxCharacteristic->setValue("ERR: Unknown command");
          pTxCharacteristic->notify();
        }
      }
    }
};

// ==========================================
// GŁÓWNA KONFIGURACJA ARDUINO
// ==========================================

void setup() {
  Serial.begin(115200);
  Serial.println("Inicjalizacja Miata Wink Mod...");

  // Inicjalizacja pamięci trwałej NVS w przestrzeni "winkmod"
  preferences.begin("winkmod", false);
  sleepyDelayMs = preferences.getInt("sleepy_delay", 220); // Domyślnie 220 ms
  stepDuration = preferences.getInt("step_duration", 800);  // Domyślnie 800 ms
  Serial.printf("Załadowano z pamięci NVS - Sleepy delay: %d ms, Step duration: %d ms\n", sleepyDelayMs, stepDuration);

  // Konfiguracja pinów przekaźnika jako wyjścia
  pinMode(RELAY_LEFT_PIN, OUTPUT);
  pinMode(RELAY_RIGHT_PIN, OUTPUT);
  pinMode(RELAY_SPARE_PIN, OUTPUT);
  pinMode(RELAY_TRIGGER_PIN, OUTPUT);

  // Ustawienie stanu początkowego (wyłączone cewki przekaźników -> światła w trybie fabrycznym)
  resetSystem();

  // Inicjalizacja BLE
  BLEDevice::init("Miata Wink Mod BLE");

  // Tworzenie serwera BLE
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Tworzenie serwisu BLE NUS
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Tworzenie charakterystyki TX (wysyłanie powiadomień)
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // Tworzenie charakterystyki RX (odbieranie komend)
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                           CHARACTERISTIC_UUID_RX,
                                           BLECharacteristic::PROPERTY_WRITE
                                         );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // Uruchomienie serwisu
  pService->start();

  // Rozpoczęcie rozgłaszania (advertising)
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE gotowe. Oczekiwanie na połączenie z telefonem...");
}

void loop() {
  // Obsługa rozłączenia i ponownego rozgłaszania
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // daj czas na zwolnienie stosu Bluetooth
    
    // Na wypadek rozłączenia wyłączamy pętlę fali dla bezpieczeństwa
    if (currentMode == MODE_WAVE_LOOP) {
      currentMode = MODE_NORMAL;
      resetSystem();
    }
    
    pServer->startAdvertising(); // uruchom ponownie rozgłaszanie
    Serial.println("Rozpoczęto ponowne rozgłaszanie BLE...");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // ==========================================================
  // NIEBLOKUJĄCA OBSŁUGA PĘTLI FALI (TOGGLE WAVE LOOP)
  // ==========================================================
  if (currentMode == MODE_WAVE_LOOP) {
    unsigned long now = millis();
    
    // Inicjalizacja pętli
    if (waveStep == 0) {
      lastStepTime = now;
      waveStep = 1;
      resetSystem();
      setRelay(RELAY_RIGHT_PIN, true);   // Odetnij prawe (0)
      setRelay(RELAY_LEFT_PIN, false);   // Włącz lewe (1)
      setRelay(RELAY_TRIGGER_PIN, true); // Sygnał podnoszenia (UP)
      Serial.println("Wave Loop: Krok 1 (Lewe=1, Prawe=0)");
    } 
    // Przełączanie kroków po upływie stepDuration (wczytanego z konfiguracji)
    else if (now - lastStepTime >= stepDuration) {
      lastStepTime = now;
      waveStep++;
      if (waveStep > 4) {
        waveStep = 1;
      }
      
      resetSystem();
      
      switch (waveStep) {
        case 1:
          setRelay(RELAY_RIGHT_PIN, true);
          setRelay(RELAY_LEFT_PIN, false);
          setRelay(RELAY_TRIGGER_PIN, true);
          Serial.println("Wave Loop: Krok 1 (Lewe=1, Prawe=0)");
          break;
          
        case 2:
          setRelay(RELAY_LEFT_PIN, true);
          setRelay(RELAY_RIGHT_PIN, false);
          setRelay(RELAY_TRIGGER_PIN, true);
          Serial.println("Wave Loop: Krok 2 (Lewe=1, Prawe=1)");
          break;
          
        case 3:
          setRelay(RELAY_RIGHT_PIN, true);
          setRelay(RELAY_LEFT_PIN, false);
          setRelay(RELAY_TRIGGER_PIN, false);
          Serial.println("Wave Loop: Krok 3 (Lewe=0, Prawe=1)");
          break;
          
        case 4:
          setRelay(RELAY_LEFT_PIN, true);
          setRelay(RELAY_RIGHT_PIN, false);
          setRelay(RELAY_TRIGGER_PIN, false);
          Serial.println("Wave Loop: Krok 4 (Lewe=0, Prawe=0)");
          break;
      }
    }
  }
  
  delay(10); // Małe opóźnienie dla stabilności pętli
}
