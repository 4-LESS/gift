/*
 * Holographic Jukebox - ESP32-C3 BLE + KY-040 Encoder
 * Pines asignados:
 * - CLK: GPIO 0
 * - DT:  GPIO 1
 * - SW:  GPIO 3
 * - VCC: 3.3V
 * - GND: GND
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Nombre del dispositivo Bluetooth
#define DEVICE_NAME "HoloJukebox"

// UUIDs estándar de Nordic UART Service (NUS)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Pines de Hardware
const int PIN_CLK = 0;
const int PIN_DT  = 1;
const int PIN_SW  = 3;

// Objetos BLE
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Variables Encoder
volatile int encoderDelta = 0;
int lastClkState = HIGH;
unsigned long lastButtonPress = 0;
bool lastButtonState = HIGH;

// Callbacks de Conexión BLE
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("[BLE] Celular CONECTADO.");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("[BLE] Celular DESCONECTADO.");
  }
};

// Enviar comando por BLE y Serial
void sendCommand(const String& cmd) {
  Serial.print("[TX] ");
  Serial.print(cmd);

  if (deviceConnected && pTxCharacteristic != NULL) {
    pTxCharacteristic->setValue((uint8_t*)cmd.c_str(), cmd.length());
    pTxCharacteristic->notify();
  }
}

// Interrupción para lectura rápida del encoder
void IRAM_ATTR isrEncoder() {
  int clk = digitalRead(PIN_CLK);
  int dt  = digitalRead(PIN_DT);

  if (clk != dt) {
    encoderDelta++;
  } else {
    encoderDelta--;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Holographic Jukebox ESP32-C3 Test ---");

  // Configuración de Pines
  pinMode(PIN_CLK, INPUT_PULLUP);
  pinMode(PIN_DT,  INPUT_PULLUP);
  pinMode(PIN_SW,  INPUT_PULLUP);

  lastClkState = digitalRead(PIN_CLK);
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), isrEncoder, FALLING);

  // Inicializar BLE
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_RX,
                        BLECharacteristic::PROPERTY_WRITE
                      );

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Transmitiendo como 'HoloJukebox'. Esperando conexion...");
}

void loop() {
  // 1. Procesar giros de Encoder
  if (encoderDelta != 0) {
    int delta = encoderDelta;
    encoderDelta = 0;

    if (delta > 0) {
      sendCommand("ENC:UP\n");
    } else {
      sendCommand("ENC:DOWN\n");
    }
  }

  // 2. Procesar Pulsador (SW) con Debounce
  bool buttonState = digitalRead(PIN_SW);
  if (buttonState == LOW && lastButtonState == HIGH) {
    unsigned long now = millis();
    if (now - lastButtonPress > 200) { // 200 ms debounce
      sendCommand("BTN:CLICK\n");
      lastButtonPress = now;
    }
  }
  lastButtonState = buttonState;

  // 3. Reconexión automática de publicidad si se desconecta
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("[BLE] Reiniciando publicidad BLE...");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // 4. Test manual desde el monitor serial de Arduino IDE
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '+') sendCommand("ENC:UP\n");
    else if (c == '-') sendCommand("ENC:DOWN\n");
    else if (c == ' ') sendCommand("BTN:CLICK\n");
    else if (c >= '1' && c <= '5') {
      String tagCmd = "TAG:0" + String(c) + "\n";
      sendCommand(tagCmd);
    } else if (c == 'e' || c == '0') {
      sendCommand("TAG:EJECT\n");
    }
  }

  delay(10);
}
