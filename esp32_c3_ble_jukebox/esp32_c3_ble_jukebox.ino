/*
 * Holographic Jukebox (HoloDeck) - Firmware Definitivo ESP32-C3 BLE HID + NFC + Relé
 * ----------------------------------------------------------------------------------
 * Teclado Bluetooth BLE HID nativo ("HoloDeck-KB") - Latencia Nula (HWCDC no bloqueante):
 *   F1 (0x3A) -> ENC:UP    (Subir volumen / Arriba en menú)
 *   F2 (0x3B) -> ENC:DOWN  (Bajar volumen / Abajo en menú)
 *   F3 (0x3C) -> BTN:CLICK (Play/Pausa / Seleccionar)
 *   F4 (0x3D) -> BTN:HOLD  (Abrir/Cerrar Menú lateral - mantener 1.5s)
 *   F5 (0x3E) -> TAG:01    (Álbum 1)
 *   F6 (0x3F) -> TAG:02    (Álbum 2)
 *   F7 (0x40) -> TAG:03    (Álbum 3)
 *   F8 (0x41) -> TAG:EJECT (Volver al Reloj/Foto)
 *
 * Combo Resetear Tarjetas NFC:
 *   Mantener presionado el botón del encoder durante 6.5s borra la memoria NVS de tags
 *   para reasignar las próximas 3 tarjetas físicas como Álbum 1, 2 y 3.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>

// ================= PINES HARDWARE (100% SEGUROS EN ESP32-C3) =================
const int PIN_RELE     = 4;  // GPIO 4  -> IN del Relé HW-307

const int PIN_CLK      = 0;  // GPIO 0  -> Encoder CLK
const int PIN_DT       = 1;  // GPIO 1  -> Encoder DT
const int PIN_SW       = 3;  // GPIO 3  -> Encoder Pulsador SW

const int PIN_NFC_SDA  = 5;  // GPIO 5  -> RC522 SDA (SS / CS)
const int PIN_NFC_SCK  = 6;  // GPIO 6  -> RC522 SCK
const int PIN_NFC_MOSI = 7;  // GPIO 7  -> RC522 MOSI
const int PIN_NFC_MISO = 10; // GPIO 10 -> RC522 MISO
const int PIN_NFC_RST  = 20; // GPIO 20 -> RC522 RST

// ================= CODIGOS HID TECLADO ESTANDAR (F1..F8) =================
const uint8_t HID_KEY_F1 = 0x3A; // ENC:UP
const uint8_t HID_KEY_F2 = 0x3B; // ENC:DOWN
const uint8_t HID_KEY_F3 = 0x3C; // BTN:CLICK
const uint8_t HID_KEY_F4 = 0x3D; // BTN:HOLD
const uint8_t HID_KEY_F5 = 0x3E; // TAG:01
const uint8_t HID_KEY_F6 = 0x3F; // TAG:02
const uint8_t HID_KEY_F7 = 0x40; // TAG:03
const uint8_t HID_KEY_F8 = 0x41; // TAG:EJECT

// Descriptor HID estándar de Teclado (Report ID 1 -> 8 bytes: [mod, 0, k1..k6])
static const uint8_t KEYBOARD_REPORT_MAP[] = {
  0x05, 0x01,  // Usage Page (Generic Desktop)
  0x09, 0x06,  // Usage (Keyboard)
  0xA1, 0x01,  // Collection (Application)
  0x85, 0x01,  //   Report ID (1)
  0x05, 0x07,  //   Usage Page (Key Codes)
  0x19, 0xE0,  //   Usage Minimum (224)
  0x29, 0xE7,  //   Usage Maximum (231)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x01,  //   Logical Maximum (1)
  0x75, 0x01,  //   Report Size (1)
  0x95, 0x08,  //   Report Count (8)
  0x81, 0x02,  //   Input (Data, Variable, Absolute) ; Modifier byte
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x08,  //   Report Size (8)
  0x81, 0x01,  //   Input (Constant) ; Reserved byte
  0x95, 0x05,  //   Report Count (5)
  0x75, 0x01,  //   Report Size (1)
  0x05, 0x08,  //   Usage Page (LEDs)
  0x19, 0x01,  //   Usage Minimum (1)
  0x29, 0x05,  //   Usage Maximum (5)
  0x91, 0x02,  //   Output (Data, Variable, Absolute) ; LED report
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x03,  //   Report Size (3)
  0x91, 0x01,  //   Output (Constant) ; LED padding
  0x95, 0x06,  //   Report Count (6)
  0x75, 0x08,  //   Report Size (8)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x65,  //   Logical Maximum (101)
  0x05, 0x07,  //   Usage Page (Key Codes)
  0x19, 0x00,  //   Usage Minimum (0)
  0x29, 0x65,  //   Usage Maximum (101)
  0x81, 0x00,  //   Input (Data, Array) ; 6 Key bytes
  0xC0         // End Collection
};

// ================= OBJETOS Y ESTADO GLOBAL =================
Preferences prefs;
MFRC522 rfid(PIN_NFC_SDA, PIN_NFC_RST);

BLEHIDDevice* hidDevice = nullptr;
BLECharacteristic* inputKeyboard = nullptr;
BLECharacteristic* outputKeyboard = nullptr;
BLEServer* bleServer = nullptr;

volatile bool bleConnected = false;

bool releActivo = false;
unsigned long releStartTime = 0;
const unsigned long RELE_BOOT_MS = 10000;

unsigned long lastNfcScanTime = 0;
unsigned long nfcCooldownUntil = 0;

// Decodificador de cuadratura por tabla Gray (100% inmune a rebotes mecánicos del KY-040)
static const int8_t DRAM_ATTR ENC_STATES[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
volatile uint8_t encPrevNext = 0x03;
volatile int16_t encSubSteps = 0;
volatile int16_t encoderDetents = 0;
unsigned long lastEncSendMs = 0;
const unsigned long ENC_STEP_COOLDOWN_MS = 85; // Paso suave y controlado (1 muesca = 1 ítem)
unsigned long btnPressStart = 0;
bool lastButtonState = HIGH;
bool btnHoldSent = false;
bool btnNfcResetSent = false;
const unsigned long BTN_HOLD_MS = 1500;      // 1.5s abre menú lateral
const unsigned long BTN_NFC_RESET_MS = 6500; // 6.5s resetea memoria de tarjetas NFC

// Log seguro no bloqueante: solo imprime si realmente hay un Monitor Serial abierto leyendo el USB CDC
inline void dbgPrint(const char* msg) {
  if (Serial.isConnected()) {
    Serial.println(msg);
  }
}

// ================= CONTROL DEL RELÉ (HW-307) =================
void encenderRele() {
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, LOW);
  releActivo = true;
}

void apagarRele() {
  pinMode(PIN_RELE, INPUT); // Alta impedancia corta el HW-307
  releActivo = false;
}

// ================= INTERRUPCIÓN ENCODER (CUADRATURA COMPLETA SIN REBOTES) =================
void IRAM_ATTR isrEncoder() {
  uint8_t s = ((digitalRead(PIN_CLK) ? 2 : 0) | (digitalRead(PIN_DT) ? 1 : 0));
  encPrevNext = ((encPrevNext << 2) | s) & 0x0F;
  int8_t d = ENC_STATES[encPrevNext];
  if (d != 0) {
    encSubSteps += d;
    if (s == 0x03) { // Posición de reposo física en la muesca del KY-040
      if (encSubSteps >= 2)       encoderDetents++;
      else if (encSubSteps <= -2) encoderDetents--;
      encSubSteps = 0;
    }
  }
}

// ================= CALLBACKS BLE HID =================
class HoloServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleConnected = true;
    dbgPrint("[BLE] Galaxy A20 CONECTADO.");
  }

  void onDisconnect(BLEServer* pServer) override {
    bleConnected = false;
    dbgPrint("[BLE] Desconectado -> Reiniciando publicidad BLE...");
    BLEDevice::startAdvertising();
  }
};

class HoloSecurityCallbacks : public BLESecurityCallbacks {
  bool onSecurityRequest() override { return true; }
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t pass_key) override {}
  bool onConfirmPIN(uint32_t pass_key) override { return true; }

#if defined(CONFIG_BLUEDROID_ENABLED)
  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) override {
    if (auth_cmpl.success) {
      dbgPrint("[BLE] Emparejamiento (Bonding) EXITOSO.");
    } else {
      BLEDevice::startAdvertising();
    }
  }
#elif defined(CONFIG_NIMBLE_ENABLED)
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (desc->sec_state.encrypted) {
      dbgPrint("[BLE] Emparejamiento (Bonding) EXITOSO.");
    } else {
      BLEDevice::startAdvertising();
    }
  }
#endif
};

void setupBleHidKeyboard() {
  BLEDevice::init("HoloDeck-KB");

  BLESecurity* pSecurity = new BLESecurity();
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setAuthenticationMode(true, false, true);
  BLEDevice::setSecurityCallbacks(new HoloSecurityCallbacks());

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new HoloServerCallbacks());

  hidDevice = new BLEHIDDevice(bleServer);
  hidDevice->manufacturer()->setValue("HoloDeck");
  hidDevice->pnp(0x02, 0x05ac, 0x820a, 0x0110);
  hidDevice->hidInfo(0x00, 0x01);
  hidDevice->reportMap((uint8_t*)KEYBOARD_REPORT_MAP, sizeof(KEYBOARD_REPORT_MAP));

  inputKeyboard  = hidDevice->inputReport(1);
  outputKeyboard = hidDevice->outputReport(1);
  hidDevice->setBatteryLevel(100);
  hidDevice->startServices();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hidDevice->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06); // 7.5ms min connection interval
  advertising->setMaxPreferred(0x0C); // 15ms max connection interval (ultra baja latencia)
  BLEDevice::startAdvertising();

  dbgPrint("[BLE] Teclado HID 'HoloDeck-KB' anunciando y listo.");
}

void sendHidKey(uint8_t hidUsage, const char* label) {
  if (Serial.isConnected()) {
    Serial.printf("[TX] %s (0x%02X, BLE=%d)\n", label, hidUsage, bleConnected ? 1 : 0);
  }
  if (!bleConnected || inputKeyboard == nullptr) return;

  uint8_t pressReport[8] = {0, 0, hidUsage, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(pressReport, sizeof(pressReport));
  inputKeyboard->notify();
  delay(8);

  uint8_t releaseReport[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(releaseReport, sizeof(releaseReport));
  inputKeyboard->notify();
  delay(6);
}

// ================= MEMORIA TARJETAS NFC =================
String getStoredTagUid(int albumIndex) {
  prefs.begin("nfc_tags", true);
  String key = "tag" + String(albumIndex);
  String val = prefs.getString(key.c_str(), "");
  prefs.end();
  return val;
}

void storeTagUid(int albumIndex, const String& uid) {
  prefs.begin("nfc_tags", false);
  String key = "tag" + String(albumIndex);
  prefs.putString(key.c_str(), uid);
  prefs.end();
}

void resetStoredTags() {
  prefs.begin("nfc_tags", false);
  prefs.clear();
  prefs.end();
  dbgPrint("[NFC] Memoria de tarjetas reseteada.");
}

void checkNfcReader() {
  unsigned long now = millis();
  if (now - lastNfcScanTime < 120) return;
  lastNfcScanTime = now;

  if (nfcCooldownUntil > 0 && now < nfcCooldownUntil) return;
  if (!rfid.PICC_IsNewCardPresent()) return;

  if (!rfid.PICC_ReadCardSerial()) {
    rfid.PCD_Init();
    return;
  }

  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  rfid.PCD_Init();

  if (uidStr.length() == 0) return;
  nfcCooldownUntil = now + 2000;

  int albumAssigned = 0;
  for (int i = 1; i <= 3; i++) {
    String stored = getStoredTagUid(i);
    if (stored.length() > 0 && stored.equalsIgnoreCase(uidStr)) {
      albumAssigned = i;
      break;
    }
  }

  if (albumAssigned == 0) {
    for (int i = 1; i <= 3; i++) {
      String stored = getStoredTagUid(i);
      if (stored.length() == 0) {
        storeTagUid(i, uidStr);
        albumAssigned = i;
        break;
      }
    }
  }

  if (albumAssigned == 0) {
    static int fallbackIdx = 0;
    fallbackIdx = (fallbackIdx % 3) + 1;
    albumAssigned = fallbackIdx;
  }

  if (albumAssigned == 1)      sendHidKey(HID_KEY_F5, "TAG:01");
  else if (albumAssigned == 2) sendHidKey(HID_KEY_F6, "TAG:02");
  else                         sendHidKey(HID_KEY_F7, "TAG:03");
}

void setup() {
  // CRÍTICO EN ESP32-C3 USB-JTAG: evitar bloqueo de 100ms por línea cuando no hay Monitor Serial abierto
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);

  // 1. Encender relé por 10 segundos al energizar la rocola
  releStartTime = millis();
  encenderRele();

  // 2. Configurar pines del Encoder (ambas fases CLK y DT para cuadratura limpia)
  pinMode(PIN_CLK, INPUT_PULLUP);
  pinMode(PIN_DT,  INPUT_PULLUP);
  pinMode(PIN_SW,  INPUT_PULLUP);
  encPrevNext = ((digitalRead(PIN_CLK) ? 2 : 0) | (digitalRead(PIN_DT) ? 1 : 0));
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_DT),  isrEncoder, CHANGE);

  // 3. Inicializar Lector NFC RC522
  SPI.begin(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SDA);
  rfid.PCD_Init();

  // 4. Iniciar Teclado Bluetooth BLE HID ("HoloDeck-KB")
  setupBleHidKeyboard();
}

void loop() {
  // 1. Apagar el relé al cumplirse los 10 segundos de arranque
  if (releActivo && (millis() - releStartTime >= RELE_BOOT_MS)) {
    apagarRele();
    dbgPrint("[RELE] 10s cumplidos -> Relé APAGADO.");
  }

  // 2. Revisar lector NFC RC522
  checkNfcReader();

  // 3. Procesar giros del Encoder (1 paso suave cada ENC_STEP_COOLDOWN_MS, sin saltos bruscos)
  if (encoderDetents != 0 && (millis() - lastEncSendMs >= ENC_STEP_COOLDOWN_MS)) {
    noInterrupts();
    if (encoderDetents > 2) encoderDetents = 2;
    if (encoderDetents < -2) encoderDetents = -2;
    int stepDir = (encoderDetents > 0) ? 1 : -1;
    encoderDetents -= stepDir;
    interrupts();

    lastEncSendMs = millis();
    if (stepDir > 0) sendHidKey(HID_KEY_F1, "ENC:UP");
    else             sendHidKey(HID_KEY_F2, "ENC:DOWN");
  }

  // 4. Procesar botón del Encoder (Clic corto = F3, 1.5s = F4 Menú, 6.5s = Reset NFC)
  bool btn = digitalRead(PIN_SW);
  unsigned long now = millis();

  if (btn == LOW && lastButtonState == HIGH) {
    btnPressStart = now;
    btnHoldSent = false;
    btnNfcResetSent = false;
  } else if (btn == LOW && lastButtonState == LOW) {
    unsigned long held = now - btnPressStart;
    if (!btnHoldSent && held >= BTN_HOLD_MS) {
      sendHidKey(HID_KEY_F4, "BTN:HOLD");
      btnHoldSent = true;
    }
    if (!btnNfcResetSent && held >= BTN_NFC_RESET_MS) {
      resetStoredTags();
      btnNfcResetSent = true;
    }
  } else if (btn == HIGH && lastButtonState == LOW) {
    if (!btnHoldSent && (now - btnPressStart >= 45)) {
      sendHidKey(HID_KEY_F3, "BTN:CLICK");
    }
  }
  lastButtonState = btn;

  // 5. Prueba manual por Monitor Serial si está conectado a un PC
  if (Serial.isConnected() && Serial.available() > 0) {
    char c = Serial.read();
    if (c == '+')      sendHidKey(HID_KEY_F1, "ENC:UP");
    else if (c == '-') sendHidKey(HID_KEY_F2, "ENC:DOWN");
    else if (c == ' ') sendHidKey(HID_KEY_F3, "BTN:CLICK");
    else if (c == 'h') sendHidKey(HID_KEY_F4, "BTN:HOLD");
    else if (c == '1') sendHidKey(HID_KEY_F5, "TAG:01");
    else if (c == '2') sendHidKey(HID_KEY_F6, "TAG:02");
    else if (c == '3') sendHidKey(HID_KEY_F7, "TAG:03");
    else if (c == '0' || c == 'e') sendHidKey(HID_KEY_F8, "TAG:EJECT");
    else if (c == 'r') resetStoredTags();
  }

  delay(3);
}
