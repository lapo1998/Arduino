/*
  Sistema Inteligente de Monitoreo Ambiental - Arduino UNO
  Sensores: DHT11 (temperatura/humedad) y LDR (luminosidad)
  Salidas: LCD 16x2 I2C, LED verde, LED amarillo, LED rojo, buzzer, Monitor Serial (UART)

  CONEXIONES:
    DHT11        -> DATA = pin 2, VCC = 5V, GND = GND
    LDR          -> un lado a 5V, el otro a A0 y a una resistencia de 10k hacia GND
    LCD I2C      -> SDA = A4, SCL = A5, VCC = 5V, GND = GND
    LED verde    -> pin 4 (con resistencia 220 ohm a GND)
    LED amarillo -> pin 5 (con resistencia 220 ohm a GND)
    LED rojo     -> pin 6 (con resistencia 220 ohm a GND)
    Buzzer       -> pin 7 (el otro terminal a GND)

  LIBRERIAS:
    - "DHT sensor library" de Adafruit
    - "Adafruit Unified Sensor"
    - "LiquidCrystal I2C" (de Frank de Brabander)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ---------- Pines ----------
#define PIN_DHT     2
#define PIN_LDR     A0
#define PIN_LED_V   4    // verde    = estado normal
#define PIN_LED_A   5    // amarillo = advertencia
#define PIN_LED_R   6    // rojo     = alarma
#define PIN_BUZZER  7

// ---------- Tipo de sensor ----------
#define TIPO_DHT DHT11

// ---------- Umbrales (el DHT11 mide en pasos de 1 unidad) ----------
const float TEMP_ADVERTENCIA = 26.0;   // grados C: desde aqui, advertencia
const float TEMP_ALARMA      = 30.0;   // grados C: desde aqui, alarma
const float HUM_ADVERTENCIA  = 77.0;   // % humedad: desde aqui, advertencia
const float HUM_ALARMA       = 90.0;   // % humedad: desde aqui, alarma
const int   LUZ_ADVERTENCIA  = 900;    // luz (0-1023): desde aqui, SOLO advertencia

// Pitidos del buzzer al entrar en advertencia
const int PITIDOS_ADVERTENCIA = 3;

// Si la advertencia (por temperatura o humedad) dura este tiempo sin
// corregirse, pasa a alarma (ms). La luz NUNCA genera alarma.
const unsigned long TIEMPO_ESCALADA = 20000;

// ---------- Objetos ----------
DHT dht(PIN_DHT, TIPO_DHT);
LiquidCrystal_I2C lcd(0x27, 16, 2);    // si no funciona prueba 0x3F

// ---------- Maquina de estados ----------
enum Estado { INICIAL, MONITOREO, ADVERTENCIA, ALARMA };
Estado estado = INICIAL;

// ---------- Variables ----------
float temperatura = 0;
float humedad = 0;
int luz = 0;

unsigned long tLectura = 0;
unsigned long tParpadeo = 0;
unsigned long tInicioAdvertencia = 0;  // momento en que empezo la advertencia
bool parpadeo = false;
int pitidos = 0;                       // pitidos dados en la advertencia actual
const unsigned long INTERVALO_LECTURA = 2000;  // ms

// Muestra una linea de 16 caracteres en el LCD
void escribirLinea(int fila, const char *texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  lcd.setCursor(0, fila);
  lcd.print(buf);
}

// Nombre corto del estado para LCD y Monitor Serial
const char* nombreEstado(Estado e) {
  switch (e) {
    case INICIAL:     return "INICIAL";
    case MONITOREO:   return "MONITOREO";
    case ADVERTENCIA: return "ADVERT.";
    case ALARMA:      return "ALARMA";
  }
  return "";
}

// Lee los sensores; devuelve false si el DHT fallo
bool leerSensores() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  luz = analogRead(PIN_LDR);          // 0 - 1023
  if (isnan(h) || isnan(t)) {
    Serial.println("Error: no se pudo leer el DHT");
    return false;
  }
  humedad = h;
  temperatura = t;
  return true;
}

// true si temperatura o humedad estan en zona de advertencia (o peor)
bool tempHumEnAdvertencia() {
  return temperatura >= TEMP_ADVERTENCIA || humedad >= HUM_ADVERTENCIA;
}

// Estado que piden los umbrales (sin considerar el tiempo)
Estado evaluarEstado() {
  if (temperatura >= TEMP_ALARMA || humedad >= HUM_ALARMA) return ALARMA;
  if (tempHumEnAdvertencia() || luz >= LUZ_ADVERTENCIA) return ADVERTENCIA;
  return MONITOREO;
}

// Acciones de salida de cada estado (maquina de Moore)
void actualizarSalidas() {
  switch (estado) {
    case INICIAL:       // todo apagado
      digitalWrite(PIN_LED_V, LOW);
      digitalWrite(PIN_LED_A, LOW);
      digitalWrite(PIN_LED_R, LOW);
      noTone(PIN_BUZZER);
      break;
    case MONITOREO:     // normal: solo verde
      digitalWrite(PIN_LED_V, HIGH);
      digitalWrite(PIN_LED_A, LOW);
      digitalWrite(PIN_LED_R, LOW);
      noTone(PIN_BUZZER);
      break;
    case ADVERTENCIA:   // amarillo fijo + PITIDOS_ADVERTENCIA pitidos
      digitalWrite(PIN_LED_V, LOW);
      digitalWrite(PIN_LED_A, HIGH);
      digitalWrite(PIN_LED_R, LOW);
      if (parpadeo && pitidos <= PITIDOS_ADVERTENCIA) tone(PIN_BUZZER, 1000);
      else noTone(PIN_BUZZER);
      break;
    case ALARMA:        // rojo fijo + buzzer intermitente continuo
      digitalWrite(PIN_LED_V, LOW);
      digitalWrite(PIN_LED_A, LOW);
      digitalWrite(PIN_LED_R, HIGH);
      if (parpadeo) tone(PIN_BUZZER, 1000); else noTone(PIN_BUZZER);
      break;
  }
}

// Muestra temperatura, humedad, luz y estado en el LCD
void mostrarLCD() {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "T:%dC H:%d%%", (int)temperatura, (int)humedad);
  snprintf(l2, sizeof(l2), "L:%d %s", luz, nombreEstado(estado));
  escribirLinea(0, l1);
  escribirLinea(1, l2);
}

// Envia los datos por UART al Monitor Serial
void enviarSerial() {
  Serial.print("Temperatura: "); Serial.print(temperatura, 1); Serial.println(" C");
  Serial.print("Humedad: ");     Serial.print(humedad, 0);     Serial.println(" %");
  Serial.print("Luminosidad: "); Serial.println(luz);
  Serial.print("Estado: ");      Serial.println(nombreEstado(estado));
  Serial.println("---------------------");
}

void setup() {
  pinMode(PIN_LED_V, OUTPUT);
  pinMode(PIN_LED_A, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.begin(9600);       // UART a 9600 baudios
  dht.begin();
  lcd.init();
  lcd.backlight();

  // Estado INICIAL: prueba de salidas y mensaje de bienvenida
  estado = INICIAL;
  escribirLinea(0, "Monitoreo");
  escribirLinea(1, "Ambiental UEA");
  digitalWrite(PIN_LED_V, HIGH);
  digitalWrite(PIN_LED_A, HIGH);
  digitalWrite(PIN_LED_R, HIGH);
  tone(PIN_BUZZER, 800, 200);
  Serial.println("Sistema iniciado");
  delay(2000);
  lcd.clear();
  estado = MONITOREO;
}

void loop() {
  unsigned long ahora = millis();

  // Cada 300 ms: ritmo de los pitidos (sin bloquear el programa)
  if (ahora - tParpadeo >= 300) {
    tParpadeo = ahora;
    parpadeo = !parpadeo;
    // En advertencia, cuenta un pitido nuevo cada vez que el buzzer se enciende
    if (estado == ADVERTENCIA && parpadeo && pitidos <= PITIDOS_ADVERTENCIA) {
      pitidos++;
    }
    actualizarSalidas();
  }

  // Cada 2 s: lectura de sensores y actualizacion
  if (ahora - tLectura >= INTERVALO_LECTURA) {
    tLectura = ahora;
    if (leerSensores()) {
      // El tiempo de escalada solo cuenta mientras temperatura/humedad
      // estan en advertencia (la luz no cuenta)
      if (!tempHumEnAdvertencia()) {
        tInicioAdvertencia = ahora;
      }

      Estado nuevo = evaluarEstado();

      // La alarma se mantiene mientras temperatura/humedad no se normalicen
      if (estado == ALARMA && nuevo == ADVERTENCIA && tempHumEnAdvertencia()) {
        nuevo = ALARMA;
      }

      // Si la advertencia por temperatura/humedad dura demasiado, escala a alarma
      if (estado == ADVERTENCIA && nuevo == ADVERTENCIA && tempHumEnAdvertencia() &&
          (ahora - tInicioAdvertencia >= TIEMPO_ESCALADA)) {
        nuevo = ALARMA;
        Serial.println("Evento: advertencia ignorada, se activa la alarma");
      }

      // Si cambia el estado: se registra y se preparan las salidas
      if (nuevo != estado) {
        estado = nuevo;
        noTone(PIN_BUZZER);
        Serial.print("Evento: cambio a estado ");
        Serial.println(nombreEstado(estado));
        if (estado == ADVERTENCIA) {
          pitidos = 0;                     // reinicia la cuenta de pitidos
          parpadeo = false;
          tParpadeo = ahora;
        }
      }

      mostrarLCD();
      enviarSerial();
      actualizarSalidas();
    }
  }
}