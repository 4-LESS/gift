/*
 * Holographic Jukebox (HoloDeck) - Firmware Completo ESP32-C3 + OTA
 * -----------------------------------------------------------------
 * Hardware (Respetando pines prohibidos GPIO 2, 8, 9, 18, 19):
 * - Relé de encendido celular (HW-307): IN = GPIO 4 (10s al arrancar, corte por alta impedancia)
 * - Encoder KY-040: CLK = GPIO 0, DT = GPIO 1, SW = GPIO 3
 * - Lector NFC RC522 (SPI): SDA(SS) = GPIO 5, SCK = GPIO 6, MOSI = GPIO 7, MISO = GPIO 10, RST = GPIO 20
 *
 * OTA (Subida de código por Wi-Fi):
 * - Hostname en Arduino IDE: "holojukebox"
 * - Red por defecto: "miinternet.cl-2.4G-1057d8" (Clave Wi-Fi y OTA: "orion2104")
 * - Al cambiar de Wi-Fi mediante el QR, la red y el código/clave OTA se actualizan
 *   automáticamente a los mismos datos ingresados en el portal del QR.
 *
 * Librerías requeridas en Arduino IDE:
 * - "WebSockets" by Markus Sattler (Links2004)
 * - "MFRC522" by GithubCommunity
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <MFRC522.h>
#include "index_html.h"

// ================= PINES HARDWARE (100% SEGUROS EN ESP32-C3) =================
// Prohibidos NO usados: GPIO 2, GPIO 8, GPIO 9 (Strapping) y GPIO 18, GPIO 19 (USB D-/D+)
const int PIN_RELE     = 4;  // GPIO 4  -> IN del Relé HW-307

const int PIN_CLK      = 0;  // GPIO 0  -> Encoder CLK
const int PIN_DT       = 1;  // GPIO 1  -> Encoder DT
const int PIN_SW       = 3;  // GPIO 3  -> Encoder Pulsador SW

const int PIN_NFC_SDA  = 5;  // GPIO 5  -> RC522 SDA (SS / CS)
const int PIN_NFC_SCK  = 6;  // GPIO 6  -> RC522 SCK
const int PIN_NFC_MOSI = 7;  // GPIO 7  -> RC522 MOSI
const int PIN_NFC_MISO = 10; // GPIO 10 -> RC522 MISO
const int PIN_NFC_RST  = 20; // GPIO 20 -> RC522 RST (o MFRC522::UNUSED_PIN si unes RST a 3.3V)

// ================= RED LOCAL POR DEFECTO Y PORTAL QR =================
const char* DEFAULT_WIFI_SSID = "miinternet.cl-2.4G-1057d8";
const char* DEFAULT_WIFI_PASS = "orion2104";

const char* AP_SSID = "HoloJukebox-Setup";
const char* AP_PASS = "12345678";
const byte  DNS_PORT = 53;

// ================= OBJETOS GLOBALES =================
Preferences prefs;
WebServer server(80);
WebSocketsServer webSocket(81);
DNSServer dnsServer;
MFRC522 rfid(PIN_NFC_SDA, PIN_NFC_RST);

// ================= TARJETAS / TAGS NFC CON AUTO-MEMORIA =================
// Guarda automáticamente cada tarjeta física que acerques en la memoria Flash
// Slot 1 = Primer tag que acerques -> Álbum 1 (THE LIFE OF A SHOWGIRL)
// Slot 2 = Segundo tag que acerques -> Álbum 2 (1989 DELUXE)
// Slot 3 = Tercer tag que acerques -> Álbum 3 (LOVER)
unsigned long lastNfcScanTime = 0;
unsigned long nfcCooldownUntil = 0;

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
  logWireless("[NFC] ¡Memoria de tarjetas reseteada! Las proximas tarjetas se asignaran como Album 1, 2 y 3.");
}

bool apModeActive = false;
bool releActivo = false;
unsigned long releStartTime = 0;
const unsigned long RELE_BOOT_MS = 10000; // 10 segundos al encender

// Variables Encoder
volatile int encoderDelta = 0;
unsigned long btnPressStart = 0;
bool lastButtonState = HIGH;
bool btnHoldSent = false;
const unsigned long BTN_HOLD_MS = 1800; // 1.8s para abrir el menú de opciones

// ================= CONTROL DEL RELÉ (HW-307) =================
void encenderRele() {
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, LOW); // Activa relé
  releActivo = true;
}

void apagarRele() {
  pinMode(PIN_RELE, INPUT);    // Alta impedancia para cortar los 5V del HW-307
  releActivo = false;
}

// ================= INTERRUPCIÓN ENCODER =================
void IRAM_ATTR isrEncoder() {
  int clk = digitalRead(PIN_CLK);
  int dt  = digitalRead(PIN_DT);
  if (clk != dt) {
    encoderDelta++;
  } else {
    encoderDelta--;
  }
}

// ================= ENVÍO WEBSOCKET Y MONITOR WEB =================
void broadcastCommand(String cmd) {
  Serial.print("[WS TX] ");
  Serial.print(cmd);
  webSocket.broadcastTXT(cmd);
}

void logWireless(String msg) {
  Serial.println(msg);
  String pkt = "LOG:" + msg + "\n";
  webSocket.broadcastTXT(pkt);
}

void handleCharCommand(char c) {
  if (c == 'o' || c == 'O') {
    if (releActivo) {
      apagarRele();
      logWireless("[MANUAL] Rele APAGADO");
    } else {
      encenderRele();
      logWireless("[MANUAL] Rele ENCENDIDO");
    }
  } else if (c == 'w' || c == 'W') {
    startCaptivePortal();
  } else if (c == 'h' || c == 'H') {
    broadcastCommand("BTN:HOLD\n");
  } else if (c == '+') {
    broadcastCommand("ENC:UP\n");
  } else if (c == '-') {
    broadcastCommand("ENC:DOWN\n");
  } else if (c == ' ') {
    broadcastCommand("BTN:CLICK\n");
  } else if (c >= '1' && c <= '5') {
    broadcastCommand("TAG:0" + String(c) + "\n");
  } else if (c == '0' || c == 'e') {
    broadcastCommand("TAG:EJECT\n");
  } else if (c == 'r' || c == 'R') {
    resetStoredTags();
  }
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[WS] Cliente #%u conectado desde %s\n", num, ip.toString().c_str());
    // Informar la IP actual de la ESP32 al conectarse
    String currentIp = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    String msg = "ESP_IP:" + currentIp + "\n";
    webSocket.sendTXT(num, msg);
  } else if (type == WStype_TEXT && length > 0) {
    String txt = String((char*)payload);
    txt.trim();
    if (txt.startsWith("CMD:") && txt.length() >= 5) {
      handleCharCommand(txt.charAt(4));
    }
  }
}

// ================= PORTAL CAUTIVO WIFI HTML =================
String buildPortalHtml() {
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    options += "<option value='" + ssid + "'>" + ssid + " (" + String(rssi) + " dBm)</option>";
  }
  if (n == 0) {
    options = "<option value=''>No se encontraron redes (escribe abajo)</option>";
  }

  String html = R"rawliteral(
<!DOCTYPE html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HoloDeck // Conexión Wi-Fi</title>
<style>
  body{background:#000;color:#fff;font-family:monospace;padding:24px;margin:0}
  .box{max-width:380px;margin:0 auto;border:1px solid #333;padding:20px}
  h2{color:#ff7722;font-size:15px;letter-spacing:.15em;margin:0 0 14px;border-bottom:1px solid #222;padding-bottom:10px}
  label{display:block;font-size:11px;color:#888;margin:14px 0 6px;letter-spacing:.08em}
  select,input,button{width:100%;padding:10px;background:#111;border:1px solid #333;color:#fff;font-family:monospace;box-sizing:border-box;font-size:13px}
  button{background:#ff7722;color:#000;font-weight:bold;border:none;margin-top:18px;cursor:pointer;letter-spacing:.12em}
  .sub{font-size:11px;color:#777;margin-top:14px;line-height:1.4}
</style></head><body>

<div class="box">
  <h2>HOLODECK // CONEXION WI-FI</h2>
  <form action="/save" method="POST">
    <label>REDES DETECTADAS:</label>
    <select onchange="document.getElementById('ssid').value=this.value">
      <option value="">-- Seleccionar red --</option>
      )rawliteral" + options + R"rawliteral(
    </select>
    <label>NOMBRE DE RED (SSID):</label>
    <input type="text" id="ssid" name="ssid" placeholder="Nombre de tu Wi-Fi" required>
    <label>CONTRASEÑA:</label>
    <input type="password" name="pass" placeholder="Contraseña Wi-Fi">
    <button type="submit">CONECTAR ROCOLA</button>
  </form>
  <div class="sub">Para ajustes de Cuenta, Pantalla o Color de Acento, mantén presionada la perilla de la rocola durante 2 segundos.</div>
</div>

</body></html>
  )rawliteral";
  return html;
}

void startCaptivePortal() {
  if (apModeActive) return;
  apModeActive = true;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress apIP = WiFi.softAPIP(); // 192.168.4.1
  dnsServer.start(DNS_PORT, "*", apIP);

  Serial.println("\n==========================================");
  Serial.println("[AP] Modo Configuración Activado");
  Serial.print("[AP] Red: "); Serial.println(AP_SSID);
  Serial.print("[AP] IP Portal: http://"); Serial.println(apIP);
  Serial.println("==========================================\n");
}

void setupWebServer() {
  // Página principal: si estamos en modo AP muestra el configurador Wi-Fi;
  // si piden /jukebox muestra la interfaz local de respaldo
  server.on("/", HTTP_GET, []() {
    if (apModeActive) {
      server.send(200, "text/html", buildPortalHtml());
    } else {
      server.send_P(200, "text/html", INDEX_HTML);
    }
  });

  server.on("/setup", HTTP_GET, []() {
    server.send(200, "text/html", buildPortalHtml());
  });

  server.on("/disc", HTTP_POST, []() {
    String dnum  = server.arg("dnum");
    String label = server.arg("label");
    String url   = server.arg("url");
    String cmd = "DISC_CFG:" + dnum + "|" + label + "|" + url + "\n";
    broadcastCommand(cmd);
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "Disco actualizado");
  });

  server.on("/ytlogin", HTTP_POST, []() {
    broadcastCommand("YT:LOGIN\n");
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "Abriendo login YouTube en la rocola");
  });

  server.on("/jukebox", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/monitor", HTTP_GET, []() {
    String monHtml = R"rawliteral(
<!DOCTYPE html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HoloDeck // Monitor Serial Wi-Fi</title>
<style>
  body{background:#08080a;color:#00ff66;font-family:monospace;padding:18px;margin:0}
  h3{color:#a855f7;margin:0 0 10px;letter-spacing:.12em}
  #log{background:#000;border:1px solid #222;height:360px;overflow-y:auto;padding:12px;font-size:13px;line-height:1.5;white-space:pre-wrap}
  .bar{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}
  button{background:#15151a;color:#fff;border:1px solid #333;padding:8px 14px;font-family:monospace;cursor:pointer}
  button:hover{border-color:#a855f7;color:#a855f7}
</style></head><body>
<h3>HOLODECK // MONITOR SERIAL WI-FI</h3>
<div id="log">Conectando al WebSocket (:81)...
</div>
<div class="bar">
  <button onclick="send('o')">RELE (o)</button>
  <button onclick="send('+')">ENC UP (+)</button>
  <button onclick="send('-')">ENC DOWN (-)</button>
  <button onclick="send(' ')">CLICK</button>
  <button onclick="send('h')">HOLD MENU (h)</button>
  <button onclick="send('1')">ALBUM 1</button>
  <button onclick="send('2')">ALBUM 2</button>
  <button onclick="send('3')">ALBUM 3</button>
  <button onclick="send('r')" style="color:#ff4444;border-color:#ff4444">RESETEAR TAGS (r)</button>
  <button onclick="document.getElementById('log').textContent=''">LIMPIAR</button>
</div>
<script>
  const logEl = document.getElementById('log');
  const ws = new WebSocket('ws://' + location.hostname + ':81/');
  function append(t){ logEl.textContent += t + '\n'; logEl.scrollTop = logEl.scrollHeight; }
  ws.onopen = () => append('[CONECTADO A ' + location.hostname + ':81]');
  ws.onmessage = (e) => append(e.data.trim());
  ws.onclose = () => append('[DESCONECTADO]');
  function send(c){ ws.send('CMD:' + c); }
</script></body></html>
    )rawliteral";
    server.send(200, "text/html", monHtml);
  });

  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();

    if (ssid.length() == 0) {
      server.send(400, "text/plain", "SSID vacio");
      return;
    }

    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    Serial.printf("[WIFI] Probando conexion a '%s'...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      delay(250);
      Serial.print(".");
    }

    String newIp = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "0.0.0.0";
    Serial.printf("\n[WIFI] Nueva IP obtenida: %s\n", newIp.c_str());

    // Avisar por WebSocket al celular de la rocola (Fully Kiosk) el nuevo Wi-Fi y la nueva IP
    String wsCfg = "WIFI_CFG:" + ssid + "|" + pass + "|" + newIp + "\n";
    broadcastCommand(wsCfg);

    String resp = "<html><body style='background:#000;color:#00ff66;font-family:monospace;padding:30px'>"
                  "<h3>¡CONECTADO!</h3><p>Red: " + ssid + "</p><p>Nueva IP ESP32: " + newIp + "</p>"
                  "<p>Sincronizando celular de la rocola...</p></body></html>";
    server.send(200, "text/html", resp);

    delay(1500);
    if (WiFi.status() == WL_CONNECTED) {
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apModeActive = false;
      MDNS.begin("holojukebox");
      // Actualizar el código/contraseña OTA a la misma clave del nuevo Wi-Fi del QR
      ArduinoOTA.end();
      if (pass.length() > 0) {
        ArduinoOTA.setPassword(pass.c_str());
      }
      ArduinoOTA.begin();
      Serial.printf("[OTA] Reiniciado en nueva red con clave OTA sincronizada al Wi-Fi del QR.\n");
    }
  });

  // Redirección de Portal Cautivo (Android / iOS)
  server.onNotFound([]() {
    if (apModeActive) {
      server.sendHeader("Location", "http://192.168.4.1/setup", true);
      server.send(302, "text/plain", "");
    } else {
      server.send_P(200, "text/html", INDEX_HTML);
    }
  });

  server.begin();
}

void setupOTA(const String& otaPassword) {
  ArduinoOTA.setHostname("holojukebox");
  if (otaPassword.length() > 0) {
    ArduinoOTA.setPassword(otaPassword.c_str());
  }

  ArduinoOTA.onStart([]() {
    Serial.println("\n[OTA] Iniciando actualizacion de firmware...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] ¡Actualizacion completada! Reiniciando...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progreso: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\n[OTA] Error [%u]\n", error);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Listo en 'holojukebox.local' (misma clave que el Wi-Fi activo).");
}

bool tryConnectWifi(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  Serial.printf("[WIFI] Conectando a: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 9000) {
    // Mientras intenta conectar al Wi-Fi, revisamos si ya pasaron los 10s del relé
    if (releActivo && (millis() - releStartTime >= RELE_BOOT_MS)) {
      apagarRele();
      Serial.println("[RELE] 10s cumplidos -> Relé APAGADO.");
    }
    delay(200);
    Serial.print(".");
  }
  return (WiFi.status() == WL_CONNECTED);
}

void connectSavedWifiOrStartAP() {
  prefs.begin("wifi", true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  String activePass = DEFAULT_WIFI_PASS;

  // 1. Si hay un Wi-Fi guardado por el QR, intentar conectarse primero a ese
  if (savedSsid.length() > 0 && tryConnectWifi(savedSsid, savedPass)) {
    activePass = savedPass;
  }
  // 2. Si no hay Wi-Fi del QR (o falló), intentar con la red local por defecto del PC
  else if (tryConnectWifi(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS)) {
    activePass = DEFAULT_WIFI_PASS;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] ¡Conectado con exito!");
    Serial.print("[WIFI] IP ESP32: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin("holojukebox")) {
      Serial.println("[mDNS] Activo en ws://holojukebox.local:81");
    }
    setupOTA(activePass);
  } else {
    Serial.println("\n[WIFI] No se pudo conectar. Activando AP HoloJukebox-Setup...");
    startCaptivePortal();
    setupOTA(AP_PASS);
  }
}

void setup() {
  Serial.begin(115200);

  // 1. ENCENDER RELÉ POR 10 SEGUNDOS APENAS CONECTA LA ESP32
  releStartTime = millis();
  encenderRele();
  Serial.println("\n[RELE] Encendido por 10 segundos para iniciar celular...");

  // 2. CONFIGURAR PINES ENCODER KY-040
  pinMode(PIN_CLK, INPUT_PULLUP);
  pinMode(PIN_DT,  INPUT_PULLUP);
  pinMode(PIN_SW,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), isrEncoder, FALLING);

  // 3. INICIALIZAR LECTOR NFC RC522 POR SPI
  SPI.begin(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SDA);
  rfid.PCD_Init();
  Serial.println("[NFC] Lector RC522 inicializado en SPI (SDA=5, SCK=6, MOSI=7, MISO=10, RST=20).");

  // 4. INICIAR WIFI (QR GUARDADO -> DEFAULT LOCAL -> AP) Y OTA
  connectSavedWifiOrStartAP();

  // 5. INICIAR SERVIDOR WEB Y WEBSOCKET (:81)
  setupWebServer();
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.println("[WS] Servidor WebSocket listo en puerto 81.");
}

void checkNfcReader() {
  unsigned long now = millis();
  if (now - lastNfcScanTime < 120) return;
  lastNfcScanTime = now;

  // Si estamos dentro del período de debounce/cooldown (2 segundos), ignorar lecturas
  if (nfcCooldownUntil > 0 && now < nfcCooldownUntil) {
    return;
  }

  // 1. ¿Hay tarjeta nueva presente?
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // 2. ¿Se pudo leer el número de serie (UID)?
  if (!rfid.PICC_ReadCardSerial()) {
    rfid.PCD_Init(); // Limpieza rápida si la lectura física falló
    return;
  }

  // 3. Extraer UID
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  // Poner la tarjeta en reposo y resetear el receptor RC522 para garantizar que la próxima tarjeta sea detectada siempre
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  rfid.PCD_Init();

  if (uidStr.length() == 0) return;

  // Activar debounce de 2 segundos: solo se introduce una vez y no produce saltos repetidos
  nfcCooldownUntil = now + 2000;

  logWireless("[NFC] Tag detectado UID: " + uidStr);

  // 4. Buscar si este UID ya está asignado a alguno de los 3 álbumes
  int albumAssigned = 0;
  for (int i = 1; i <= 3; i++) {
    String stored = getStoredTagUid(i);
    if (stored.length() > 0 && stored.equalsIgnoreCase(uidStr)) {
      albumAssigned = i;
      break;
    }
  }

  // 5. Si es una tarjeta nueva aún no memorizada, se asigna al primer álbum que esté libre
  if (albumAssigned == 0) {
    for (int i = 1; i <= 3; i++) {
      String stored = getStoredTagUid(i);
      if (stored.length() == 0) {
        storeTagUid(i, uidStr);
        albumAssigned = i;
        logWireless("[NFC] >> NUEVO TAG ASIGNADO: UID " + uidStr + " -> ALBUM " + String(i));
        break;
      }
    }
  }

  // 6. Si ya los 3 álbumes tienen tarjetas asignadas, rota limpiamente
  if (albumAssigned == 0) {
    static int fallbackIdx = 0;
    fallbackIdx = (fallbackIdx % 3) + 1;
    albumAssigned = fallbackIdx;
    logWireless("[NFC] Tag no registrado -> Reproduciendo Album " + String(albumAssigned));
  } else {
    logWireless("[NFC] Reproduciendo ALBUM " + String(albumAssigned));
  }

  // Enviar comando a la pantalla de la rocola
  broadcastCommand("TAG:0" + String(albumAssigned) + "\n");
}

void loop() {
  // 1. Apagar el relé automáticamente al cumplirse los 10 segundos de arranque
  if (releActivo && (millis() - releStartTime >= RELE_BOOT_MS)) {
    apagarRele();
    Serial.println("[RELE] 10 segundos completados -> Relé APAGADO.");
  }

  // 2. Atender OTA, DNS (si está en modo Portal), HTTP y WebSocket
  ArduinoOTA.handle();
  if (apModeActive) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  webSocket.loop();

  // 3. Revisar lector NFC RC522
  checkNfcReader();

  // 4. Procesar giros del Encoder físico
  if (encoderDelta != 0) {
    int delta = encoderDelta;
    encoderDelta = 0;
    if (delta > 0) {
      broadcastCommand("ENC:UP\n");
    } else {
      broadcastCommand("ENC:DOWN\n");
    }
  }

  // 4. Procesar botón del Encoder (SW): Clic corto vs Mantener presionado 1.8s (Menú)
  bool btn = digitalRead(PIN_SW);
  unsigned long now = millis();

  if (btn == LOW && lastButtonState == HIGH) {
    // Recién presionado
    btnPressStart = now;
    btnHoldSent = false;
  } else if (btn == LOW && lastButtonState == LOW) {
    // Sigue presionado: si supera 1.8s, dispara BTN:HOLD inmediatamente
    if (!btnHoldSent && (now - btnPressStart >= BTN_HOLD_MS)) {
      broadcastCommand("BTN:HOLD\n");
      btnHoldSent = true;
    }
  } else if (btn == HIGH && lastButtonState == LOW) {
    // Soltado: si fue una pulsación corta (> 45ms y sin haber disparado HOLD), envía BTN:CLICK
    if (!btnHoldSent && (now - btnPressStart >= 45)) {
      broadcastCommand("BTN:CLICK\n");
    }
  }
  lastButtonState = btn;

  // 5. Comandos por Monitor Serial (para pruebas rápidas)
  if (Serial.available() > 0) {
    char c = Serial.read();
    handleCharCommand(c);
  }
}
