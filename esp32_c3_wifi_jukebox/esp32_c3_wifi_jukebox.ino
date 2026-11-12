/*
 * Holographic Jukebox (HoloDeck) - Firmware Completo ESP32-C3
 * -----------------------------------------------------------
 * Hardware:
 * - Relé de encendido celular (HW-307): GPIO 4 (10s al arrancar, corte por alta impedancia)
 * - Encoder KY-040: CLK = GPIO 0, DT = GPIO 1, SW = GPIO 3
 *
 * Funciones de Red:
 * - Si hay Wi-Fi guardado: Conecta a la red local + mDNS (holojukebox.local) + WebSocket (:81)
 * - Si NO hay Wi-Fi (o falla): Levanta AP "HoloJukebox-Setup" (clave: 12345678, IP: 192.168.4.1)
 *   con Portal Cautivo para elegir red Wi-Fi y sincronizar automáticamente con Fully Kiosk.
 *
 * Librería requerida en Arduino IDE:
 * - "WebSockets" by Markus Sattler (Links2004)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include "index_html.h"

// ================= PINES HARDWARE =================
const int PIN_RELE = 4; // GPIO 4 -> IN del Relé HW-307
const int PIN_CLK  = 0; // GPIO 0 -> Encoder CLK
const int PIN_DT   = 1; // GPIO 1 -> Encoder DT
const int PIN_SW   = 3; // GPIO 3 -> Encoder Pulsador SW

// ================= CONFIGURACIÓN AP / PORTAL =================
const char* AP_SSID = "HoloJukebox-Setup";
const char* AP_PASS = "12345678";
const byte  DNS_PORT = 53;

// ================= OBJETOS GLOBALES =================
Preferences prefs;
WebServer server(80);
WebSocketsServer webSocket(81);
DNSServer dnsServer;

bool apModeActive = false;
bool releActivo = false;
unsigned long releStartTime = 0;
const unsigned long RELE_BOOT_MS = 10000; // 10 segundos al encender

// Variables Encoder
volatile int encoderDelta = 0;
unsigned long lastButtonPress = 0;
bool lastButtonState = HIGH;

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

// ================= ENVÍO WEBSOCKET =================
void broadcastCommand(String cmd) {
  Serial.print("[WS TX] ");
  Serial.print(cmd);
  webSocket.broadcastTXT(cmd);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[WS] Cliente #%u conectado desde %s\n", num, ip.toString().c_str());
    // Informar la IP actual de la ESP32 al conectarse
    String currentIp = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    String msg = "ESP_IP:" + currentIp + "\n";
    webSocket.sendTXT(num, msg);
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
<title>HoloDeck // Panel de Control</title>
<style>
  body{background:#000;color:#fff;font-family:monospace;padding:18px;margin:0}
  .box{max-width:400px;margin:0 auto 18px;border:1px solid #333;padding:18px}
  h2{color:#ff7722;font-size:14px;letter-spacing:.15em;margin:0 0 12px;border-bottom:1px solid #222;padding-bottom:8px}
  label{display:block;font-size:11px;color:#888;margin:12px 0 5px;letter-spacing:.08em}
  select,input,button{width:100%;padding:10px;background:#111;border:1px solid #333;color:#fff;font-family:monospace;box-sizing:border-box;font-size:13px}
  button{background:#ff7722;color:#000;font-weight:bold;border:none;margin-top:14px;cursor:pointer;letter-spacing:.1em}
  button.sec{background:#1a1a1a;color:#fff;border:1px solid #ff7722}
  .sub{font-size:11px;color:#777;margin-top:10px;line-height:1.4}
</style></head><body>

<div class="box">
  <h2>1. CONEXION WI-FI</h2>
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
    <button type="submit">CONECTAR WIFI</button>
  </form>
</div>

<div class="box">
  <h2>2. YOUTUBE MUSIC // DISCOS & CUENTA</h2>
  <form action="/disc" method="POST">
    <label>DISCO A CONFIGURAR (1 AL 5):</label>
    <select name="dnum">
      <option value="1">DISCO 1</option>
      <option value="2">DISCO 2</option>
      <option value="3">DISCO 3</option>
      <option value="4">DISCO 4</option>
      <option value="5">DISCO 5</option>
    </select>
    <label>NOMBRE DEL ARTISTA / ALBUM:</label>
    <input type="text" name="label" placeholder="Ej: DAFT PUNK" required>
    <label>LINK DE ALBUM / PLAYLIST DE YT MUSIC:</label>
    <input type="text" name="url" placeholder="https://music.youtube.com/playlist?list=..." required>
    <button type="submit">GUARDAR EN DISCO</button>
  </form>
  <form action="/ytlogin" method="POST">
    <button type="submit" class="sec">CAMBIAR CUENTA YOUTUBE EN ROCOLA</button>
  </form>
  <div class="sub">Nota: Tu cuenta YouTube Premium queda guardada en Fully Kiosk como fallback sin anuncios.</div>
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

void connectSavedWifiOrStartAP() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length() == 0) {
    Serial.println("[WIFI] Sin red guardada. Iniciando AP HoloJukebox-Setup...");
    startCaptivePortal();
    return;
  }

  Serial.printf("[WIFI] Conectando a red guardada: %s\n", ssid.c_str());
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

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] ¡Conectado con exito!");
    Serial.print("[WIFI] IP ESP32: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin("holojukebox")) {
      Serial.println("[mDNS] Activo en ws://holojukebox.local:81");
    }
  } else {
    Serial.println("\n[WIFI] No se pudo conectar. Activando AP HoloJukebox-Setup...");
    startCaptivePortal();
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

  // 3. INICIAR WIFI O PORTAL CAUTIVO
  connectSavedWifiOrStartAP();

  // 4. INICIAR SERVIDOR WEB Y WEBSOCKET (:81)
  setupWebServer();
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.println("[WS] Servidor WebSocket listo en puerto 81.");
}

void loop() {
  // 1. Apagar el relé automáticamente al cumplirse los 10 segundos de arranque
  if (releActivo && (millis() - releStartTime >= RELE_BOOT_MS)) {
    apagarRele();
    Serial.println("[RELE] 10 segundos completados -> Relé APAGADO.");
  }

  // 2. Atender DNS (si está en modo Portal), HTTP y WebSocket
  if (apModeActive) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  webSocket.loop();

  // 3. Procesar giros del Encoder físico
  if (encoderDelta != 0) {
    int delta = encoderDelta;
    encoderDelta = 0;
    if (delta > 0) {
      broadcastCommand("ENC:UP\n");
    } else {
      broadcastCommand("ENC:DOWN\n");
    }
  }

  // 4. Procesar botón del Encoder (SW)
  bool btn = digitalRead(PIN_SW);
  if (btn == LOW && lastButtonState == HIGH) {
    unsigned long now = millis();
    if (now - lastButtonPress > 200) {
      broadcastCommand("BTN:CLICK\n");
      lastButtonPress = now;
    }
  }
  lastButtonState = btn;

  // 5. Comandos por Monitor Serial (para pruebas rápidas)
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'o' || c == 'O') {
      if (releActivo) {
        apagarRele();
        Serial.println("[MANUAL] Rele APAGADO");
      } else {
        encenderRele();
        Serial.println("[MANUAL] Rele ENCENDIDO");
      }
    } else if (c == 'w' || c == 'W') {
      startCaptivePortal();
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
    }
  }
}
