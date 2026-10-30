# Monitor del calentador solar: firmware v2 y app multiplataforma

```
Firmware/monitor_calentador_solar_v2/monitor_calentador_solar_v2.ino   (ESP32)
App/MonitorCalentador.dpr, uPrincipal.pas, uPrincipal.fmx              (Delphi FMX)
```

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

## 2. Reposo y despertar por Wi-Fi

En *deep sleep* el radio Wi-Fi está apagado, así que el ESP32 no puede recibir peticiones. Por eso se usa **reposo con Wi-Fi asociado**:

| Estado | CPU | Wi-Fi | Respuesta |
|---|---|---|---|
| ACTIVO | 240 MHz | Sin ahorro (`WIFI_PS_NONE`) | Inmediata |
| REPOSO | Light sleep automático (o 80 MHz si el core no lo soporta) | `WIFI_PS_MAX_MODEM`: el radio se apaga entre beacons del router | Unos cientos de ms |

- **Cómo entra en reposo:** pasan 60 s sin peticiones (`IDLE_TIMEOUT_MS`) o la app envía `/sleep`.
- **Cómo despierta:** con **cualquier petición HTTP** que llegue por Wi-Fi. `/wake?min=N` además lo mantiene activo N minutos.
- **Qué modo usa:** el monitor serie indica si el core permite *light sleep* automático o si usa *modem sleep*.

## 3. API del ESP32

| Ruta | Función |
|---|---|
| `/data` | Lecturas: `adc`, `voltage`, `level`, `levelRaw`, `temperatureC`, `state`, `awakeRemaining` |
| `/status` | Firmware, estado, modo de reposo, SSID, IP, RSSI, calibración |
| `/wake?min=5` | Despierta y mantiene activo N minutos (máximo 60) |
| `/sleep` | Reposo inmediato |
| `/calibrate?point=empty\|full\|default` | Guarda el ADC actual como vacío o lleno, o restaura los valores por defecto (se conserva al reiniciar) |
| `/resetwifi` | Borra la red guardada y reinicia en modo configuración |

## 4. Cargar el firmware

1. En Arduino IDE instala el core **esp32 (Espressif)** y la librería **WiFiManager (tzapu)**.
2. Elige la placa *ESP32 Dev Module* y carga el .ino.
3. **Primer arranque:**
   1. Conéctate a la red `ESP32-Nivel` (contraseña `12345678`).
   2. Abre http://192.168.4.1 y elige tu red.
   3. Desde entonces se conecta solo.
4. **Para cambiar de red:** mantén presionado **BOOT** mientras lo energizas, o usa `/resetwifi`.
5. La IP aparece en el monitor serie a 115200 baudios. Conviene reservarla en el router para que no cambie.

## 5. App (RAD Studio, Multi-Device Application)

- Abre `App/MonitorCalentador.dproj`.
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
- **Manifiesto:** `AndroidManifest.template.xml` incluye `android:usesCleartextTraffic="true"`. Android bloquea HTTP sin cifrar por defecto, así que sin esto la app no se conecta al ESP32. Si se borra el template, hay que volver a agregarlo.
- **Instalar en el celular:** selecciona *Android 64-bit* y el dispositivo, y presiona **Ctrl+Shift+F9**.
  - En Xiaomi/Redmi hay que activar "Instalar vía USB" y aceptar el aviso a tiempo.
  - También se puede instalar a mano el APK de `App\MonitorCalentador\bin\MonitorCalentador.apk`.
- `nivel.local` (mDNS) no funciona en todos los teléfonos. Usa la IP.
