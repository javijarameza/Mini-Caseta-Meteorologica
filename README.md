# 🌱 Nodo IoT de Monitoreo Ambiental

Estación meteorológica autónoma (energéticamente) diseñada para el monitoreo de microclima y calidad del aire (AQI). Este sistema integra procesamiento de señales en el borde (_Edge Computing_) y transmisión GPRS para telemetría en tiempo real.

## 🛠️ Arquitectura de Hardware

- **Core:** ESP32 (NodeMCU) con gestión de energía en modo _Deep Sleep_.
- **Comunicaciones:** Módulo celular SIM800L (GSM/GPRS).
- **Sensores:**
  - **M702 (UART):** Sensor láser de material particulado (PM2.5, PM10) y gas (CO2, TVOC, CH2O).
  - **SHT31 (I2C):** Sensor de alta precisión para Temperatura y Humedad Relativa.
- **Power Path:** Panel Solar (24V) → Step-Down → TP4056 (Carga Li-ion) → Batería 18650 (2800mAh) → MT3608 (Boost 5V).

## 🧠 Procesamiento de Datos (Edge Computing)

Para garantizar la integridad de los datos antes de la transmisión, el firmware implementa:

1. **Estabilización Térmica:** Rutina de precalentamiento para los elementos sensores MOX y láser.
2. **Limpieza de Ruido (Trimmed Mean):** Captura de 20 muestras, eliminando el 15% de los valores atípicos (outliers) para mitigar interferencias ambientales.
3. **Filtrado Digital (EMA):** Filtro de Media Móvil Exponencial ($\alpha = 0.3$) para suavizar la curva de datos y emular el estándar **NowCast**.

## 🚀 Configuración e Instalación

### 1. Requisitos previos

- **PlatformIO IDE** (VS Code).
- Cuenta en **ThingSpeak** para la visualización de datos.

### 2. Gestión de Credenciales (Seguridad)

Este proyecto utiliza un archivo `secrets.h` para proteger claves de API y configuraciones de red. **Nunca subas tu `secrets.h` real al repositorio.**

1. Localiza el archivo `include/secrets.example.h` en el repositorio.
2. Renómbralo a `include/secrets.h`.
3. Edita el archivo con tus credenciales reales:

   ```c
   const char apn[] = "tu_apn";
   const char *writeAPIKey = "TU_THINGSPEAK_KEY";
   unsigned long channelID = 000000;
   ```

### 3. Despliegue

```bash
# Clonar el repositorio
git clone https://github.com/tu-usuario/nombre-del-repo.git

# Compilar y subir al ESP32
pio run --target upload
```

## 📖 El Cálculo del AQI

El Índice de Calidad del Aire (AQI) se calcula mediante la interpolación lineal de la concentración de PM2.5, siguiendo los estándares de la EPA:

$$AQI = \frac{I_{high} - I_{low}}{C_{high} - C_{low}} (C - C_{low}) + I_{low}$$

**Donde:**

- $C$ = Concentración actual de PM2.5 ($\mu g/m^3$).

- $C_{low}$ y $C_{high}$ = Los límites de concentración mínimo y máximo de la categoría en la que cayó $C$.

- $I_{low}$ y $I_{high}$ = Los valores AQI (0 a 500) que corresponden a esa categoría.

Este cálculo permite transformar microgramos por metro cúbico ($\mu g/m^3$) en una escala comprensible de 0 a 500.

- **0 - 50 (Verde):** Calidad Buena. Ideal para trabajar en el biohuerto.
- **51 - 100 (Amarillo):** Moderada.
- **101 - 150 (Naranja):** Dañina para grupos sensibles.
- **151 - 500 (Rojo/Morado/Marrón):** Insalubre a Peligrosa. Dañino tanto para humanos como para los estomas de las plantas.


## 📊 Visualización en ThingSpeak

Firmware aplicado en prototipo actualmente. Los datos se visualizan en tiempo real en ThingSpeak (temperatura, humedad, PM2.5, PM10, CO2, TVOC, CH2O y AQI):
https://thingspeak.mathworks.com/channels/3290961

---

## ⚖️ Licencia

Este proyecto está bajo la Licencia MIT. Siéntete libre de usarlo, modificarlo y compartirlo.
