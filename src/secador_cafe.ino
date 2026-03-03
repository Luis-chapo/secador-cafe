/*
 * SECADOR DE CAFÉ 
 */

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

// ====== CONFIGURACIÓN PINES ======
#define DHTPIN 8
#define DHTTYPE DHT22
const int AC_IN_ZERO = 2;
const int triac = 9;
const int LED_VERDE = 7;
const int LED_AMARILLO = 6;
const int LED_ROJO = 5;
const int chipSelect = 10;
const int BOTON_EJECT = A0;

// ====== RANGOS VÁLIDOS SENSOR ======
const float TEMP_MIN = -40.0;
const float TEMP_MAX = 80.0;
const float HUM_MIN = 0.0;
const float HUM_MAX = 100.0;

// ====== OBJETOS ======
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7);
DHT dht(DHTPIN, DHTTYPE);

// ====== VARIABLES TRIAC ======
volatile int pot_value = 7000;
volatile int pot_value_objetivo = 7000;
volatile boolean state = LOW;
volatile int estado_AC_IN_ZERO = 0;
volatile int lastButtonState_asc = 0;

// ====== CONTROL AMBIENTAL ======
float offsetTemperatura = 0.0;
float offsetHumedad = 0.0;
float temperatura = 25.0;
float humedadAmbiente = 50.0;
int calorAutomatico = 10;
String estadoSecado = "INICIANDO";
bool sistemaActivo = true;

// Parámetros de secado
const float HUMEDAD_OBJETIVO_MIN = 10.0;
const float HUMEDAD_OBJETIVO_MAX = 12.0;
const float HUMEDAD_LIMITE_ALTO = 15.0;
const float HUMEDAD_CRITICA = 25.0;
const float TEMP_AMBIENTE_MIN = 35.0;
const float TEMP_AMBIENTE_OPTIMA = 42.0;
const float TEMP_AMBIENTE_MAX = 50.0;

// ====== VARIABLES SD ======
bool sdOk = false;
bool loggingActivo = true;
unsigned long lastLogTime = 0;
unsigned long count = 0;
const unsigned long LOG_INTERVAL = 60000;

// ====== VARIABLES LCD ======
String lineaAnterior0 = "";
String lineaAnterior1 = "";
bool forzarActualizacion = false;

// ====== VARIABLES TEMPORIZACIÓN ======
unsigned long tiempoUltimoCambio = 0;
unsigned long ultimoCambioCalor = 0;
bool ledAmarilloEstado = LOW;

// ====== PROTOTIPOS ======
unsigned long recuperarUltimoLog();
String obtenerNombreArchivoDiario();
unsigned long calcularDiaActual();

// ====== SETUP ======
void setup() {
  Serial.begin(9600);
  Serial.println(F("========================================"));
  Serial.println(F("SECADOR CAFE - VERSION CAMPO v2"));
  Serial.println(F("- Sistema de reintentos SD (5x)"));
  Serial.println(F("- Boton dual (3s/6s)"));
  Serial.println(F("- Recuperacion automatica"));
  Serial.println(F("========================================"));
  
  // Configurar TRIAC
  pinMode(AC_IN_ZERO, INPUT);
  pinMode(triac, OUTPUT);
  digitalWrite(triac, LOW);
  
  // Configurar LEDs y botón
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(BOTON_EJECT, INPUT_PULLUP);
  
  // Test LEDs
  digitalWrite(LED_ROJO, HIGH); delay(200); digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_AMARILLO, HIGH); delay(200); digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_VERDE, HIGH); delay(200); digitalWrite(LED_VERDE, LOW);
  
  // Inicializar DHT22
  Serial.print(F("DHT22... "));
  dht.begin();
  delay(2000);
  
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  if (isnan(t) || isnan(h)) {
    Serial.println(F("ERROR!"));
    lcd.clear();
    lcd.print(F("ERROR DHT22!"));
    while(1) {
      digitalWrite(LED_ROJO, !digitalRead(LED_ROJO));
      delay(500);
    }
  }
  Serial.println(F("OK"));
  Serial.print(F("  T:")); Serial.print(t,1); Serial.print(F("C H:"));
  Serial.print(h,1); Serial.println(F("%"));
  
  // Inicializar LCD
  lcd.setBacklightPin(3, POSITIVE);
  lcd.setBacklight(HIGH);
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Secador de Cafe"));
  lcd.setCursor(0, 1);
  lcd.print(F("Iniciando..."));
  delay(1500);
  
  // Inicializar SD UNA SOLA VEZ
  Serial.print(F("SD... "));
  if (SD.begin(chipSelect)) {
    sdOk = true;
    Serial.println(F("OK"));
    
    lcd.clear();
    lcd.print(F("Recuperando log"));
    count = recuperarUltimoLog();
    Serial.print(F("Ultimo log: #")); Serial.println(count);
    Serial.print(F("Continuando: #")); Serial.println(count + 1);
    
    lcd.setCursor(0, 1);
    lcd.print(F("Log#")); lcd.print(count);
    delay(1500);
  } else {
    Serial.println(F("NO"));
    lcd.setCursor(0, 1);
    lcd.print(F("Sin SD - OK"));
    delay(1500);
  }
  
  // Estado inicial
  lcd.clear();
  forzarActualizacion = true;
  calorAutomatico = 10;
  pot_value = 7000;
  pot_value_objetivo = 7000;
  tiempoUltimoCambio = millis();
  ultimoCambioCalor = millis();
  
  Serial.println(F("SISTEMA LISTO"));
  Serial.println(F("Boton A0:"));
  Serial.println(F("  - 3s = Expulsar SD"));
  Serial.println(F("  - 6s = Reiniciar sistema"));
  Serial.println(F("========================================\n"));
}

// ====== LOOP PRINCIPAL ======
void loop() {
  // CONTROL CRÍTICO DEL TRIAC
  estado_AC_IN_ZERO = digitalRead(AC_IN_ZERO);
  
  if (estado_AC_IN_ZERO != lastButtonState_asc) {
    if (estado_AC_IN_ZERO == LOW) {
      state = true;
    }
  }
  lastButtonState_asc = estado_AC_IN_ZERO;
  
  if (state == true) {
    delayMicroseconds(pot_value);
    digitalWrite(triac, HIGH);
    delayMicroseconds(10);
    digitalWrite(triac, LOW);
    state = false;
  }
  
  aplicarTransicionSuave();
  
  // TAREAS PERIÓDICAS
  static unsigned long ultimaEjecucion = 0;
  unsigned long tiempoActual = millis();
  
  if (tiempoActual - ultimaEjecucion > 50) {
    static byte tarea = 0;
    
    switch (tarea) {
      case 0:
        if (sistemaActivo) {
          controlAutomaticoAmbiente();
          calcularDelayTRIACOptimizado();
        } else {
          calorAutomatico = 0;
          pot_value_objetivo = 8000;
        }
        break;
      case 1: controlarLEDsIndicadores(); break;
      case 2: if (Serial.available()) procesarComandos(); break;
      case 3: {
        static unsigned long ultimoLCD = 0;
        if (tiempoActual - ultimoLCD > 1000) {
          actualizarPantallaSinParpadeo();
          ultimoLCD = tiempoActual;
        }
      } break;
      case 4: verificarBotonExpulsion(); break;
      case 5: guardarDatosSD(); break;
    }
    
    tarea = (tarea + 1) % 6;
    ultimaEjecucion = tiempoActual;
  }
  
  // Leer sensor cada 3 segundos
  static unsigned long ultimaLecturaDHT = 0;
  if (tiempoActual - ultimaLecturaDHT > 3000) {
    leerSensorAmbienteRapido();
    ultimaLecturaDHT = tiempoActual;
  }
  
  // Reporte cada 15 segundos
  static unsigned long ultimoReporte = 0;
  if (tiempoActual - ultimoReporte > 15000) {
    mostrarReporteAmbiente();
    ultimoReporte = tiempoActual;
  }
}

// ====== FUNCIONES SD CON REINTENTOS ======
unsigned long recuperarUltimoLog() {
  unsigned long maxLog = 0;
  File root = SD.open("/");
  if (!root) return 0;
  
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    
    String filename = entry.name();
    if (filename.startsWith("LOG_") && filename.endsWith(".CSV")) {
      if (entry.size() > 50) {
        entry.seek(entry.size() - 50);
        String lastLine = "";
        String currentLine = "";
        
        while (entry.available()) {
          char c = entry.read();
          if (c == '\n') {
            if (currentLine.length() > 0) {
              lastLine = currentLine;
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }
        }
        
        if (currentLine.length() > 0) lastLine = currentLine;
        
        int commaPos = lastLine.indexOf(',');
        if (commaPos > 0) {
          unsigned long logNum = lastLine.substring(0, commaPos).toInt();
          if (logNum > maxLog) maxLog = logNum;
        }
      }
    }
    entry.close();
  }
  
  root.close();
  return maxLog;
}

unsigned long calcularDiaActual() {
  return millis() / 86400000UL;
}

String obtenerNombreArchivoDiario() {
  return "log_" + String(calcularDiaActual()) + ".csv";
}

void guardarDatosSD() {
  if (!sdOk || !loggingActivo || !sistemaActivo) return;
  
  unsigned long tiempoActual = millis();
  if (tiempoActual - lastLogTime < LOG_INTERVAL) return;
  
  lastLogTime = tiempoActual;
  count++;
  
  // Validar datos
  if (temperatura < TEMP_MIN || temperatura > TEMP_MAX) {
    count--;
    return;
  }
  if (humedadAmbiente < HUM_MIN || humedadAmbiente > HUM_MAX) {
    count--;
    return;
  }
  
  // ====== OPTIMIZACIÓN RAM: Usar buffer de caracteres ======
  char filename[13];  // "log_0.csv" + null = 10 chars
  strcpy(filename, "log_0.csv");
  
  // Sistema de reintentos
  static int erroresConsecutivos = 0;
  
  // ====== CRÍTICO: Abrir SOLO en modo append ======
  File dataFile = SD.open(filename, FILE_WRITE);
  
  if (dataFile) {
    // Verificar si es archivo nuevo (tamaño = 0)
    bool esNuevo = (dataFile.size() == 0);
    
    if (esNuevo) {
      // Header sin F() para ahorrar flash
      dataFile.print(F("Num,Time_ms,Temp_C,Hum_%,Calor_%,Estado,potValue\n"));
    }
    
    // ====== ESCRITURA DIRECTA SIN STRING ======
    dataFile.print(count);
    dataFile.write(',');
    dataFile.print(millis());
    dataFile.write(',');
    dataFile.print(temperatura, 2);
    dataFile.write(',');
    dataFile.print(humedadAmbiente, 2);
    dataFile.write(',');
    dataFile.print(calorAutomatico);
    dataFile.write(',');
    
    // Estado como char array (sin String)
    if (estadoSecado == "H.CRITICA") dataFile.print(F("H.CRITICA"));
    else if (estadoSecado == "SECANDO") dataFile.print(F("SECANDO"));
    else if (estadoSecado == "AJUSTANDO") dataFile.print(F("AJUSTANDO"));
    else if (estadoSecado == "OPTIMO") dataFile.print(F("OPTIMO"));
    else if (estadoSecado == "MUY_SECO") dataFile.print(F("MUY_SECO"));
    else if (estadoSecado == "T.ALTA") dataFile.print(F("T.ALTA"));
    else if (estadoSecado == "T.CONTROL") dataFile.print(F("T.CONTROL"));
    else if (estadoSecado == "CALENTANDO") dataFile.print(F("CALENTANDO"));
    else dataFile.print(F("DESCONOCIDO"));
    
    dataFile.write(',');
    dataFile.print(pot_value);
    dataFile.write('\n');
    
    // ====== CERRAR INMEDIATAMENTE ======
    dataFile.close();
    
    // Log success (sin String concatenation)
    Serial.print(F("Log#"));
    Serial.print(count);
    Serial.print(F(" OK\n"));
    
    if (erroresConsecutivos > 0) {
      Serial.print(F("  [Recuperado tras "));
      Serial.print(erroresConsecutivos);
      Serial.print(F(" error(es)]\n"));
    }
    erroresConsecutivos = 0;
    
  } else {
    // ERROR
    erroresConsecutivos++;
    
    Serial.print(F("Error SD ("));
    Serial.print(erroresConsecutivos);
    Serial.print(F("/5) - Log#"));
    Serial.println(count);
    
    if (erroresConsecutivos >= 5) {
      Serial.println(F("\n*** SD DESHABILITADA ***"));
      sdOk = false;
      
      lcd.clear();
      lcd.print(F("ERROR SD"));
      
      for (int i = 0; i < 4; i++) {
        digitalWrite(LED_ROJO, HIGH);
        delay(200);
        digitalWrite(LED_ROJO, LOW);
        delay(200);
      }
      
      forzarActualizacion = true;
    } else {
      Serial.print(F("  Reintento en 60s\n"));
      count--;
    }
  }
}



// ====== BOTÓN DUAL: EXPULSAR (3s) O REINICIAR (6s) ======
void verificarBotonExpulsion() {
  static unsigned long tiempoInicio = 0;
  static bool estadoAnterior = HIGH;
  static bool expulsionActivada = false;
  static bool reinicioActivado = false;
  
  bool estadoActual = digitalRead(BOTON_EJECT);
  
  // Detectar inicio de presión
  if (estadoActual == LOW && estadoAnterior == HIGH) {
    tiempoInicio = millis();
    expulsionActivada = false;
    reinicioActivado = false;
    digitalWrite(LED_AMARILLO, HIGH);
  }
  
  // Botón mantenido presionado
  if (estadoActual == LOW) {
    unsigned long duracion = millis() - tiempoInicio;
    
    // Entre 3-6s: feedback de preparación para reinicio
    if (duracion > 3000 && duracion < 6000 && !expulsionActivada) {
      static unsigned long ultimoParpadeo = 0;
      if (millis() - ultimoParpadeo > 200) {
        digitalWrite(LED_AMARILLO, !digitalRead(LED_AMARILLO));
        ultimoParpadeo = millis();
      }
    }
    
    // A los 6s: REINICIAR
    if (duracion > 6000 && !reinicioActivado) {
      reinicioActivado = true;
      reiniciarSistema();
    }
    // A los 3s: marcar para expulsión
    else if (duracion > 3000 && duracion <= 6000 && !expulsionActivada) {
      expulsionActivada = true;
    }
  }
  
  // Botón liberado
  if (estadoActual == HIGH && estadoAnterior == LOW) {
    unsigned long duracion = millis() - tiempoInicio;
    
    // Si liberó entre 3-6s → ejecutar expulsión
    if (duracion > 3000 && duracion < 6000 && expulsionActivada) {
      digitalWrite(LED_AMARILLO, LOW);
      expulsarSD();
    }
    // Si liberó antes de 3s → cancelar
    else if (duracion < 3000) {
      digitalWrite(LED_AMARILLO, LOW);
    }
  }
  
  estadoAnterior = estadoActual;
}

void expulsarSD() {
  if (!sdOk) {
    Serial.println(F("No hay SD"));
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_ROJO, HIGH); delay(200);
      digitalWrite(LED_ROJO, LOW); delay(200);
    }
    return;
  }
  
  Serial.println(F("EXPULSION SD"));
  loggingActivo = false;
  
  delay(2000);  // Esperar escrituras pendientes
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("** RETIRAR SD **"));
  lcd.setCursor(0, 1);
  lcd.print(F("**   SEGURO   **"));
  
  Serial.println(F("SEGURO RETIRAR SD"));
  Serial.println(F("Bombillo sigue activo"));
  
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_VERDE, i % 2 == 0 ? HIGH : LOW);
    delay(300);
  }
  digitalWrite(LED_VERDE, LOW);
  
  sdOk = false;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("SD retirada"));
  lcd.setCursor(0, 1);
  lcd.print(F("Sistema activo"));
  
  Serial.println(F("SD marcada como retirada"));
  
  delay(2000);
  forzarActualizacion = true;
}

void reiniciarSistema() {
  Serial.println(F("\n========================================"));
  Serial.println(F("     REINICIANDO SISTEMA"));
  Serial.println(F("========================================"));
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("  REINICIANDO   "));
  lcd.setCursor(0, 1);
  lcd.print(F("  SISTEMA...    "));
  
  // Parpadeo de TODOS los LEDs
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_ROJO, HIGH);
    digitalWrite(LED_AMARILLO, HIGH);
    digitalWrite(LED_VERDE, HIGH);
    delay(200);
    digitalWrite(LED_ROJO, LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_VERDE, LOW);
    delay(200);
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F(" REINICIO EN... "));
  
  for (int i = 3; i > 0; i--) {
    lcd.setCursor(7, 1);
    lcd.print(i);
    digitalWrite(LED_ROJO, HIGH);
    delay(300);
    digitalWrite(LED_ROJO, LOW);
    delay(300);
  }
  
  lcd.clear();
  lcd.print(F("   REINICIANDO  "));
  Serial.println(F("Ejecutando reinicio..."));
  delay(500);
  
  // REINICIAR
  asm volatile ("  jmp 0");
}

// ====== FUNCIONES TRIAC (sin cambios) ======
void aplicarTransicionSuave() {
  if (pot_value != pot_value_objetivo) {
    unsigned long tiempoActual = millis();
    if (tiempoActual - tiempoUltimoCambio > 50) {
      int diferencia = pot_value_objetivo - pot_value;
      if (abs(diferencia) > 1000) {
        pot_value += (diferencia > 0) ? 50 : -50;
      } else if (abs(diferencia) > 500) {
        pot_value += (diferencia > 0) ? 30 : -30;
      } else if (abs(diferencia) > 100) {
        pot_value += (diferencia > 0) ? 15 : -15;
      } else {
        pot_value = pot_value_objetivo;
      }
      tiempoUltimoCambio = tiempoActual;
    }
  }
}

void calcularDelayTRIACOptimizado() {
  int nuevoValor;
  
  if (calorAutomatico == 0) nuevoValor = 8000;
  else if (calorAutomatico <= 10) nuevoValor = 7000;
  else if (calorAutomatico <= 20) nuevoValor = 6500;
  else if (calorAutomatico <= 30) nuevoValor = 6000;
  else if (calorAutomatico <= 40) nuevoValor = 5500;
  else if (calorAutomatico <= 50) nuevoValor = 5000;
  else if (calorAutomatico <= 60) nuevoValor = 4500;
  else if (calorAutomatico <= 70) nuevoValor = 4000;
  else if (calorAutomatico <= 80) nuevoValor = 3500;
  else if (calorAutomatico <= 90) nuevoValor = 3000;
  else nuevoValor = 2500;
  
  nuevoValor = constrain(nuevoValor, 2500, 8000);
  
  if (abs(nuevoValor - pot_value_objetivo) > 200) {
    pot_value_objetivo = nuevoValor;
  }
  
  static unsigned long ultimoDebug = 0;
  if (millis() - ultimoDebug > 5000) {
    Serial.print(F("Control:")); Serial.print(calorAutomatico);
    Serial.print(F("% pot:")); Serial.print(pot_value);
    Serial.print(F("us->")); Serial.print(pot_value_objetivo);
    Serial.print(F("us H:")); Serial.print(humedadAmbiente,1);
    Serial.println(F("%"));
    ultimoDebug = millis();
  }
}

void leerSensorAmbienteRapido() {
  float tempRaw = dht.readTemperature();
  float humRaw = dht.readHumidity();
  
  if (!isnan(tempRaw) && !isnan(humRaw)) {
    temperatura = (temperatura * 0.9) + ((tempRaw + offsetTemperatura) * 0.1);
    humedadAmbiente = (humedadAmbiente * 0.9) + ((humRaw + offsetHumedad) * 0.1);
  } else {
    estadoSecado = "ERROR_SENSOR";
    calorAutomatico = 0;
  }
}

void controlAutomaticoAmbiente() {
  int nuevoCalor = calorAutomatico;
  
  if (humedadAmbiente > HUMEDAD_CRITICA) {
    nuevoCalor = 90; estadoSecado = "H.CRITICA";
  } else if (humedadAmbiente > HUMEDAD_LIMITE_ALTO) {
    float factor = (humedadAmbiente - HUMEDAD_LIMITE_ALTO) / (HUMEDAD_CRITICA - HUMEDAD_LIMITE_ALTO);
    nuevoCalor = 40 + (int)(factor * 40); estadoSecado = "SECANDO";
  } else if (humedadAmbiente > HUMEDAD_OBJETIVO_MAX) {
    nuevoCalor = 35; estadoSecado = "AJUSTANDO";
  } else if (humedadAmbiente >= HUMEDAD_OBJETIVO_MIN) {
    nuevoCalor = 20; estadoSecado = "OPTIMO";
  } else {
    nuevoCalor = 10; estadoSecado = "MUY_SECO";
  }
  
  if (temperatura > TEMP_AMBIENTE_MAX) {
    nuevoCalor = 0; estadoSecado = "T.ALTA";
  } else if (temperatura > TEMP_AMBIENTE_OPTIMA) {
    nuevoCalor = (int)(nuevoCalor * 0.6);
    if (estadoSecado != "T.ALTA") estadoSecado = "T.CONTROL";
  } else if (temperatura < TEMP_AMBIENTE_MIN && nuevoCalor > 0) {
    nuevoCalor = min(nuevoCalor + 25, 95);
    if (estadoSecado == "OPTIMO") estadoSecado = "CALENTANDO";
  }
  
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimoCambioCalor < 2000) return;
  
  if (abs(nuevoCalor - calorAutomatico) > 3) {
    if (nuevoCalor > calorAutomatico) {
      calorAutomatico = min(calorAutomatico + 3, nuevoCalor);
    } else {
      calorAutomatico = max(calorAutomatico - 3, nuevoCalor);
    }
    ultimoCambioCalor = tiempoActual;
  }
  
  calorAutomatico = constrain(calorAutomatico, 0, 95);
}

// ====== FUNCIONES LCD ======
void actualizarPantallaSinParpadeo() {
  String nuevaLinea0 = construirLinea0();
  String nuevaLinea1 = construirLinea1();
  
  while (nuevaLinea0.length() < 16) nuevaLinea0 += " ";
  while (nuevaLinea1.length() < 16) nuevaLinea1 += " ";
  
  if (nuevaLinea0 != lineaAnterior0 || forzarActualizacion) {
    lcd.setCursor(0, 0);
    lcd.print(nuevaLinea0);
    lineaAnterior0 = nuevaLinea0;
  }
  
  if (nuevaLinea1 != lineaAnterior1 || forzarActualizacion) {
    lcd.setCursor(0, 1);
    lcd.print(nuevaLinea1);
    lineaAnterior1 = nuevaLinea1;
  }
  
  forzarActualizacion = false;
}

String construirLinea0() {
  String linea = "T:";
  linea += String(temperatura, 1);
  linea += "C H:";
  linea += String(humedadAmbiente, 0);
  linea += "%";
  if (sdOk) linea += " SD";
  else if (!sistemaActivo) linea += " OFF";
  else if (temperatura > TEMP_AMBIENTE_MAX) linea += " !";
  return linea;
}

String construirLinea1() {
  String linea = "";
  if (!sistemaActivo) {
    linea = "SISTEMA APAGADO";
    return linea;
  }
  
  bool condicionesOptimas = (humedadAmbiente >= HUMEDAD_OBJETIVO_MIN && 
                            humedadAmbiente <= HUMEDAD_OBJETIVO_MAX &&
                            temperatura >= TEMP_AMBIENTE_MIN && 
                            temperatura <= TEMP_AMBIENTE_OPTIMA &&
                            estadoSecado == "OPTIMO");
  
  bool hayProblema = (temperatura > TEMP_AMBIENTE_MAX || 
                     estadoSecado == "ERROR_SENSOR" ||
                     estadoSecado == "T.ALTA");
  
  if (hayProblema) {
    if (estadoSecado == "ERROR_SENSOR") linea = "ERROR SENSOR!";
    else if (estadoSecado == "T.ALTA") linea = "TEMP MUY ALTA!";
    else linea = "VERIFICAR SIST!";
  } else if (condicionesOptimas) {
    linea = "VERIFICAR GRANO";
  } else {
    linea = "Auto:";
    linea += String(calorAutomatico);
    linea += "% ";
    if (estadoSecado == "SECANDO") linea += "WORK";
    else if (estadoSecado == "H.CRITICA") linea += "HIGH";
    else if (estadoSecado == "AJUSTANDO") linea += "AJUS";
    else if (estadoSecado == "MUY_SECO") linea += "DRY";
    else if (estadoSecado == "CALENTANDO") linea += "HEAT";
    else if (estadoSecado == "T.CONTROL") linea += "TCTL";
    else linea += "NORM";
  }
  return linea;
}

void controlarLEDsIndicadores() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);
  if (!sistemaActivo) return;
  
  bool hayProblema = (temperatura > TEMP_AMBIENTE_MAX || 
                     estadoSecado == "ERROR_SENSOR" ||
                     estadoSecado == "T.ALTA");
  bool condicionesOptimas = (humedadAmbiente >= HUMEDAD_OBJETIVO_MIN && 
                            humedadAmbiente <= HUMEDAD_OBJETIVO_MAX &&
                            temperatura >= TEMP_AMBIENTE_MIN && 
                            temperatura <= TEMP_AMBIENTE_OPTIMA &&
                            estadoSecado == "OPTIMO");
  
  if (hayProblema) {
    digitalWrite(LED_ROJO, HIGH);
  } else if (condicionesOptimas) {
    ledAmarilloEstado = !ledAmarilloEstado;
    digitalWrite(LED_AMARILLO, ledAmarilloEstado);
  } else {
    digitalWrite(LED_VERDE, HIGH);
  }
}

void mostrarReporteAmbiente() {
  Serial.println(F("=REPORTE="));
  Serial.print(F("T:")); Serial.print(temperatura,1);
  Serial.print(F(" H:")); Serial.println(humedadAmbiente,1);
  Serial.print(F("Calor:")); Serial.print(calorAutomatico);
  Serial.print(F("% pot:")); Serial.println(pot_value);
  Serial.print(F("Estado:")); Serial.println(estadoSecado);
  Serial.print(F("SD:")); Serial.print(sdOk?F("OK"):F("NO"));
  Serial.print(F(" Logs:")); Serial.println(count);
  Serial.println(F("========="));
}

void procesarComandos() {
  String comando = Serial.readStringUntil('\n');
  comando.trim();
  comando.toUpperCase();
  
  if (comando == "ON") {
    sistemaActivo = true;
    forzarActualizacion = true;
    Serial.println(F("ON"));
  } else if (comando == "OFF") {
    sistemaActivo = false;
    calorAutomatico = 0;
    pot_value_objetivo = 8000;
    estadoSecado = "APAGADO";
    forzarActualizacion = true;
    Serial.println(F("OFF"));
  } else if (comando == "STATUS") {
    Serial.println(F("=STATUS="));
    Serial.print(F("Sistema:")); Serial.println(sistemaActivo?F("ON"):F("OFF"));
    Serial.print(F("T:")); Serial.println(temperatura,1);
    Serial.print(F("H:")); Serial.println(humedadAmbiente,1);
    Serial.print(F("Calor:")); Serial.println(calorAutomatico);
    Serial.print(F("pot:")); Serial.print(pot_value);
    Serial.print(F("->")); Serial.println(pot_value_objetivo);
    Serial.print(F("Estado:")); Serial.println(estadoSecado);
    Serial.print(F("SD:")); Serial.println(sdOk?F("OK"):F("NO"));
    Serial.print(F("Logs:")); Serial.println(count);
    Serial.println(F("========"));
  } else if (comando == "HELP") {
    Serial.println(F("ON/OFF/STATUS/HELP"));
  }
}
