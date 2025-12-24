# Sistem Inteligent de Localizare și Control prin Gesturi

## Introducere

Ascensiunea IoT a permis monitorizarea și interacțiunea de la distanță cu dispozitivele fizice în moduri fără precedent. Acest proiect implementează un **Sistem Inteligent de Localizare și Control prin Gesturi** utilizând un microcontroller ESP32-C3. Sistemul combină urmărirea GPS în timp real cu controlul intuitiv prin gesturi, stabilind o legătură între interacțiunile fizice locale și stocarea datelor în cloud prin Firebase. Acest sistem demonstrează integrarea protocoalelor de comunicație multiple într-o soluție embedded coerentă.

## Context

În sistemele moderne de gestionare a flotelor și vehicule inteligente, există o nevoie tot mai mare de dispozitive care nu doar monitorizează locația, dar permit și șoferilor sau operatorilor să semnalizeze actualizări de stare fără interfețe complexe. Butoanele tradiționale pot fi greoaie; controlul prin gesturi "touchless" oferă o alternativă mai sigură și mai modernă. În plus, transmisia fiabilă a datelor către cloud este esențială pentru monitorizarea de la distanță. Acest proiect răspunde acestor nevoi prin prototiparea unui dispozitiv care urmărește locația și răspunde la mișcările mâinii pentru a declanșa alerte vizuale, simulând un sistem pentru vehicule de urgență sau utilitare.

## Descrierea Proiectului

Obiectivul acestui proiect este construirea unui sistem embedded conectat care:
1.  **Monitorizează Locația**: Achiziționează continuu date de latitudine și longitudine de la un modul GPS.
2.  **Recunoaște Gesturi**: Detectează mișcările mâinii pentru a controla indicatorii de stare **TBD**.
3.  **Oferă Feedback Vizual**: Utilizează un LED RGB și un ecran LCD pentru a oferi feedback imediat utilizatorului.
4.  **Sincronizare Cloud**: Încarcă datele de locație în timp real într-o bază de date Google Firebase Realtime Database pentru vizualizare de la distanță.

Dispozitivul funcționează autonom, conectându-se la o rețea Wi-Fi specificată și gestionând concurent intrările de la senzori.

## Arhitectură

### Prezentare Generală a Sistemului

Sistemul este construit în jurul **ESP32-C3**, un SoC low-cost, low-power, cu Wi-Fi și Bluetooth integrate. Acesta acționează ca unitate centrală de procesare, agregând datele de la senzorii GPS și de Gesturi.
-   **Intrare**: 
    + [NEO-6M GPS](https://randomnerdtutorials.com/guide-to-neo-6m-gps-module-with-arduino/)
    + [Senzor Gesturi DFRobot](https://wiki.dfrobot.com/Gesture%20%26%20Touch%20Sensor%20V1.0%20SKU:%20SEN0285)
-   **Procesare**:
    + [ESP32-C3](https://documentation.espressif.com/esp32-c3-mini-1_datasheet_en.pdf)
-   **Ieșire**:
    + [LCD I2C](https://randomnerdtutorials.com/esp32-esp8266-i2c-lcd-arduino-ide/)
    + LED RGB
    + [Firebase](https://firebase.google.com/docs)
    + [Web View](TDB TODO)

![Schema generala](./resources/image.png)

### Hardware
*   **Microcontroller**: ESP32-C3 DevKitM-1 (Arhitectură RISC-V).
*   **Modul GPS**: NEO-6M (comunică prin UART).
    *   *Configurație*: Conectat la Hardware Serial (Pinii 6 TX / 7 RX).
*   **Senzor Gesturi**: DFRobot Gesture Touch (comunică prin UART).
    *   *Configurație*: Conectat la Software Serial (Pinii 18 RX / 19 TX).
*   **Afișaj**: LCD 1602 cu Modul I2C.
    *   *Configurație*: SDA (Pin 4), SCL (Pin 5).
*   **Indicator**: LED RGB NeoPixel (Pin 8).
*   **Alimentare**: USB / 3.3V Logic.

![Schema Electrica](./resources/image2.png)

### Software
Firmware-ul este dezvoltat în **C++** utilizând ecosistemul **PlatformIO** cu framework-ul Arduino. Bibliotecile și modulele cheie includ:
*   **TinyGPSPlus**: Pentru parsarea propozițiilor NMEA de la modulul GPS.
*   **DFRobot_Gesture_Touch**: Pentru interpretarea fluxurilor de date binare de la senzorul de gesturi.
*   **Firebase ESP Client**: Pentru autentificare securizată și trimiterea datelor JSON către Firebase Realtime Database.
*   **LiquidCrystal_I2C**: Pentru controlul afișajului.
*   **SoftwareSerial**: Pentru crearea unei interfețe UART secundare pe pinii GPIO.

**Detalii de Implementare:**
*   **Rezolvarea Conflictelor Serial**: ESP32-C3 are resurse UART hardware limitate. GPS-ul este prioritizat pe Hardware Serial (remapat pe pinii 6/7) pentru fiabilitate. Senzorul de gesturi utilizează un port serial emulat software pe pinii 18/19.
*   **Logică Non-Blocantă**: Funcția `loop()` este proiectată pentru a gestiona interogarea senzorilor la frecvență înaltă, gestionând în același timp sarcinile de rețea la frecvență joasă (actualizări Firebase la fiecare 5 secunde) fără a bloca execuția.

### 1. Embedded (Firmware)
Sistemul funcționează într-o buclă infinită (`loop()`) care verifică secvențial perifericele, asigurând un comportament non-blocant.

1.  **Inițializare (`setup`)**:
    *   Configurare Serial (Debug, GPS, Gesturi).
    *   Inițializare LCD și LED (Secvență de boot).
    *   Conectare Wi-Fi și Autentificare Firebase.
2.  **Bucla Principală (`loop`)**:
    *   **Pasul A: Citire Gesturi**: Interoghează senzorul DFRobot. Dacă este detectat un gest, actualizează variabila de stare `ledMode` și afișează pe LCD.
    *   **Pasul B: Efecte Vizuale**: Dacă modul "Girofar" este activ, alternează culorile LED-ului (Roșu/Albastru) folosind `millis()` pentru a nu bloca execuția.
    *   **Pasul C: Procesare GPS**: Citește fluxul UART de la GPS și pasează caracterele parserului `TinyGPSPlus`.
    *   **Pasul D: Sincronizare Cloud**: La fiecare 5 secunde, dacă există o locație validă, trimite un JSON către Firebase.

**A. Configurarea Serială Hibridă**
Pentru a acomoda doi senzori seriali pe un cip cu un singur UART hardware liber, am utilizat o abordare hibridă:
```cpp
// GPS pe Hardware Serial (Fiabilitate critică)
HardwareSerial gpsHwSerial(1);
gpsHwSerial.begin(9600, SERIAL_8N1, 7, 6); // RX=7, TX=6

// Senzor Gesturi pe Software Serial (Viteză redusă)
SoftwareSerial gestureSerial(18, 19); // RX=18, TX=19
DFRobot_Gesture_Touch dfgt(&gestureSerial);
```

**B. Logica de Control prin Gesturi**
Un `switch` interpretează codul gestului primit și schimbă starea sistemului:
```cpp
int8_t rslt = dfgt.getAnEvent();
if (rslt != 0) {
  switch (rslt) {
    case DFGT_EVT_TOUCH1:
      ledMode = 1; // Activează modul Girofar
      break;
    case DFGT_EVT_TOUCH2:
      pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Roșu manual
      pixels.show();
      break;
    // ... alte cazuri
  }
}
```

**C. Sincronizarea Non-Blocantă cu Firebase**
Se utilizează `millis()` pentru a trimite date doar la intervale specifice, permițând codului să citească senzorii în restul timpului:
```cpp
if (millis() - sendDataPrevMillis > 5000) {
  sendDataPrevMillis = millis();
  if (gps.location.isValid()) {
    FirebaseJson json;
    json.set("lat", gps.location.lat());
    json.set("lon", gps.location.lng());
    Firebase.RTDB.setJSON(&fbdo, "/location", &json);
  }
}
```

### 2. Web
Componenta web a sistemului servește drept panou de control și monitorizare, permițând utilizatorului să vizualizeze locația dispozitivului în timp real și să interacționeze cu acesta. Aplicația este construită ca o pagină web care rulează direct în browser.

**Tehnologii Utilizate:**
*   **HTML5 / CSS3**: Pentru structura paginii și stilizarea controalelor.
*   **Google Maps JavaScript API**: Motorul principal de vizualizare, responsabil pentru afișarea hărții, a markerului de locație și a trasării rutei.
*   **Firebase SDK (Client)**: Asigură conexiunea directă și în timp real cu baza de date, fără necesitatea unui backend server-side adițional.

![Firebase](./resources/image4.png)

**Funcționalități Cheie:**
1.  **Monitorizare în Timp Real (Live Tracking)**:
    *   Aplicația se abonează la nodul `/location` din Firebase Realtime Database.
    *   La recepționarea unor noi coordonate (latitudine/longitudine), poziția markerului de pe hartă este actualizată instantaneu, iar harta este recentrată automat pentru a urmări dispozitivul.
2.  **Vizualizarea Traseului (Path Tracking)**:
    *   Utilizatorul poate activa opțiunea **"Urmărește drumul"** printr-un checkbox dedicat.
    *   Când este activată, aplicația stochează istoric toate punctele de locație primite în acea sesiune și desenează o polilinie (`Google Maps Polyline`) roșie pe hartă, permițând vizualizarea traiectoriei exacte a vehiculului/dispozitivului.
3.  **Control de la Distanță (Alarmă)**:
    *   Interfața include un buton de comandă **"TRIGGER ALARM"**.
    *   Acționarea acestui buton trimite o comandă către ESP32 prin setarea valorii `true` pe nodul `/alarm` din baza de date. Aceasta demonstrează capacitatea de comunicare bidirecțională (Internet -> Dispozitiv).

**Arhitectură Web:**
Interfața este proiectată cu un design responsive și minimalist. Elementele de control (butonul de alarmă și comutatorul de urmărire) sunt poziționate într-un container suprapus ("floating overlay") în colțul din dreapta-sus al hărții pentru a asigura accesibilitatea rapidă fără a obstrucționa vizibilitatea hărții.

![Web](./resources/image3.png)

## Rezultate
![Rezultat](./resources/image5.png)

![Rezultat gif](./resources/result.gif)

Implementarea finală a sistemului confirmă faptul că toate obiectivele tehnice au fost atinse, rezultând o instalație complet funcțională și stabilă. În cadrul testelor de performanță, dispozitivul a demonstrat o capacitate excelentă de a gestiona simultan procesele locale și sincronizarea cu infrastructura cloud, fără erori de execuție sau latențe sesizabile de către utilizator.

Monitorizarea locației este deplin operațională, coordonatele extrase de modulul GPS fiind transmise constant către Firebase și mapate cu precizie pe interfața web. Această conexiune permite o vizualizare fluidă a deplasării în timp real, oferind un istoric clar al traseului parcurs. Totodată, sistemul de control prin gesturi răspunde prompt, permițând activarea modului „girofar” printr-o simplă mișcare a mâinii; în acest mod, LED-ul RGB alternează culorile roșu și albastru, simulând fidel avertizarea vizuală a unui vehicul de intervenție.

Fiabilitatea comunicației bidirecționale a fost validată prin funcția de declanșare a alarmei de la distanță (triggering), unde comenzile transmise din panoul web sunt recepționate și executate de ESP32-C3 aproape instantaneu. Structura flexibilă a bazei de date Firebase permite nu doar monitorizarea parametrilor actuali, ci și extinderea viitoare a sistemului pentru a transmite orice alte date telemetrice necesare. În concluzie, ansamblul hardware și software funcționează exact cum s-a dorit, reprezentând o soluție IoT coerentă și robustă pentru localizare și control inteligent.

## Concluzii
Acest proiect a vizat dezvoltarea unui sistem inteligent de localizare și control prin gesturi, oferind o soluție intuitivă pentru monitorizarea vehiculelor și interacțiunea "touchless" cu indicatorii de stare ai acestora. Rezultatul final este un prototip funcțional, capabil să integreze date geografice precise cu o interfață de control modernă, eliminând necesitatea interacțiunii fizice cu butoane mecanice în timpul condusului.

Deși proiectul a pus bazele unei platforme solide pentru monitorizarea de la distanță, este important de menționat că sistemul se află în stadiul de prototip și poate fi rafinat pentru o utilizare profesională. Precizia localizării depinde de calitatea semnalului GPS, iar în medii urbane dens populate pot apărea abateri minore. Pentru a crește rigoarea sistemului, o îmbunătățire viitoare ar putea consta în integrarea unui sistem de corecție a datelor sau utilizarea unui modul GPS cu antenă activă performantă.

Din punct de vedere al extinderii funcționalităților, proiectul ar putea beneficia de integrarea unor senzori suplimentari, cum ar fi un accelerometru pentru detectarea impactului sau a stilului de condus, și afișarea unor recomandări de siguranță pe interfața web pe baza vitezei de deplasare. De asemenea, implementarea unui modul de notificare tip „push” pe dispozitivele mobile ar putea alerta utilizatorul instantaneu în momentul în care alarma este declanșată.

Obiectivul principal a fost construirea unui sistem IoT complet, de la hardware la cloud, și aprofundarea provocărilor tehnice ce apar în dezvoltarea soluțiilor embedded. Din această perspectivă, proiectul și-a îndeplinit rolul, oferindu-mi oportunitatea de a progresa dincolo de conceptele studiate în laboratoare și de a gestiona integrarea complexă a protocoalelor de comunicație într-un produs coerent.

## Referințe

1.  **Espressif Systems**: [ESP32-C3 Datasheet](https://www.espressif.com/en/products/socs/esp32-c3)
2.  **PlatformIO**: [Documentație](https://docs.platformio.org/)
3.  **DFRobot**: [Wiki Senzor Gesturi](https://wiki.dfrobot.com/)
4.  **TinyGPS++ Library**: [GitHub Repository](https://github.com/mikalhart/TinyGPSPlus)
5.  **Firebase**: [Documentație Realtime Database](https://firebase.google.com/docs/database)