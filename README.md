# SolarTankMonitor · Monitor del calentador solar

Sistema de monitoreo de **temperatura y nivel de agua** para un calentador solar doméstico. Usa una sonda LJXH de 4 hilos, un ESP32 DevKit v1, una **app multiplataforma** (Windows y Android) hecha en Delphi/FireMonkey y **páginas web** servidas por el propio ESP32.

Equipo: Rodríguez Martínez Alan Mitchel · Segura Ortiz Juan Manuel · Castañón Haro Jesús Isaac
Instituto Tecnológico de Tlajomulco, Ingeniería en Sistemas Computacionales.

## Guía rápida para revisar el proyecto

**Novedades de la versión 2.2.0**
- Páginas web servidas por el ESP32, con el mismo diseño visual que el portal de los compañeros: Inicio, Monitor, Calibración, Energía y WiFi (ver [4. Páginas web](#4-páginas-web)).
- Modo de reposo seguro: en reposo el CPU baja a 80 MHz y el Wi-Fi sigue encendido, así que el ESP32 siempre responde (ver [2. Modo de reposo](#2-modo-de-reposo)).
- `/status` ahora incluye gateway y máscara de red.
- Se agregó `app/AndroidManifest.template.xml`, necesario para que la app de Android se conecte al ESP32 por HTTP.

**1. Descargar**
1. En esta página, presiona el botón verde **Code → Download ZIP** y descomprime el archivo.
2. Usa la carpeta `SolarTankMonitor/` **completa**: contiene `SolarTankMonitor.ino` y `paginas_web.h`, y los dos deben quedar juntos en la misma carpeta. Si solo se descarga el `.ino`, no compila.

**2. Cargar el firmware al ESP32**
1. En Arduino IDE 2, instala el soporte para **esp32 (Espressif Systems)** y la librería **WiFiManager (tzapu)**.
2. Abre `SolarTankMonitor/SolarTankMonitor.ino`. Deben aparecer dos pestañas: el `.ino` y `paginas_web.h`.
3. Elige la placa **ESP32 Dev Module** y el puerto **COMx**, y presiona **Subir**.
4. Conéctate a la red **SolarTank-Config** (contraseña `12345678`), abre `http://192.168.4.1` y elige la red Wi-Fi.

Los pasos detallados están en [5. Cargar el firmware](#5-cargar-el-firmware).

**3. Revisar las páginas web**
1. Abre el Monitor serie a 115200 baudios y copia la IP que muestra.
2. Desde un navegador **en la misma red**, abre esa IP o `http://nivel.local`.
3. Recorre el menú: **Inicio, Monitor, Calibración, Energía y WiFi**. Qué revisar en cada una está en [6. Prueba rápida](#6-prueba-rápida).

**4. Revisar la app**
- **Android:** instala `Monitor tanque solar.apk` (está en la raíz del repositorio), escribe la IP del ESP32 y presiona **Monitorear**.
- **Windows:** abre `app/MonitorCalentador.dproj` en RAD Studio 13 (Community) y ejecútalo con la plataforma *Windows 32-bit*.

Más detalles en [7. App](#7-app-rad-studio-multi-device-application).

## Estructura del repositorio

```
SolarTankMonitor/
├── SolarTankMonitor.ino        Firmware del ESP32 (lectura, API JSON, reposo, servidor web)
└── paginas_web.h               HTML/CSS/JS de las páginas web (se compila junto con el .ino)
app/
├── MonitorCalentador.dpr/.dproj  Proyecto Delphi (Multi-Device Application)
├── uPrincipal.pas / .fmx         Lógica e interfaz de la app
└── AndroidManifest.template.xml  Plantilla del manifiesto (permite HTTP en red local)
Monitor tanque solar.apk         App compilada para Android
```

## Conexiones

| Sonda / componente | ESP32 |
|---|---|
| Rojo (nivel) + resistencia de 10 kΩ a 3V3 | GPIO34 |
| Verde (NTC) + resistencia de 10 kΩ a 3V3 | GPIO35 |
| Azul (nivel) y blanco (NTC) | GND |

- Usa solo 3.3 V.
- GPIO34 y GPIO35 pertenecen al ADC1, que funciona con el Wi-Fi activo.
- Si el nivel marca valores raros, intercambia rojo y azul.

## 1. Diagnóstico de la práctica (calentador_solar.csv)

| Momento | ADC | Nivel que mostraba la v1 |
|---|---|---|
| Vacío seco (09:27–09:30) | ~3399 | 4 % |
| Lleno (09:31:40) | ~2376 | 100 % |
| Vaciado, pausa (09:32:53) | ~3310 | 13 % |
| Vacío tras vaciar (09:33:20 →) | ~3380 | 6 % (se quedaba ahí) |

- **Calibración:** `EMPTY_ADC = 3440`, pero el vacío real es ~3400. Por eso nunca bajaba de ~4 %.
- **Sonda mojada:** después de vaciar, la sonda queda ~20 cuentas por debajo del vacío seco por la película de agua. Es un efecto físico, no un error de código.
- **Ruido:** había picos aislados (por ejemplo, 3270 con el tanque vacío) que el promedio no eliminaba.

**Correcciones en la v2**

- Vacío recalibrado a 3400.
- Filtro de mediana de 15 muestras para eliminar los picos.
- Histéresis: el tanque se considera vacío por debajo de 8 % y deja de considerarse vacío por encima de 10 %.

Con los datos del CSV, la v2 marca 0 % al final del vaciado; la v1 marcaba 6 %.

## 2. Modo de reposo

En *deep sleep* el radio Wi-Fi se apaga, así que el ESP32 ya no podría recibir peticiones. Por eso el firmware usa un **reposo seguro**:

| Estado | CPU | Wi-Fi | Lectura de sensores |
|---|---|---|---|
| ACTIVO | 240 MHz | Siempre encendido (`WIFI_PS_NONE`) | Al recibir peticiones |
| REPOSO | 80 MHz | Siempre encendido (`WIFI_PS_NONE`) | No lee |

- **Cuándo entra en reposo:** tras 60 s sin peticiones (`IDLE_TIMEOUT_MS`), o con `/sleep`.
- **Cómo se despierta:** con **cualquier petición HTTP**. Se atiende de inmediato porque el Wi-Fi nunca se apaga. Con `/wake?min=N` además se mantiene activo N minutos.
- **Por qué no se usa modem sleep ni light sleep:** se probó apagar el radio entre beacons y el ESP32 dejaba de responder bien a las páginas web. Por eso solo se reduce la frecuencia del CPU.

## 3. API JSON del ESP32

| Ruta | Función |
|---|---|
| `/data` | Lecturas: `adc`, `voltage`, `level`, `levelRaw`, `temperatureC`, `state`, `awakeRemaining`, `uptime` |
| `/status` | Firmware, estado, modo de reposo (`sleepMode`), SSID, IP, MAC, gateway, máscara, RSSI, calibración, `uptime` |
| `/wake?min=5` | Despierta y mantiene activo N minutos (máximo 60) |
| `/sleep` | Reposo inmediato |
| `/calibrate?point=empty\|full\|default` | Guarda el ADC actual como vacío o lleno, o restaura los valores por defecto (3400 / 2449). Se guarda en NVS. |
| `/resetwifi` | Borra la red guardada y reinicia en modo configuración |

La app de Delphi y las páginas web usan **la misma API**.

## 4. Páginas web

Se abren desde un navegador **en la misma red** que el ESP32, en `http://<IP del ESP32>` o `http://nivel.local`. Tienen el mismo diseño visual que el portal de los compañeros del docente: fondo gris, tarjetas blancas, botones azules `#1976D2`, verdes para calibrar, rojos para acciones peligrosas y barra de menú.

| Página | Contenido |
|---|---|
| `/` Inicio | Información del ESP32 (firmware, MAC, red, IP, RSSI, mDNS), menú y estado de energía |
| `/monitor` | Temperatura con su estado y termómetro; nivel con valor sin filtro, ADC y voltaje; gráfica; log en vivo cada 2 s con pausa; descarga en CSV |
| `/calibracion` | Lectura actual, valores guardados, botones para guardar VACÍO/LLENO y restaurar valores por defecto |
| `/energia` | Estado de reposo, tiempo restante, despertar N minutos y dormir ahora |
| `/wifi` | Datos de la red y botón para borrar la red guardada |

### Cómo se hizo

1. **Servidor web.** El ESP32 usa la librería `WebServer` en el puerto 80. Cada ruta tiene su función (`handlePaginaInicio`, `handlePaginaMonitor`, etc.) registrada en `setup()` con `server.on(...)`.
2. **HTML separado del código.** Las páginas están en `paginas_web.h` como texto guardado en flash (`PROGMEM`), para no gastar RAM:
   - `WEB_CSS`: estilos.
   - `WEB_MENU`: barra de menú.
   - `WEB_JS_COMUN`: funciones JavaScript comunes.
   - `WEB_INICIO`, `WEB_MONITOR`, `WEB_CALIBRACION`, `WEB_ENERGIA`, `WEB_WIFI`: contenido de cada página.
3. **Armado de la página.** `enviarPagina()` une encabezado, estilos, JavaScript común, título, menú y contenido, y lo envía al navegador.
4. **Datos con JavaScript.** Las páginas llegan sin datos. En el navegador, `fetch()` consulta la API: Inicio, Energía y WiFi leen `/status`, Monitor lee `/data` cada 2 s, y Calibración usa `/calibrate`. Así la web y la app muestran exactamente lo mismo.
5. **Historial y CSV en el navegador.** Las lecturas del Monitor se guardan en `sessionStorage` (máximo 2000) y el CSV se genera en el navegador. El ESP32 no guarda historial, para no tener que leer la sonda todo el tiempo.
6. **Relación con el reposo.** Cada página o consulta cuenta como actividad. Mientras el Monitor está abierto, el ESP32 se mantiene activo. Si se cierra la página o se pausan las lecturas, al minuto entra en reposo.

## 5. Cargar el firmware

**Preparar Arduino IDE 2 (una sola vez):**
1. Ve a *Archivo > Preferencias* y, en "URLs adicionales de gestor de placas", agrega:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. En *Herramientas > Placa > Gestor de placas*, instala **esp32** (Espressif Systems).
3. En *Herramientas > Administrar bibliotecas*, instala **WiFiManager** (tzapu).

**Cargar:**
1. Abre `SolarTankMonitor/SolarTankMonitor.ino`. Deben aparecer dos pestañas: el `.ino` y `paginas_web.h`. La carpeta y el `.ino` deben llamarse igual.
2. Elige la placa **ESP32 Dev Module** y el puerto **COMx**, y presiona **Subir**. Si se queda en "Connecting…", mantén presionado **BOOT**.

**Primer arranque:**
1. Abre el Monitor serie a **115200 baudios**.
2. Conéctate a la red **SolarTank-Config** (contraseña `12345678`).
3. Abre `http://192.168.4.1` y elige tu red.
4. El Monitor serie muestra `Servidor iniciado: abre http://<IP> o http://nivel.local`. Conviene reservar esa IP en el router.

**Cambiar de red:** mantén presionado **BOOT** al encender, o usa el botón de la página `/wifi`.

## 6. Prueba rápida

- **Monitor:** la temperatura y el nivel se actualizan cada 2 s y la tabla crece.
- **Energía:** "Dormir ahora" muestra en el Monitor serie `[ENERGIA] REPOSO SEGURO`. Al abrir cualquier página aparece `Peticion HTTP recibida -> despertando`.
- **Calibración:** con el tanque vacío presiona "Guardar VACÍO"; con el tanque lleno, "Guardar LLENO".
- **CSV:** "Descargar CSV" genera un archivo que abre en Excel.

## 7. App (RAD Studio, Multi-Device Application)

- Abre `app/MonitorCalentador.dproj`.
- Plataformas: **Windows 32/64 bits** y **Android 64 bits**.
- Escribe la IP del ESP32 y pulsa **Monitorear**. La app consulta `/data` cada 2 s, y mientras lo hace el ESP32 permanece activo.

| Control | Qué hace |
|---|---|
| **Despertar (5 min)** | Envía `/wake` |
| **Dormir ahora** | Detiene el sondeo y envía `/sleep` |
| **Calibración** | Con el tanque realmente vacío o lleno, guarda ese punto en el ESP32 |
| **Exportar CSV** | En Windows abre el cuadro "Guardar como". En Android guarda en `Android/data/<paquete>/files`. |

- En Android, al mandar la app a segundo plano se pausa el sondeo para que el ESP32 pueda dormir.

### Android (configurado y probado en un Redmi 2201117TL)

- **Java:** RAD Studio usa el JDK 22 de `C:\Program Files\Java\jdk-22`. Está configurado en *Tools > Options > Deployment > SDK Manager > Java* y en la variable `JAVA_HOME` (*Tools > Options > IDE > Environment Variables*).
- **Proyecto:** el `.dproj` tiene la configuración de Android 64-bit:
  - minSdk 23, targetSdk 35
  - Permisos: Internet y estado de la red
  - Paquete: `com.ittj.MonitorCalentador`
- **Manifiesto:** `app/AndroidManifest.template.xml` incluye `android:usesCleartextTraffic="true"`. Android bloquea HTTP sin cifrar por defecto, así que sin esto la app no se conecta al ESP32. Si se borra el template, hay que volver a agregarlo.
- **Instalar en el celular:** selecciona *Android 64-bit* y el dispositivo, y presiona **Ctrl+Shift+F9**.
  - En Xiaomi/Redmi hay que activar "Instalar vía USB" y aceptar el aviso a tiempo.
  - También se puede instalar a mano el APK de `app\MonitorCalentador\bin\MonitorCalentador.apk` (o el `Monitor tanque solar.apk` de la raíz del repositorio).
- `nivel.local` (mDNS) no funciona en todos los teléfonos. Usa la IP.
