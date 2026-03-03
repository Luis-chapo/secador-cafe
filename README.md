# Secador de Café — Control Automático con Arduino

Sistema electrónico de control automático de temperatura y humedad para el 
proceso de secado de café, con registro de datos en SD card. 
Desarrollado como solución de hardware + software para productores del norte 
del Perú.

## Problema que resuelve

El secado tradicional de café al sol expone el grano a condiciones climáticas 
impredecibles y requiere supervisión constante del agricultor. Este sistema 
automatiza el control del ambiente interior del secador, mantiene condiciones 
óptimas para alcanzar la humedad requerida de **10–12%** y registra todos los 
datos en una SD card para análisis posterior.

## ⚙️ Hardware

| Componente | Función |
|---|---|
| Arduino UNO | Microcontrolador principal |
| DHT22 | Sensor de temperatura y humedad ambiente |
| TRIAC BT136 | Control de potencia AC de focos infrarrojos |
| MOC3021 | Optoacoplador de disparo del TRIAC |
| H11AA1 | Detector de cruce por cero (zero-crossing) |
| Módulo SD card | Almacenamiento local de datos en CSV |
| LCD I2C 16×2 | Pantalla de monitoreo local |
| LEDs (verde/amarillo/rojo) | Indicadores visuales de estado |
| Botón pulsador | Expulsión segura SD / Reinicio del sistema |

## 🔌 Pines

| Pin Arduino | Componente |
|---|---|
| D2 | Detector cruce por cero (AC_IN_ZERO) |
| D9 | Disparo TRIAC |
| D8 | Sensor DHT22 |
| D10 | Chip Select (CS) módulo SD |
| D11 / D12 / D13 | SPI (MOSI / MISO / SCK) — SD card |
| D7 / D6 / D5 | LEDs Verde / Amarillo / Rojo |
| A0 | Botón expulsión SD / Reinicio |
| A4 / A5 (I2C) | LCD 16×2 |

## Lógica de control

El sistema regula la potencia del foco infrarrojo mediante **control de fase AC** 
(variación del ángulo de disparo del TRIAC), con transición suave para evitar 
picos bruscos. Lectura del sensor DHT22 cada **3 segundos** con filtro 
exponencial (90/10) para estabilizar valores.

| Estado | Humedad ambiente | Potencia calor |
|---|---|---|
| H.CRITICA 🔴 | > 25% | 90% |
| SECANDO 🟢 | 15% – 25% | 40% – 80% |
| AJUSTANDO 🟢 | 12% – 15% | 35% |
| ÓPTIMO ✅ | 10% – 12% | 20% |
| MUY_SECO | < 10% | 10% |
| T.ALTA 🔴 | T > 50°C | 0% (apagado) |
| T.CONTROL | T > 42°C | Reducción 40% |
| CALENTANDO | T < 35°C | +25% extra |

## 💾 Registro en SD card

Los datos se guardan automáticamente en `log_0.csv` cada **60 segundos**.

### Formato del archivo CSV

Num, Time_ms, Temp_C, Hum_%, Calor_%, Estado, potValue  
1, 60000, 42.30, 13.50, 35, AJUSTANDO, 6000  
2, 120000, 42.10, 12.80, 20, OPTIMO, 6500  

### Características del sistema de logging
- Recuperación automática del último número de registro al reiniciar
- Validación de datos antes de guardar (rango válido T: -40 a 80°C, H: 0 a 100%)
- Sistema de **5 reintentos** antes de deshabilitar la SD
- Funciona sin SD card (el sistema de control sigue activo)

## 🔘 Botón dual (Pin A0)

| Tiempo presionado | Acción |
|---|---|
| < 3 segundos | Cancelar / sin acción |
| 3 – 6 segundos (soltar) | ✅ Expulsión segura de SD |
| > 6 segundos | 🔄 Reinicio completo del sistema |

Durante la expulsión segura el sistema desactiva el logging, espera escrituras 
pendientes y muestra **"RETIRAR SD — SEGURO"** en la pantalla LCD.

## 📺 Pantalla LCD

T:42.3C H:13% SD ← Línea 1: Temperatura, Humedad, estado SD  
Auto:35% AJUS ← Línea 2: Potencia actual y estado de secado

## 🖥️ Comandos por Serial (9600 baud)

| Comando | Acción |
|---|---|
| `ON` | Encender sistema |
| `OFF` | Apagar sistema |
| `STATUS` | Ver estado completo (T, H, calor, SD, logs) |
| `HELP` | Lista de comandos |

## 📁 Estructura del repositorio

secador-cafe/  
├── src/  
│ └── secador_cafe.ino # Código fuente principal  
├── hardware/  
│ ├── esquematico.png # Diagrama de conexiones  
│ └── pcb.png # Diseño PCB  
├── fotos/  
│ ├── prototipo_1.jpg  
│ └── prototipo_2.jpg  
├── datos/  
│ └── ejemplo_log.csv # Ejemplo de datos registrados  
└── README.md  


## Librerías requeridas

SPI.h (incluida en Arduino IDE)  
SD.h (incluida en Arduino IDE)  
Wire.h (incluida en Arduino IDE)  
LiquidCrystal_I2C.h  
DHT.h (Adafruit DHT sensor library)  

## 👨‍💻 Desarrollador

Hardware + Software: **Luis Miguel Chapoñan Baldera**  
Ingeniería Electrónica — UNPRG  
🔗 [linkedin.com/in/luis-chapoñan](https://linkedin.com/in/luis-chapoñan)  
🏅 [credly.com/users/luis-miguel-chaponan-baldera](https://credly.com/users/luis-miguel-chaponan-baldera)
