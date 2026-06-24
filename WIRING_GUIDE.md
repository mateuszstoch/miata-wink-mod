# Poradnik Instalacji Elektrycznej - Miata Wink Mod (ESP32)

Ten przewodnik pomoże Ci podłączyć fizyczny mikrokontroler ESP32, 4-kanałowy moduł przekaźników oraz przetwornicę zasilania do instalacji elektrycznej Mazdy Miata NA (1990-1997).

---

## ⚠️ WAŻNE ZASADY BEZPIECZEŃSTWA

1. **Odłącz akumulator:** Przed jakimkolwiek cięciem lub lutowaniem przewodów w aucie, bezwzględnie odłącz ujemną (-) klemę akumulatora.
2. **Zasilanie po stacyjce (ACC / IGN):** Przetwornicę zasilającą ESP32 podłącz pod zasilanie, które pojawia się dopiero po przekręceniu kluczyka (np. z wiązki radia przewód ACC, gniazdo zapalniczki lub bezpiecznik w kabinie). *Podłączenie bezpośrednio pod stałe 12V (akumulator) spowoduje, że ESP32 będzie działać 24/7 i rozładuje akumulator w kilka dni.*
3. **Zabezpieczenie bezpiecznikiem:** Na wejściu przetwornicy (linia 12V z auta) zainstaluj mały bezpiecznik rurkowy lub samochodowy (np. 2A lub 3A), aby chronić układ.
4. **Zastosowanie styków NC (Normally Closed):** Silniczki reflektorów podłączamy przez styki COM i NC przekaźników. Dzięki temu, gdy moduł nie ma zasilania, obwód jest zamknięty, a reflektory działają w pełni seryjnie.

---

## 1. Schemat Połączeń Mikrokontrolera ESP32 i Przekaźników

### Zasilanie Układu
*   **Przetwornica 12V -> 5V (Input):**
    *   **IN+ :** +12V po stacyjce (ACC) z auta (np. z radia lub zapalniczki) przez bezpiecznik 2A.
    *   **IN- :** Masa (GND) z karoserii auta.
*   **Przetwornica 12V -> 5V (Output):**
    *   **OUT+ (5V) :** Podłącz do pinu **VIN** (lub **5V**) płytki ESP32 oraz do pinu **VCC** (lub **JD-VCC**) modułu przekaźników.
    *   **OUT- (GND) :** Podłącz do pinu **GND** płytki ESP32 oraz pinu **GND** modułu przekaźników.

### Sterowanie Przekaźnikami (ESP32 -> Moduł Przekaźników)
Połącz piny GPIO mikrokontrolera ESP32 z wejściami sterującymi modułu przekaźników (IN1 - IN4):

| Płytka ESP32 | Moduł Przekaźników | Funkcja | Połączenie po stronie 12V |
| :--- | :--- | :--- | :--- |
| **GND** | **GND** | Wspólna masa | — |
| **VIN / 5V** | **VCC** | Zasilanie cewki | — |
| **GPIO 4** | **IN1** | Odcięcie Lewego Oka | Wpięty w zasilanie lewego silniczka (COM / NC) |
| **GPIO 5** | **IN2** | Odcięcie Prawego Oka | Wpięty w zasilanie prawego silniczka (COM / NC) |
| **GPIO 6** | **IN3** | Kanał Rezerwowy | — |
| **GPIO 7** | **IN4** | Wyzwalacz Popupów | Wpięty w przycisk deski rozdzielczej (COM / NO) |

*Uwaga dot. zasilania przekaźników:* Jeśli Twój moduł przekaźników posiada zworkę **VCC-JDVCC**, najlepiej ją zdjąć, podłączyć **JD-VCC do 5V** (z przetwornicy), a pin **VCC do 3.3V z ESP32**. Zapobiega to cofaniu się napięcia 5V z przekaźników na piny logiczne ESP32, co chroni mikrokontroler przed uszkodzeniem.

---

## 2. Podłączenie do Silniczków Reflektorów (Wink i Sleepy Eyes)

Każdy z reflektorów popup posiada 5-przewodową kostkę przy silniczku w komorze silnika. Interesuje nas przewód dostarczający **stałe 12V** (zabezpieczony fabrycznym bezpiecznikiem RETRACT 30A). 

*Najczęściej jest to przewód **Biało-Czerwony** (White/Red) lub **Czerwono-Biały** (Red/White). Przed przecięciem upewnij się za pomocą multimetru, że na tym przewodzie jest napięcie 12V, nawet gdy kluczyk w stacyjce jest wyjęty, a światła są wyłączone.*

### Lewy Reflektor (Przekaźnik 1)
1. Zlokalizuj przewód stałego 12V lewego silniczka reflektora i go przetnij.
2. Koniec przewodu idący od strony instalacji samochodu (od bezpieczników) podłącz do pinu **COM** (Common) Przekaźnika 1.
3. Koniec przewodu idący bezpośrednio do silniczka podłącz do pinu **NC** (Normally Closed) Przekaźnika 1.

### Prawy Reflektor (Przekaźnik 2)
1. Zlokalizuj przewód stałego 12V prawego silniczka reflektora i go przetnij.
2. Koniec przewodu idący od strony instalacji samochodu podłącz do pinu **COM** (Common) Przekaźnika 2.
3. Koniec przewodu idący bezpośrednio do silniczka podłącz do pinu **NC** (Normally Closed) Przekaźnika 2.

---

## 3. Podłączenie pod Przycisk Popup (Tombstone Switch)

Przycisk podnoszenia reflektorów na konsoli środkowej (nad radiem) działa poprzez zwieranie linii sygnałowej do masy (GND). Wpinamy się w niego równolegle, dzięki czemu ESP32 może symulować jego wciśnięcie, a przycisk fizyczny nadal działa bez zmian.

1. Zdejmij konsolę środkową ("tombstone") i zlokalizuj wtyczkę przycisku popup.
2. Zlokalizuj przewód sygnałowy wyzwalacza (najczęściej jest to przewód **Biało-Czarny** (White/Black) – zwarcie go z masą przy włączonym zapłonie powinno podnieść światła).
3. Wykonaj "wcinkę" (nie przecinaj przewodu! Ściągnij tylko kawałek izolacji i dolutuj dodatkowy kabel) i połącz ten przewód z pinem **NO** (Normally Open) Przekaźnika 4.
4. Pin **COM** (Common) Przekaźnika 4 połącz z dowolnym solidnym punktem masowym (GND) w kabinie lub z przewodem masowym (czarnym) we wtyczce przycisku.

---

## 4. Pierwsze Uruchomienie i Kalibracja

1. Wgraj program do ESP32 z poziomu komputera (upewnij się, że zasilanie z auta jest wtedy wyłączone).
2. Podłącz zasilanie z auta i połącz się za pomocą przeglądarki **Bluefy** na iOS (lub innej darmowej aplikacji BLE Terminal).
3. Przetestuj mrugnięcia lewym i prawym okiem.
4. **Kalibracja Sleepy Eyes:**
   * Domyślny czas otwierania świateł to **220 ms**.
   * Jeśli światła otwierają się za mało, użyj suwaka w interfejsie i zwiększ wartość (np. do 260 ms).
   * Jeśli otwierają się za dużo, zmniejsz wartość (np. do 180 ms).
   * Po znalezieniu idealnej wysokości dla swojej Miaty, możesz zapisać tę wartość w pamięci (lub zmienić domyślną wartość `sleepyDelayMs` w kodzie programu i wgrać go ponownie).
