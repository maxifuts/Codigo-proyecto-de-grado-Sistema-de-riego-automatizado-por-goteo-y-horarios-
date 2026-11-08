// ============================================================
//  RIEGO AUTOMÁTICO CON ESP32 + RELÉ + RELOJ RTC (DS1307)
//  Abre una válvula a horas fijas durante unos minutos.
//  No usa WiFi: la hora la lleva el módulo RTC con su pila.
// ============================================================

#include <Wire.h>      // Comunicación I2C (así el ESP32 habla con el RTC)
#include "RTClib.h"    // Librería que maneja el reloj RTC y las fechas

// ===== CONFIGURA ESTO =====
// Pon true SOLO la primera vez para poner el RTC en la hora del computador.
// Luego sube el código otra vez con false (si no, cada reinicio
// volvería a poner la hora de compilación, que ya sería una hora vieja).
const bool AJUSTAR_HORA = false;
// ==========================

RTC_DS1307 rtc;            // Objeto que representa el módulo RTC

const int PIN_RELE = 26;   // Pin del ESP32 conectado al relé

// ------------------------------------------------------------
// Estructura para guardar un horario de riego:
//   hora        -> 0 a 23
//   minuto      -> 0 a 59
//   duracionMin -> cuántos minutos queda abierta la válvula
// ------------------------------------------------------------
struct Horario { uint8_t hora; uint8_t minuto; uint8_t duracionMin; };

// Lista de horarios. Para agregar otro riego, añade una línea más.
Horario horarios[] = {
  {6, 0, 5},     // 6:00 a. m. durante 5 minutos (riego principal)
  {16, 0, 3}     // 4:00 p. m. durante 3 minutos (refuerzo ligero)
};

// Calcula automáticamente cuántos horarios hay en la lista
const int NUM_HORARIOS = sizeof(horarios) / sizeof(horarios[0]);

// ---------- Variables de estado (la "memoria" del programa) ----------
bool regando = false;                 // ¿Está regando ahora mismo?
unsigned long inicioRiego = 0;        // Momento (en millis) en que empezó el riego
unsigned long duracionActualMs = 0;   // Cuánto debe durar el riego actual, en ms
int ultimaClave = -1;                 // Último minuto del día ya revisado
unsigned long ultimaImpresion = 0;    // Última vez que se imprimió la hora

// ------------------------------------------------------------
// Abre o cierra la válvula.
//   abrir = true  -> pin como INPUT (flotante): el relé NO se activa
//   abrir = false -> pin como OUTPUT en LOW: el relé SE activa
// Esto depende de cómo esté cableado tu relé (módulo activo en LOW
// y válvula conectada al contacto NC). Si tu válvula hace lo
// contrario de lo esperado, invierte esta lógica.
// ------------------------------------------------------------
void valvula(bool abrir) {
  if (abrir) {
    pinMode(PIN_RELE, INPUT);
  } else {
    pinMode(PIN_RELE, OUTPUT);
    digitalWrite(PIN_RELE, LOW);
  }
  regando = abrir;   // Guarda el estado actual
}

// ------------------------------------------------------------
// Imprime la fecha y hora en el monitor serie con formato
// dd/mm/aaaa hh:mm:ss (sirve para comprobar que el RTC va bien)
// ------------------------------------------------------------
void mostrarHora(DateTime t) {
  Serial.printf("Hora RTC: %02d/%02d/%04d %02d:%02d:%02d\n",
                t.day(), t.month(), t.year(),
                t.hour(), t.minute(), t.second());
}

// ============================================================
//  setup(): se ejecuta UNA vez al encender o reiniciar
// ============================================================
void setup() {
  Serial.begin(115200);   // Inicia el monitor serie
  valvula(false);         // Por seguridad arranca con la válvula cerrada

  Wire.begin(21, 22);     // Inicia I2C: SDA = pin 21, SCL = pin 22

  // Busca el RTC. Si no responde, avisa y se queda aquí para siempre
  if (!rtc.begin()) {
    Serial.println("No se encontró el RTC. Revisa SDA/SCL/VCC/GND.");
    while (true) delay(1000);
  }

  // Pone la hora de compilación si lo pediste con AJUSTAR_HORA
  // o si el RTC no estaba funcionando (pila agotada o módulo nuevo)
  if (AJUSTAR_HORA || !rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC ajustado con la hora de compilación.");
  }

  mostrarHora(rtc.now());   // Muestra la hora con la que arrancó
}

// ============================================================
//  loop(): se repite sin parar
// ============================================================
void loop() {
  DateTime ahora = rtc.now();   // Lee la hora actual del RTC

  // "clave" convierte la hora en un solo número: minutos desde medianoche.
  // Ejemplo: 6:00 -> 360. Sirve para detectar cuándo cambia el minuto.
  int clave = ahora.hour() * 60 + ahora.minute();

  // Imprime la hora una vez por segundo
  if (millis() - ultimaImpresion >= 1000) {
    ultimaImpresion = millis();
    mostrarHora(ahora);
  }

  // ---------- 1) ¿Toca iniciar un riego? ----------
  // Solo se revisa si NO está regando y si cambió el minuto,
  // así un mismo horario no se dispara varias veces.
  if (!regando && clave != ultimaClave) {
    ultimaClave = clave;   // Recuerda que este minuto ya fue revisado

    // Recorre la lista de horarios buscando una coincidencia
    for (int i = 0; i < NUM_HORARIOS; i++) {
      if (horarios[i].hora == ahora.hour() && horarios[i].minuto == ahora.minute()) {
        // Convierte los minutos a milisegundos (UL evita desbordamiento)
        duracionActualMs = horarios[i].duracionMin * 60UL * 1000UL;
        valvula(true);              // Abre la válvula
        inicioRiego = millis();     // Anota cuándo empezó
        Serial.printf("Riego ON  %02d:%02d por %d min\n",
                      ahora.hour(), ahora.minute(), horarios[i].duracionMin);
        break;                      // Ya encontró uno, deja de buscar
      }
    }
  }

  // ---------- 2) ¿Ya terminó el riego? ----------
  // Compara el tiempo transcurrido con la duración programada
  if (regando && millis() - inicioRiego >= duracionActualMs) {
    valvula(false);                 // Cierra la válvula
    Serial.println("Riego OFF");
  }

  delay(200);   // Pausa corta para no saturar el procesador
}
