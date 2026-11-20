/*
  =====================================================================
   Monitor del calentador solar - Firmware v2 (ESP32 DevKit v1)
  =====================================================================
   - Calibración del nivel guardada en memoria no volátil (NVS).
   - Filtro de mediana para eliminar picos de ruido del ADC.
   - Histéresis de "tanque vacío" para compensar la sonda mojada.
   - Wi-Fi con credenciales guardadas (portal cautivo sólo si falla o
     si se mantiene presionado BOOT al encender).
   - Páginas web para revisar el sistema desde el navegador, con el mismo
     diseño visual que el portal de los compañeros (archivo paginas_web.h).
   - API JSON para la app Delphi (Windows / Android).
   - MODO REPOSO SEGURO: la CPU trabaja fija a 80 MHz desde el arranque
     y en reposo sólo deja de leer los sensores; el Wi-Fi sigue
     completamente activo para que HTTP siempre pueda despertarlo.
   - No utiliza Light Sleep ni Modem Sleep, evitando perder accesibilidad
     desde el navegador o la app Delphi.

   Páginas web (todas despiertan al ESP32):
     GET /             -> Inicio: información del ESP32 y menú
     GET /monitor      -> Temperatura, nivel, gráfica, log y CSV
     GET /calibracion  -> Calibración de vacío y lleno
     GET /energia      -> Estado de reposo, despertar y dormir
     GET /wifi         -> Estado de la red y cambio de red

   Endpoints JSON:
     GET /data                    -> lecturas de sensores en JSON (despierta)
     GET /status                  -> estado del equipo en JSON (despierta)
     GET /wake?min=5              -> mantenerlo despierto N minutos
     GET /sleep                   -> dormir inmediatamente
     GET /calibrate?point=empty   -> guarda ADC actual como "vacío"
     GET /calibrate?point=full    -> guarda ADC actual como "lleno"
     GET /calibrate?point=default -> restaura calibración por defecto
     GET /resetwifi               -> borra la red guardada y reinicia

   Librerías: WiFiManager (tzapu). Core ESP32 2.x o 3.x.

   Cambios de la v2.2.1 respecto a la v2.2.0:
     - Reposo: el ESP32 dejaba de responder a la app y a la web al entrar
       en reposo. La causa era cambiar la CPU de 240 a 80 MHz con
       setCpuFrequencyMhz() con el Wi-Fi encendido. Ahora la CPU se fija a
       80 MHz en setup() antes del Wi-Fi y no se vuelve a cambiar.
     - Sensor de temperatura desconectado: el ADC con atenuación de 11 dB
       se satura cerca de 3.1 V, así que el límite de 3.29 V nunca se
       alcanzaba y se mostraba una temperatura falsa (~ -26 °C). Ahora se
       descarta a partir de 3.0 V o si la temperatura es menor a -10 °C.
     - Reposo automático: después de ~24.8 días encendido (millis() > 2^31)
       el ESP32 dejaba de dormirse solo. La ventana de /wake ahora usa una
       bandera y comparaciones seguras ante el desbordamiento de millis().
     - El SSID se escapa al armar el JSON de /status (comillas o "\" en el
       nombre de la red ya no rompen el JSON).
  =====================================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <math.h>
#include "paginas_web.h"

#define FW_VERSION "2.2.1"

// =====================================================
// PINES
// =====================================================

const int waterLevelPin  = 34;
const int temperaturePin = 35;
const int bootButtonPin  = 0;     // Botón BOOT de la DevKit

// =====================================================
// CALIBRACION DEL NIVEL (valores por defecto)
// =====================================================

const int DEFAULT_EMPTY_ADC = 3400;
const int DEFAULT_FULL_ADC  = 2449;

int emptyAdc = DEFAULT_EMPTY_ADC;
int fullAdc  = DEFAULT_FULL_ADC;

// Histéresis para la sonda mojada
const float EMPTY_ENTER_PCT = 8.0;
const float EMPTY_EXIT_PCT  = 10.0;

const int LEVEL_SAMPLES = 15;     // muestras para la mediana

// =====================================================
// ADC
// =====================================================

const int   ADC_MAX_VALUE   = 4095;
const float ADC_MAX_VOLTAGE = 3.3;

// =====================================================
// TERMISTOR NTC
// =====================================================

const float TEMP_FIXED_RESISTOR     = 10000.0;
const float TEMP_NOMINAL_RESISTANCE = 10000.0;
const float TEMP_NOMINAL_KELVIN     = 298.15;
const float TEMP_BETA               = 3950.0;
const int   TEMP_SAMPLES            = 15;

// Detección de sensor desconectado o en corto.
// Con ADC_11db el ESP32 se satura alrededor de 3.1 V, por eso el límite
// superior es 3.0 V (NTC abierta) y no 3.3 V.
const float TEMP_SHORT_VOLTAGE = 0.05;   // NTC en corto
const float TEMP_OPEN_VOLTAGE  = 3.00;   // NTC desconectada
const float TEMP_MIN_VALID_C   = -10.0;  // el agua del calentador no baja de aquí
const float TEMP_MAX_VALID_C   = 120.0;

// =====================================================
// REPOSO
// =====================================================

const unsigned long IDLE_TIMEOUT_MS   = 60UL * 1000UL;  // 1 min sin peticiones
const unsigned long MAX_WAKE_MINUTES  = 60;

enum PowerState { STATE_ACTIVE, STATE_SLEEP };

PowerState powerState = STATE_ACTIVE;
unsigned long lastActivityMs = 0;
unsigned long awakeUntilMs   = 0;     // fin de la ventana forzada por /wake
bool wakeWindowActive = false;        // true mientras la ventana de /wake está vigente
bool sleepRequested = false;          // /sleep pendiente de aplicar
// El Wi-Fi permanece activo incluso durante el reposo.


// =====================================================
// WIFI, SERVIDOR Y MEMORIA
// =====================================================

WebServer server(80);
WiFiManager wm;
Preferences prefs;

const char* AP_NAME     = "SolarTank-Config";
const char* AP_PASSWORD = "12345678";
const char* MDNS_NAME   = "Calentador";

bool tankEmpty = true;   // estado de la histéresis

// =====================================================
// UTILIDADES
// =====================================================

void sortInts(int* a, int n) {
  for (int i = 1; i < n; i++) {
    int key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = key;
  }
}

// =====================================================
// LECTURA DEL NIVEL DE AGUA (mediana)
// =====================================================

int readWaterLevelADC() {
  int s[LEVEL_SAMPLES];
  for (int i = 0; i < LEVEL_SAMPLES; i++) {
    s[i] = analogRead(waterLevelPin);
    delay(3);
  }
  sortInts(s, LEVEL_SAMPLES);
  return s[LEVEL_SAMPLES / 2];
}

float adcToVoltage(int adcValue) {
  return adcValue * (ADC_MAX_VOLTAGE / ADC_MAX_VALUE);
}

// Porcentaje sin filtrar (0-100)
float rawLevelPercent(int adcValue) {
  float den = (float)(emptyAdc - fullAdc);
  if (den == 0) return NAN;

  float level = ((float)(emptyAdc - adcValue) / den) * 100.0;
  if (level < 0)   level = 0;
  if (level > 100) level = 100;
  return level;
}

// Porcentaje con histéresis de vacío (compensa sonda mojada)
float filteredLevelPercent(float raw) {
  if (isnan(raw)) return NAN;

  if (tankEmpty) {
    if (raw > EMPTY_EXIT_PCT) tankEmpty = false;
  } else {
    if (raw < EMPTY_ENTER_PCT) tankEmpty = true;
  }
  return tankEmpty ? 0.0 : raw;
}

// =====================================================
// LECTURA DE TEMPERATURA NTC (mediana)
// Circuito: 3V3 -- 10k -- GPIO35 -- NTC -- GND
// =====================================================

float readWaterTemperature() {
  int mv[TEMP_SAMPLES];
  for (int i = 0; i < TEMP_SAMPLES; i++) {
    mv[i] = analogReadMilliVolts(temperaturePin);
    delay(3);
  }
  sortInts(mv, TEMP_SAMPLES);

  float voltage = mv[TEMP_SAMPLES / 2] / 1000.0;

  if (voltage <= TEMP_SHORT_VOLTAGE || voltage >= TEMP_OPEN_VOLTAGE) return NAN;

  float resistance = TEMP_FIXED_RESISTOR * voltage / (ADC_MAX_VOLTAGE - voltage);
  if (resistance <= 0) return NAN;

  float inverseKelvin =
      (1.0 / TEMP_NOMINAL_KELVIN) +
      log(resistance / TEMP_NOMINAL_RESISTANCE) / TEMP_BETA;

  if (inverseKelvin <= 0) return NAN;

  float celsius = (1.0 / inverseKelvin) - 273.15;
  if (celsius < TEMP_MIN_VALID_C || celsius > TEMP_MAX_VALID_C) return NAN;
  return celsius;
}

// =====================================================
// GESTIÓN DE ENERGÍA
// =====================================================

// IMPORTANTE:
// No usamos Light Sleep ni Modem Sleep, y tampoco cambiamos la frecuencia
// de la CPU con el Wi-Fi encendido: setCpuFrequencyMhz() cambia el reloj
// "a mano" (este core no tiene activado el administrador de energía de
// ESP-IDF, CONFIG_PM_ENABLE) y el Wi-Fi no se entera del cambio. En la
// v2.2.0 eso dejaba al ESP32 sin responder después de entrar en reposo.
//
// Por eso la CPU se fija UNA sola vez en setup(), antes de iniciar el
// Wi-Fi, y el reposo sólo deja de leer los sensores.

const uint32_t CPU_FREQ_MHZ = 80;   // 80, 160 o 240 (80 = menor consumo)

void enterActive() {
  // Mantener la radio Wi-Fi siempre disponible.
  esp_wifi_set_ps(WIFI_PS_NONE);

  powerState = STATE_ACTIVE;
  Serial.println("[ENERGIA] ACTIVO (lee sensores, Wi-Fi siempre activo)");
}

void enterSleep() {
  // No apagar ni poner en ahorro la radio Wi-Fi.
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Reposo: el loop y el servidor HTTP siguen funcionando igual;
  // simplemente no se leen los sensores hasta la siguiente petición.
  powerState = STATE_SLEEP;
  Serial.println("[ENERGIA] REPOSO (sin lecturas, Wi-Fi activo)");
}

// Se llama en cada petición recibida (excepto /sleep).
void registerActivity() {
  lastActivityMs = millis();

  if (powerState == STATE_SLEEP) {
    Serial.println("[ENERGIA] Peticion HTTP recibida -> despertando");
    enterActive();
  }
}

// ¿Sigue vigente la ventana de /wake? Las restas de unsigned long son
// seguras aunque millis() se desborde (cada ~49.7 días).
bool wakeWindowOpen(unsigned long now) {
  if (!wakeWindowActive) return false;
  if ((long)(awakeUntilMs - now) <= 0) {
    wakeWindowActive = false;
    return false;
  }
  return true;
}

unsigned long awakeRemainingSeconds() {
  if (powerState == STATE_SLEEP) return 0;

  unsigned long now = millis();
  unsigned long idleElapsed = now - lastActivityMs;
  unsigned long remaining =
      (idleElapsed < IDLE_TIMEOUT_MS) ? IDLE_TIMEOUT_MS - idleElapsed : 0;

  if (wakeWindowOpen(now)) {
    unsigned long wakeRemaining = awakeUntilMs - now;
    if (wakeRemaining > remaining) remaining = wakeRemaining;
  }
  return remaining / 1000UL;
}

const char* stateName() {
  return powerState == STATE_ACTIVE ? "activo" : "reposo";
}

// =====================================================
// RESPUESTAS HTTP (JSON)
// =====================================================

void sendJson(int code, const String& json) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", json);
}

String jsonFloat(float v, int decimals) {
  return isnan(v) ? String("null") : String(v, decimals);
}

// Escapa un texto para meterlo entre comillas en un JSON
String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((unsigned char)c < 0x20) {
      char buf[7];
      snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
      out += buf;
    }
    else out += c;
  }
  return out;
}

// Nombre mDNS en minúsculas, por ejemplo "calentador.local"
String mdnsHost() {
  String h = String(MDNS_NAME);
  h.toLowerCase();
  return h + ".local";
}

void handleData() {
  registerActivity();

  int adc = readWaterLevelADC();
  float voltage = adcToVoltage(adc);
  float raw = rawLevelPercent(adc);
  float level = filteredLevelPercent(raw);
  float temp = readWaterTemperature();

  String json = "{";
  json += "\"adc\":" + String(adc) + ",";
  json += "\"voltage\":" + String(voltage, 4) + ",";
  json += "\"level\":" + jsonFloat(level, 2) + ",";
  json += "\"levelRaw\":" + jsonFloat(raw, 2) + ",";
  json += "\"temperatureC\":" + jsonFloat(temp, 2) + ",";
  json += "\"state\":\"" + String(stateName()) + "\",";
  json += "\"awakeRemaining\":" + String(awakeRemainingSeconds()) + ",";
  json += "\"uptime\":" + String(millis() / 1000UL);
  json += "}";

  sendJson(200, json);
}

void handleStatus() {
  registerActivity();

  String json = "{";
  json += "\"fw\":\"" FW_VERSION "\",";
  json += "\"state\":\"" + String(stateName()) + "\",";
  json += "\"sleepMode\":\"cpu_fija_wifi_on\",";
  json += "\"cpuMhz\":" + String(getCpuFrequencyMhz()) + ",";
  json += "\"awakeRemaining\":" + String(awakeRemainingSeconds()) + ",";
  json += "\"idleTimeout\":" + String(IDLE_TIMEOUT_MS / 1000UL) + ",";
  json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
  json += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
  json += "\"mdns\":\"" + mdnsHost() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"emptyAdc\":" + String(emptyAdc) + ",";
  json += "\"fullAdc\":" + String(fullAdc) + ",";
  json += "\"uptime\":" + String(millis() / 1000UL);
  json += "}";

  sendJson(200, json);
}

void handleWake() {
  registerActivity();

  long minutes = 5;
  if (server.hasArg("min")) minutes = server.arg("min").toInt();
  if (minutes < 1) minutes = 1;
  if (minutes > (long)MAX_WAKE_MINUTES) minutes = MAX_WAKE_MINUTES;

  awakeUntilMs = millis() + (unsigned long)minutes * 60000UL;
  wakeWindowActive = true;

  sendJson(200, "{\"ok\":true,\"state\":\"activo\",\"awakeMinutes\":" +
                String(minutes) + "}");
}

void handleSleep() {
  wakeWindowActive = false;
  sleepRequested = true;   // se aplica en loop(), después de responder
  sendJson(200, "{\"ok\":true,\"state\":\"reposo\"}");
}

void handleCalibrate() {
  registerActivity();

  String point = server.arg("point");
  int adc = readWaterLevelADC();

  if (point == "empty") {
    if (adc <= fullAdc + 200) {
      sendJson(400, "{\"ok\":false,\"error\":\"El valor de vacio debe ser mayor que el de lleno\"}");
      return;
    }
    emptyAdc = adc;
    prefs.putInt("emptyAdc", emptyAdc);
    tankEmpty = true;
  } else if (point == "full") {
    if (adc >= emptyAdc - 200) {
      sendJson(400, "{\"ok\":false,\"error\":\"El valor de lleno debe ser menor que el de vacio\"}");
      return;
    }
    fullAdc = adc;
    prefs.putInt("fullAdc", fullAdc);
  } else if (point == "default") {
    emptyAdc = DEFAULT_EMPTY_ADC;
    fullAdc = DEFAULT_FULL_ADC;
    prefs.putInt("emptyAdc", emptyAdc);
    prefs.putInt("fullAdc", fullAdc);
  } else {
    sendJson(400, "{\"ok\":false,\"error\":\"Use point=empty, full o default\"}");
    return;
  }

  sendJson(200, "{\"ok\":true,\"emptyAdc\":" + String(emptyAdc) +
                ",\"fullAdc\":" + String(fullAdc) + "}");
}

void handleResetWifi() {
  sendJson(200, "{\"ok\":true,\"msg\":\"Red borrada. Reiniciando en modo configuracion.\"}");
  delay(500);
  wm.resetSettings();
  ESP.restart();
}

// =====================================================
// PÁGINAS WEB (el HTML está en paginas_web.h)
// =====================================================

// Arma la página: encabezado + estilos + JS común + título + menú + contenido
void enviarPagina(const char* titulo, const char* encabezado, const char* cuerpo) {
  registerActivity();

  String html;
  html.reserve(strlen(WEB_CSS) + strlen(WEB_MENU) + strlen(WEB_JS_COMUN) +
               strlen(cuerpo) + 400);

  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'><title>");
  html += titulo;
  html += F("</title>");
  html += WEB_CSS;
  html += WEB_JS_COMUN;
  html += F("</head><body><h1>");
  html += encabezado;
  html += F("</h1>");
  html += WEB_MENU;
  html += cuerpo;
  html += F("</body></html>");

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", html);
}

void handlePaginaInicio() {
  enviarPagina("Calentador solar", "ESP32 - Monitor del Calentador Solar", WEB_INICIO);
}

void handlePaginaMonitor() {
  enviarPagina("Monitor", "Temperatura y Nivel de Agua", WEB_MONITOR);
}

void handlePaginaCalibracion() {
  enviarPagina("Calibración", "Calibración de la Sonda", WEB_CALIBRACION);
}

void handlePaginaEnergia() {
  enviarPagina("Energía", "Energía y Modo de Reposo", WEB_ENERGIA);
}

void handlePaginaWifi() {
  enviarPagina("Configuración WiFi", "Configuración WiFi", WEB_WIFI);
}

void handleNotFound() {
  registerActivity();
  sendJson(404, "{\"ok\":false,\"error\":\"Ruta no encontrada\"}");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  // La frecuencia de la CPU se fija aquí, ANTES de iniciar el Wi-Fi,
  // y ya no se cambia (ver GESTIÓN DE ENERGÍA).
  setCpuFrequencyMhz(CPU_FREQ_MHZ);

  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("==================================");
  Serial.println(" Monitor del calentador solar v" FW_VERSION);
  Serial.println("==================================");

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(waterLevelPin, ADC_11db);
  analogSetPinAttenuation(temperaturePin, ADC_11db);
  pinMode(waterLevelPin, INPUT);
  pinMode(temperaturePin, INPUT);
  pinMode(bootButtonPin, INPUT_PULLUP);

  // Calibración guardada
  prefs.begin("calentador", false);
  emptyAdc = prefs.getInt("emptyAdc", DEFAULT_EMPTY_ADC);
  fullAdc  = prefs.getInt("fullAdc", DEFAULT_FULL_ADC);
  Serial.printf("Calibracion: vacio=%d lleno=%d\n", emptyAdc, fullAdc);

  // ---------------- Wi-Fi ----------------
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalTimeout(180);   // 3 min para configurar
  wm.setConnectTimeout(20);

  bool connected;

  if (digitalRead(bootButtonPin) == LOW) {
    Serial.println("BOOT presionado: abriendo portal de configuracion.");
    connected = wm.startConfigPortal(AP_NAME, AP_PASSWORD);
  } else {
    Serial.println("Conectando a la red guardada...");
    Serial.printf("Si no hay red guardada, conectate a %s (pass %s) y abre http://192.168.4.1\n",
                  AP_NAME, AP_PASSWORD);
    connected = wm.autoConnect(AP_NAME, AP_PASSWORD);
  }

  if (!connected) {
    Serial.println("No se pudo conectar. Reiniciando...");
    delay(1000);
    ESP.restart();
  }

  WiFi.setAutoReconnect(true);

  Serial.print("Red: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://" + mdnsHost());
  }

  // ---------------- Rutas ----------------
  // Páginas web
  server.on("/", handlePaginaInicio);
  server.on("/monitor", handlePaginaMonitor);
  server.on("/calibracion", handlePaginaCalibracion);
  server.on("/energia", handlePaginaEnergia);
  server.on("/wifi", handlePaginaWifi);
  // API JSON (app Delphi y páginas web)
  server.on("/data", handleData);
  server.on("/status", handleStatus);
  server.on("/wake", handleWake);
  server.on("/sleep", handleSleep);
  server.on("/calibrate", handleCalibrate);
  server.on("/resetwifi", handleResetWifi);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Servidor iniciado: abre http://" + WiFi.localIP().toString() + " o http://" + mdnsHost());

  // Estrategia estable: Wi-Fi siempre activo; solo baja la CPU en reposo.
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.printf("Ahorro seguro: CPU fija a %u MHz, Wi-Fi siempre activo.\n",
                (unsigned)getCpuFrequencyMhz());

  lastActivityMs = millis();
  enterActive();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  // Debe ejecutarse continuamente para aceptar conexiones HTTP incluso
  // cuando el equipo está en reposo.
  server.handleClient();

  unsigned long now = millis();

  if (powerState == STATE_ACTIVE) {
    bool idle = (now - lastActivityMs) > IDLE_TIMEOUT_MS;
    bool windowOver = !wakeWindowOpen(now);

    if (sleepRequested || (idle && windowOver)) {
      sleepRequested = false;
      enterSleep();
    }
  } else {
    // Si llega una petición, server.handleClient() la atenderá y
    // registerActivity() cambiará el estado a ACTIVO.
    sleepRequested = false;
  }

  // Pausa corta para no bloquear la atención de solicitudes HTTP.
  delay(2);
}