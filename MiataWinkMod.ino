/**
 * Miata Wink Mod - ESP32 Firmware (BLE Edition)
 * 
 * Ten kod steruje reflektorami popup w Mazdzie Miata (NA) za pomocą 4-kanałowego modułu przekaźników.
 * Umożliwia mruganie lewym/prawym okiem, mruganie naprzemienne (nieblokująca pętla fali), 
 * ustawianie "sleepy eyes" z regulacją wysokości oraz resetowanie świateł do stanu fabrycznego.
 * 
 * Komunikacja odbywa się za pośrednictwem Bluetooth Low Energy (BLE) przy użyciu profilu
 * Nordic UART Service (NUS) - standardu kompatybilnego z aplikacjami terminalowymi BLE
 * oraz stronami Web Bluetooth API.
 * 
 * BEZPIECZEŃSTWO (FAIL-SAFE):
 * Przekaźniki odcięcia zasilania silniczków reflektorów są podłączone jako Normalnie Zamknięte (NC).
 * Oznacza to, że jeśli ESP32 straci zasilanie lub zostanie wyłączone, światła będą działały
 * w 100% fabrycznie za pomocą oryginalnych przełączników w aucie.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==========================================
// USTAWIENIA SPRZĘTOWE
// ==========================================

// Piny sterowania przekaźnikami
#define RELAY_LEFT_PIN    18 // Przekaźnik 1: Odcięcie zasilania lewego silniczka (NC - Normally Closed)
#define RELAY_RIGHT_PIN   19 // Przekaźnik 2: Odcięcie zasilania prawego silniczka (NC - Normally Closed)
#define RELAY_SPARE_PIN   21 // Przekaźnik 3: Rezerwowy
#define RELAY_TRIGGER_PIN 22 // Przekaźnik 4: Wirtualny przycisk podnoszenia (NO - Normally Open)

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

// Domyślne opóźnienie dla "Sleepy Eyes" (w milisekundach)
int sleepyDelayMs = 220; 

// ==========================================
// ZMIENNE DLA NIEBLOKUJĄCEJ PĘTLI FALI (TOGGLE WAVE)
// ==========================================
enum ModMode {
  MODE_NORMAL,
  MODE_WAVE_LOOP
};

volatile ModMode currentMode = MODE_NORMAL;
volatile int waveStep = 0;
volatile unsigned long lastStepTime = 0;
const unsigned long STEP_DURATION = 800; // Czas jednego kroku w ms (800ms)

// ==========================================
// FUNKCJE POMOCNICZE PRZEKAŹNIKÓW
// ==========================================

/**
 * Steruje stanem przekaźnika z uwzględnieniem logiki Active Low/High.
 * @param pin Pin GPIO mikrokontrolera
 * @param active true = włącz cewkę przekaźnika (zmień stan styków), false = wyłącz cewkę (stan domyślny)
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
  delay(800);                         // Czekaj, aż prawe oko podniesie się całkowicie
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk (prawe oko zaczyna opadać)
  delay(800);                         // Czekaj, aż prawe oko opadnie całkowicie
  
  resetSystem();                      // Przywróć zasilanie lewemu oku
}

// Mrugnięcie prawym okiem (prawe stoi, lewe się rusza)
void winkRight() {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_RIGHT_PIN, true);    // Odetnij zasilanie prawego silnika
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij przycisk
  delay(800);                         // Czekaj na pełne podniesienie lewego oka
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk (lewe oko zaczyna opadać)
  delay(800);                         // Czekaj na opadnięcie lewego oka
  
  resetSystem();                      // Przywróć zasilanie prawemu oku
}

// Mrugnięcie oboma oczami jednocześnie (zwykły cykl góra-dół)
void winkBoth() {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij przycisk
  delay(800);                         // Światła w górę
  
  setRelay(RELAY_TRIGGER_PIN, false); // Puść przycisk
  delay(800);                         // Światła w dół
  
  resetSystem();
}

// Sleepy Eyes (Otwarcie reflektorów do połowy)
void sleepyEyes(int delayMs) {
  resetSystem();
  delay(50);
  
  setRelay(RELAY_TRIGGER_PIN, true);  // Wciśnij przycisk (światła ruszają w górę)
  delay(delayMs);                     // Czekaj określony czas (np. 200 ms)
  
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
      Serial.println("Połączono z telefonem!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Rozłączono z telefonem!");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        Serial.print("Otrzymano komendę: ");
        for (int i = 0; i < rxValue.length(); i++) {
          Serial.print(rxValue[i]);
        }
        Serial.println();

        // Jeśli tryb fali w pętli jest włączony, wyłączamy go przy każdej innej komendzie
        if (rxValue != "wink_a" && currentMode == MODE_WAVE_LOOP) {
          currentMode = MODE_NORMAL;
          resetSystem();
          Serial.println("Pętla fali zatrzymana przez inne żądanie.");
        }

        // Przetwarzanie komend
        if (rxValue == "wink_l") {
          Serial.println("Akcja: Wink Lewy");
          pTxCharacteristic->setValue("ACK: Wink L");
          pTxCharacteristic->notify();
          winkLeft();
        } 
        else if (rxValue == "wink_r") {
          Serial.println("Akcja: Wink Prawy");
          pTxCharacteristic->setValue("ACK: Wink R");
          pTxCharacteristic->notify();
          winkRight();
        } 
        else if (rxValue == "wink_b") {
          Serial.println("Akcja: Wink Oba");
          pTxCharacteristic->setValue("ACK: Wink Both");
          pTxCharacteristic->notify();
          winkBoth();
        } 
        else if (rxValue == "wink_a") {
          if (currentMode == MODE_WAVE_LOOP) {
            currentMode = MODE_NORMAL;
            resetSystem();
            Serial.println("Akcja: Pętla fali WYŁĄCZONA");
            pTxCharacteristic->setValue("ACK: Wave Loop OFF");
            pTxCharacteristic->notify();
          } else {
            resetSystem();
            currentMode = MODE_WAVE_LOOP;
            waveStep = 0; // Uruchomi obsługę w loop() od kroku 1
            Serial.println("Akcja: Pętla fali WŁĄCZONA");
            pTxCharacteristic->setValue("ACK: Wave Loop ON");
            pTxCharacteristic->notify();
          }
        } 
        else if (rxValue == "reset") {
          Serial.println("Akcja: Reset");
          pTxCharacteristic->setValue("ACK: Reset");
          pTxCharacteristic->notify();
          resetSystem();
        } 
        else if (rxValue.rfind("sleepy_", 0) == 0) { // Zaczyna się od "sleepy_"
          std::string msStr = rxValue.substr(7);
          int customDelay = atoi(msStr.c_str());
          if (customDelay >= 50 && customDelay <= 600) {
            sleepyDelayMs = customDelay;
            Serial.printf("Akcja: Sleepy Eyes (%d ms)\n", sleepyDelayMs);
            char reply[30];
            snprintf(reply, sizeof(reply), "ACK: Sleepy %d ms", sleepyDelayMs);
            pTxCharacteristic->setValue(reply);
            pTxCharacteristic->notify();
            sleepyEyes(sleepyDelayMs);
          } else {
            Serial.println("Błąd: Niepoprawny czas dla Sleepy");
            pTxCharacteristic->setValue("ERR: Invalid sleepy time");
            pTxCharacteristic->notify();
          }
        }
        else {
          Serial.println("Błąd: Nieznana komenda");
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
  pAdvertising->setMinPreferred(0x06);  // Czas odpowiedzi na skanowanie
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
  // Wzór użytkownika:
  // Krok 1: Lewe = 1 (otwarte), Prawe = 0 (zamknięte)
  // Krok 2: Lewe = 1 (otwarte), Prawe = 1 (otwarte)
  // Krok 3: Lewe = 0 (zamknięte), Prawe = 1 (otwarte)
  // Krok 4: Lewe = 0 (zamknięte), Prawe = 0 (zamknięte)
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
    // Przełączanie kroków po upływie STEP_DURATION (800ms)
    else if (now - lastStepTime >= STEP_DURATION) {
      lastStepTime = now;
      waveStep++;
      if (waveStep > 4) {
        waveStep = 1;
      }
      
      resetSystem();
      
      switch (waveStep) {
        case 1:
          // Krok 1: Lewe=1, Prawe=0
          // Zasilanie lewego włączone, prawego odcięte, wyzwalacz włączony (UP)
          setRelay(RELAY_RIGHT_PIN, true);   // Odetnij prawy
          setRelay(RELAY_LEFT_PIN, false);   // Włącz lewy
          setRelay(RELAY_TRIGGER_PIN, true); // Wyzwalacz ON (UP)
          Serial.println("Wave Loop: Krok 1 (Lewe=1, Prawe=0)");
          break;
          
        case 2:
          // Krok 2: Lewe=1, Prawe=1
          // Lewy odcięty (zostaje w górze), prawy włączony (idzie w górę), wyzwalacz włączony (UP)
          setRelay(RELAY_LEFT_PIN, true);     // Odetnij lewy (zostaje 1)
          setRelay(RELAY_RIGHT_PIN, false);   // Włącz prawy (idzie do 1)
          setRelay(RELAY_TRIGGER_PIN, true);  // Wyzwalacz ON (UP)
          Serial.println("Wave Loop: Krok 2 (Lewe=1, Prawe=1)");
          break;
          
        case 3:
          // Krok 3: Lewe=0, Prawe=1
          // Lewy włączony (idzie w dół), prawy odcięty (zostaje w górze), wyzwalacz wyłączony (DOWN)
          setRelay(RELAY_RIGHT_PIN, true);    // Odetnij prawy (zostaje 1)
          setRelay(RELAY_LEFT_PIN, false);    // Włącz lewy (idzie do 0)
          setRelay(RELAY_TRIGGER_PIN, false); // Wyzwalacz OFF (DOWN)
          Serial.println("Wave Loop: Krok 3 (Lewe=0, Prawe=1)");
          break;
          
        case 4:
          // Krok 4: Lewe=0, Prawe=0
          // Lewy odcięty (zostaje w dole), prawy włączony (idzie w dół), wyzwalacz wyłączony (DOWN)
          setRelay(RELAY_LEFT_PIN, true);     // Odetnij lewy (zostaje 0)
          setRelay(RELAY_RIGHT_PIN, false);   // Włącz prawy (idzie do 0)
          setRelay(RELAY_TRIGGER_PIN, false); // Wyzwalacz OFF (DOWN)
          Serial.println("Wave Loop: Krok 4 (Lewe=0, Prawe=0)");
          break;
      }
    }
  }
  
  delay(10); // Małe opóźnienie dla stabilności pętli
}
