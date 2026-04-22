#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <LiquidCrystal_I2C.h>
#include "M702.h"
#include "secrets.h"

// 1. Configuración de TinyGSM
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <ThingSpeak.h>

// 2. Mapeo de Arquitectura
#define RXD1_SIM800 26
#define TXD1_SIM800 27
#define RXD2_M702 16
#define TXD2_M702 17
#define PIN_POWER_SHT31 25
#define PIN_MOSFET 4

// 3. Parámetros de Configuración
#define TIEMPO_DEEP_SLEEP_MINUTOS 15
#define TIEMPO_CALENTAMIENTO_MS 180000
#define NUM_MUESTRAS 20
#define RECORTES_POR_LADO 3
#define ALPHA_EMA 0.3

// 4. Bóveda de Memoria RTC (Filtro EMA)
RTC_DATA_ATTR float rtc_temp = 0.0;
RTC_DATA_ATTR float rtc_hum = 0.0;
RTC_DATA_ATTR float rtc_co2 = 0.0;
RTC_DATA_ATTR float rtc_pm25 = 0.0;
RTC_DATA_ATTR float rtc_pm10 = 0.0;
RTC_DATA_ATTR float rtc_tvoc = 0.0;
RTC_DATA_ATTR float rtc_ch2o = 0.0;
RTC_DATA_ATTR int rtc_aqi = 0;
RTC_DATA_ATTR bool primera_vez = true;

// 5. Instancias Globales
HardwareSerial SerialSIM800(1);
HardwareSerial SerialSensor(2);
TinyGsm modem(SerialSIM800);
TinyGsmClient client(modem);
M702 miSensorAmbiental(SerialSensor);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
LiquidCrystal_I2C lcd(0x27, 20, 4);

unsigned long tiempo_arranque_energia = 0;

// --- FUNCIONES DE FILTRADO Y MATEMÁTICAS ---

int calcularAQI_PM25(float conc)
{
  float C_low, C_high, I_low, I_high;
  if (conc <= 12.0)
  {
    C_low = 0.0;
    C_high = 12.0;
    I_low = 0;
    I_high = 50;
  }
  else if (conc <= 35.4)
  {
    C_low = 12.1;
    C_high = 35.4;
    I_low = 51;
    I_high = 100;
  }
  else if (conc <= 55.4)
  {
    C_low = 35.5;
    C_high = 55.4;
    I_low = 101;
    I_high = 150;
  }
  else if (conc <= 150.4)
  {
    C_low = 55.5;
    C_high = 150.4;
    I_low = 151;
    I_high = 200;
  }
  else if (conc <= 250.4)
  {
    C_low = 150.5;
    C_high = 250.4;
    I_low = 201;
    I_high = 300;
  }
  else
  {
    C_low = 250.5;
    C_high = 500.4;
    I_low = 301;
    I_high = 500;
  }
  return round(((I_high - I_low) / (C_high - C_low)) * (conc - C_low) + I_low);
}

int calcularMediaTruncadaInt(int *array, int validCount)
{
  if (validCount <= RECORTES_POR_LADO * 2)
    return 0;
  for (int i = 0; i < validCount - 1; i++)
  {
    for (int j = 0; j < validCount - i - 1; j++)
    {
      if (array[j] > array[j + 1])
      {
        int temp = array[j];
        array[j] = array[j + 1];
        array[j + 1] = temp;
      }
    }
  }
  long sum = 0;
  for (int i = RECORTES_POR_LADO; i < validCount - RECORTES_POR_LADO; i++)
  {
    sum += array[i];
  }
  return sum / (validCount - (RECORTES_POR_LADO * 2));
}

float calcularMediaTruncadaFloat(float *array, int validCount)
{
  if (validCount <= RECORTES_POR_LADO * 2)
    return 0.0;
  for (int i = 0; i < validCount - 1; i++)
  {
    for (int j = 0; j < validCount - i - 1; j++)
    {
      if (array[j] > array[j + 1])
      {
        float temp = array[j];
        array[j] = array[j + 1];
        array[j + 1] = temp;
      }
    }
  }
  float sum = 0;
  for (int i = RECORTES_POR_LADO; i < validCount - RECORTES_POR_LADO; i++)
  {
    sum += array[i];
  }
  return sum / (validCount - (RECORTES_POR_LADO * 2));
}

void actualizarLCD(float t, float h, int co2, int aqi, int pm25, int pm10, int tvoc, int ch2o)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("T:%.1f C  H:%.1f %%", t, h);
  lcd.setCursor(0, 1);
  lcd.printf("CO2:%d ppm AQI:%d", co2, aqi);
  lcd.setCursor(0, 2);
  lcd.printf("PM2.5:%d  PM10:%d", pm25, pm10);
  lcd.setCursor(0, 3);
  lcd.printf("TVOC:%d  CH2O:%d", tvoc, ch2o);
}

void irADeepSleep()
{
  Serial.println("[Energía] Ciclo completado. Entrando en Deep Sleep...");
  digitalWrite(PIN_MOSFET, LOW);
  digitalWrite(PIN_POWER_SHT31, LOW);

  esp_sleep_enable_timer_wakeup(TIEMPO_DEEP_SLEEP_MINUTOS * 60 * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

// --- FLUJO PRINCIPAL ---

void setup()
{
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_MOSFET, OUTPUT);
  pinMode(PIN_POWER_SHT31, OUTPUT);

  Serial.println("\n[Sistema] Despertar por Temporizador. Iniciando...");

  // 1. Dar energía a todo físicamente (Inicia el ventilador del M702)
  digitalWrite(PIN_MOSFET, HIGH);
  digitalWrite(PIN_POWER_SHT31, HIGH);
  tiempo_arranque_energia = millis(); // Marcamos el "minuto cero"
  delay(100);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  // 2. Verificar causa de arranque para mostrar mensaje adecuado en LCD
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER)
  {
    // Arranque en frío (primera vez que le conectas la batería)
    lcd.setCursor(0, 1);
    lcd.print("Iniciando Sistema...");
  }
  else
  {
    // Arranque por temporizador (ciclos posteriores) - Mostrar última lectura (EMA)
    actualizarLCD(rtc_temp, rtc_hum, round(rtc_co2), rtc_aqi, round(rtc_pm25), round(rtc_pm10), round(rtc_tvoc), round(rtc_ch2o));
  }

  SerialSIM800.begin(9600, SERIAL_8N1, RXD1_SIM800, TXD1_SIM800);
  miSensorAmbiental.begin(RXD2_M702, TXD2_M702);

  ThingSpeak.begin(client);
  sht31.begin(0x44);
}

void loop()
{
  // --- BUCLE DE ESPERA FIJA CON ENCENDIDO RELATIVO DEL SIM ---
  Serial.printf("[Sensor] Iniciando cuenta regresiva de %d segundos para precalentamiento...\n", TIEMPO_CALENTAMIENTO_MS / 1000);

  bool red_conectada = false;

  // Cálculo de seguridad: Si el calentamiento es mayor a 1 minuto, enciende 60s antes.
  // Si es menor a 1 minuto, enciende en el segundo 0 (de inmediato).
  unsigned long umbral_encendido_sim = (TIEMPO_CALENTAMIENTO_MS > 60000) ? (TIEMPO_CALENTAMIENTO_MS - 60000) : 0;

  while (millis() - tiempo_arranque_energia < TIEMPO_CALENTAMIENTO_MS)
  {
    unsigned long tiempo_transcurrido = millis() - tiempo_arranque_energia;

    // Encendido dinámico: Faltando exactamente 1 minuto (o inmediato si el ciclo es muy corto)
    if (tiempo_transcurrido >= umbral_encendido_sim && !red_conectada)
    {
      Serial.print("\n[Red] Tiempo umbral alcanzado (60s antes de finalizar). Despertando módem y buscando Claro... ");
      modem.restart();
      if (modem.waitForNetwork(60000) && modem.gprsConnect(apn, gprsUser, gprsPass))
      {
        Serial.println("OK! Red lista en segundo plano.");
      }
      else
      {
        Serial.println("Error al conectar a la red.");
      }
      red_conectada = true;
    }

    // Mantenemos limpio el buffer del M702 vaciándolo continuamente durante la espera
    while (SerialSensor.available())
    {
      SerialSensor.read();
    }

    // Pequeño retardo para no colapsar el procesador (Watchdog)
    delay(50);
  }

  Serial.println("\n[Sensor] Precalentamiento cumplido. Capturando Muestras Oficiales...");

  // --- ARRAYS PARA MEDIA TRUNCADA (INALTERADO) ---
  int arr_co2[NUM_MUESTRAS], arr_ch2o[NUM_MUESTRAS], arr_tvoc[NUM_MUESTRAS];
  int arr_pm25[NUM_MUESTRAS], arr_pm10[NUM_MUESTRAS];
  float arr_temp[NUM_MUESTRAS], arr_hum[NUM_MUESTRAS];
  int validM702 = 0, validSHT31 = 0;

  for (int i = 0; i < NUM_MUESTRAS; i++)
  {
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if (!isnan(t) && !isnan(h))
    {
      arr_temp[validSHT31] = t;
      arr_hum[validSHT31] = h;
      validSHT31++;
    }

    bool leidoM702 = false;
    unsigned long timeoutM702 = millis() + 3000;
    while (millis() < timeoutM702)
    {
      if (miSensorAmbiental.readSensor())
      {
        leidoM702 = true;
        break;
      }
    }

    if (leidoM702)
    {
      arr_co2[validM702] = miSensorAmbiental.CO2;
      arr_ch2o[validM702] = miSensorAmbiental.CH2O;
      arr_tvoc[validM702] = miSensorAmbiental.TVOC;
      arr_pm25[validM702] = miSensorAmbiental.PM25;
      arr_pm10[validM702] = miSensorAmbiental.PM10;
      validM702++;
      Serial.print(".");
    }
  }
  Serial.println("\n[Sistema] Muestreo Completado!");

  // --- CÁLCULO, EMA Y ACTUALIZACIÓN (INALTERADO) ---
  if (validM702 > (RECORTES_POR_LADO * 2) && validSHT31 > (RECORTES_POR_LADO * 2))
  {

    // 1. Obtener promedios truncados brutos
    int crudo_co2 = calcularMediaTruncadaInt(arr_co2, validM702);
    int crudo_ch2o = calcularMediaTruncadaInt(arr_ch2o, validM702);
    int crudo_tvoc = calcularMediaTruncadaInt(arr_tvoc, validM702);
    int crudo_pm25 = calcularMediaTruncadaInt(arr_pm25, validM702);
    int crudo_pm10 = calcularMediaTruncadaInt(arr_pm10, validM702);
    float crudo_temp = calcularMediaTruncadaFloat(arr_temp, validSHT31);
    float crudo_hum = calcularMediaTruncadaFloat(arr_hum, validSHT31);

    // 2. Aplicar Filtro EMA
    if (primera_vez)
    {
      rtc_co2 = crudo_co2;
      rtc_ch2o = crudo_ch2o;
      rtc_tvoc = crudo_tvoc;
      rtc_pm25 = crudo_pm25;
      rtc_pm10 = crudo_pm10;
      rtc_temp = crudo_temp;
      rtc_hum = crudo_hum;
      primera_vez = false;
    }
    else
    {
      rtc_co2 = (crudo_co2 * ALPHA_EMA) + (rtc_co2 * (1.0 - ALPHA_EMA));
      rtc_ch2o = (crudo_ch2o * ALPHA_EMA) + (rtc_ch2o * (1.0 - ALPHA_EMA));
      rtc_tvoc = (crudo_tvoc * ALPHA_EMA) + (rtc_tvoc * (1.0 - ALPHA_EMA));
      rtc_pm25 = (crudo_pm25 * ALPHA_EMA) + (rtc_pm25 * (1.0 - ALPHA_EMA));
      rtc_pm10 = (crudo_pm10 * ALPHA_EMA) + (rtc_pm10 * (1.0 - ALPHA_EMA));
      rtc_temp = (crudo_temp * ALPHA_EMA) + (rtc_temp * (1.0 - ALPHA_EMA));
      rtc_hum = (crudo_hum * ALPHA_EMA) + (rtc_hum * (1.0 - ALPHA_EMA));
    }

    // 3. Calcular AQI
    rtc_aqi = calcularAQI_PM25(rtc_pm25);

    // 4. Actualizar LCD
    actualizarLCD(rtc_temp, rtc_hum, round(rtc_co2), rtc_aqi, round(rtc_pm25), round(rtc_pm10), round(rtc_tvoc), round(rtc_ch2o));

    // 5. Enviar a ThingSpeak
    if (!modem.isGprsConnected())
    {
      Serial.println("[Red] Reconectando GPRS por seguridad...");
      modem.gprsConnect(apn, gprsUser, gprsPass);
    }

    Serial.println("[Nube] Enviando telemetría...");
    ThingSpeak.setField(1, (int)round(rtc_co2));
    ThingSpeak.setField(2, (int)round(rtc_ch2o));
    ThingSpeak.setField(3, (int)round(rtc_tvoc));
    ThingSpeak.setField(4, (int)round(rtc_pm25));
    ThingSpeak.setField(5, (int)round(rtc_pm10));
    ThingSpeak.setField(6, rtc_temp);
    ThingSpeak.setField(7, rtc_hum);
    ThingSpeak.setField(8, rtc_aqi);

    if (ThingSpeak.writeFields(channelID, writeAPIKey) == 200)
    {
      Serial.println("[Éxito] Payload publicado en ThingSpeak.");
    }
    else
    {
      Serial.println("[Error] Fallo al publicar.");
    }
  }
  else
  {
    Serial.println("[Error] Datos insuficientes para aplicar la media.");
  }

  // A dormir
  irADeepSleep();
}