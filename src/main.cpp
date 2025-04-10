#include <Wire.h>
#include <VL53L0X.h> // Usar biblioteca VL53L0X de Pololu

// Pines XSHUT para sensores VL53L0X
#define XSHUT_PIN1 11 // Sensor frontal
#define XSHUT_PIN3 13 // Sensor derecho
#define XSHUT_PIN2 12 // Sensor izquierdo

// Direcciones I2C para sensores
#define LOX1_ADDRESS 0x30
#define LOX2_ADDRESS 0x31
#define LOX3_ADDRESS 0x32

// Pines para sensores TCRT5000 (detección de borde)
#define TCRT_FRONT A0 // Sensor frontal de línea
#define TCRT_BACK A1  // Sensor trasero de línea

// Pines para L298N - Un módulo para controlar 2 pares de motores
// Motor A (Par Izquierdo)
const int IN1 = 7;
const int IN2 = 8;
const int ENA = 3; // Pin PWM para velocidad motores izquierdos

// Motor B (Par Derecho)
const int IN3 = 9;
const int IN4 = 10;
const int ENB = 5; // Pin PWM para velocidad motores derechos

// Velocidades para diferentes acciones
const int ATTACK_SPEED = 255;       // Velocidad máxima para ataque
const int TURN_SPEED = 200;         // Velocidad para giros
const int SEARCH_SPEED = 100;       // Velocidad baja para búsqueda
const int ESCAPE_SPEED = 180;       // Velocidad para maniobras de escape
const int EDGE_RETREAT_SPEED = 220; // Velocidad para retirarse del borde

// Crear objetos para sensores
VL53L0X sensor1; // Frontal
VL53L0X sensor2; // Izquierdo
VL53L0X sensor3; // Derecho

// Umbrales y constantes
const uint16_t OPPONENT_THRESHOLD = 350;       // Umbral de detección (mm) para dohyo de 80cm
const uint16_t MIN_VALID_DISTANCE = 20;        // Distancia mínima válida (mm)
const uint16_t MAX_VALID_DISTANCE = 1200;      // Distancia máxima válida (mm)
const unsigned long SEARCH_CHANGE_TIME = 5000; // Tiempo para cambiar dirección de búsqueda (ms)
const unsigned long STUCK_TIMEOUT = 3000;      // Tiempo para detectar estancamiento (ms)

// Variables para almacenar mediciones
uint16_t dist1, dist2, dist3;
bool tcrtFrontValue, tcrtBackValue;

// Variables de control
unsigned long lastSearchChange = 0; // Tiempo de último cambio de dirección de búsqueda
unsigned long lastMoveChange = 0;   // Tiempo de último cambio de movimiento
bool searchDirection = true;        // true = horario, false = antihorario
bool isStuck = false;               // Bandera de estancamiento
bool edgeDetected = false;          // Bandera de detección de borde

// Prioridades para acciones
const int PRIORITY_EDGE = 1;   // Máxima prioridad: evitar salirse del dohyo
const int PRIORITY_ATTACK = 2; // Segunda prioridad: atacar al oponente
const int PRIORITY_SEARCH = 3; // Menor prioridad: buscar al oponente

// Variable para almacenar la acción actual
int currentAction = PRIORITY_SEARCH;

void moveForward(int speed)
{
  // Par izquierdo adelante
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, speed);

  // Par derecho adelante
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENB, speed);
}

void forwardAttack()
{
  moveForward(ATTACK_SPEED);
}

// Control de motores con PWM para L298N

void moveBackward(int speed)
{
  // Par izquierdo atrás
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  analogWrite(ENA, speed);

  // Par derecho atrás
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  analogWrite(ENB, speed);
}

void turnLeft(int speed)
{
  // Par izquierdo atrás
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  analogWrite(ENA, speed);

  // Par derecho adelante
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENB, speed);
}

void turnRight(int speed)
{
  // Par izquierdo adelante
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, speed);

  // Par derecho atrás
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  analogWrite(ENB, speed);
}

void stopMotors()
{
  // Todos los motores detenidos
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENB, 0);
}

// Actualizar la acción actual según prioridades
void updateCurrentAction()
{
  // Verificar detección de borde (prioridad máxima)
  if (!tcrtFrontValue || !tcrtBackValue)
  {
    currentAction = PRIORITY_EDGE;
    edgeDetected = true;
    return;
  }
  else
  {
    edgeDetected = false;
  }

  // Verificar detección de oponente (segunda prioridad)
  if ((dist1 < OPPONENT_THRESHOLD && dist1 > MIN_VALID_DISTANCE) ||
      (dist2 < OPPONENT_THRESHOLD && dist2 > MIN_VALID_DISTANCE) ||
      (dist3 < OPPONENT_THRESHOLD && dist3 > MIN_VALID_DISTANCE))
  {
    currentAction = PRIORITY_ATTACK;
    return;
  }

  // Si no hay borde ni oponente, modo búsqueda
  currentAction = PRIORITY_SEARCH;
}

// Maniobra para evitar salirse del dohyo
void avoidEdge()
{
  // Detener motores brevemente
  stopMotors();
  delay(50);

  // Determinar qué sensor detectó el borde
  if (!tcrtFrontValue)
  {
    Serial.println("¡Borde detectado ADELANTE! Retrocediendo...");
    // Si el sensor frontal detecta el borde, retroceder
    moveBackward(EDGE_RETREAT_SPEED);
    delay(300); // Retroceder durante un tiempo suficiente

    // Girar para alejarse más del borde
    turnRight(TURN_SPEED);
    delay(200);
  }
  else if (!tcrtBackValue)
  {
    Serial.println("¡Borde detectado ATRÁS! Avanzando...");
    // Si el sensor trasero detecta el borde, avanzar
    moveForward(EDGE_RETREAT_SPEED);
    delay(300); // Avanzar durante un tiempo suficiente

    // Girar para alejarse más del borde
    turnRight(TURN_SPEED);
    delay(200);
  }

  // Actualizar tiempo de último movimiento para evitar detección de estancamiento
  lastMoveChange = millis();
}

// Función para atacar al oponente según la posición detectada
void attackOpponent()
{
  if (dist1 < OPPONENT_THRESHOLD && dist1 > MIN_VALID_DISTANCE)
  {
    // ESCENARIO 1: Oponente al frente - Ataque directo
    Serial.println("¡Ataque frontal!");
    forwardAttack();
  }
  else if (dist2 < OPPONENT_THRESHOLD && dist2 > MIN_VALID_DISTANCE)
  {
    // ESCENARIO 2: Oponente a la izquierda - Girar y atacar
    Serial.println("¡Oponente a la izquierda!");
    turnLeft(TURN_SPEED);
    delay(100); // Pequeña pausa para completar el giro
    forwardAttack();
  }
  else if (dist3 < OPPONENT_THRESHOLD && dist3 > MIN_VALID_DISTANCE)
  {
    // ESCENARIO 3: Oponente a la derecha - Girar y atacar
    Serial.println("¡Oponente a la derecha!");
    turnRight(TURN_SPEED);
    delay(100); // Pequeña pausa para completar el giro
    forwardAttack();
  }

  lastMoveChange = millis();
}

// Función para verificar estancamiento
void checkIfStuck()
{
  // Si llevamos mucho tiempo en la misma acción, podríamos estar estancados
  if (millis() - lastMoveChange > STUCK_TIMEOUT)
  {
    isStuck = true;
    Serial.println("¡Posible estancamiento detectado!");
    lastMoveChange = millis();
  }
  else
  {
    isStuck = false;
  }
}

// Maniobra de escape para situaciones de estancamiento
void escapeManeuver()
{
  Serial.println("Ejecutando maniobra de escape");

  // Retroceder
  moveBackward(ESCAPE_SPEED);
  delay(300);

  // Girar en dirección aleatoria
  if (random(2) == 0)
  {
    turnLeft(TURN_SPEED);
  }
  else
  {
    turnRight(TURN_SPEED);
  }
  delay(200);

  // Restablecer bandera de estancamiento
  isStuck = false;
  lastMoveChange = millis();
}

// Modo de búsqueda
void searchMode()
{
  unsigned long currentTime = millis();

  // Cambiar dirección de búsqueda periódicamente
  if (currentTime - lastSearchChange > SEARCH_CHANGE_TIME)
  {
    searchDirection = !searchDirection;
    lastSearchChange = currentTime;
    Serial.println("Cambiando dirección de búsqueda");
  }

  if (searchDirection)
  {
    Serial.println("Buscando (horario)");
    turnRight(SEARCH_SPEED);
  }
  else
  {
    Serial.println("Buscando (antihorario)");
    turnLeft(SEARCH_SPEED);
  }
}

// Ataque directo a máxima potencia

void setup()
{
  Serial.begin(9600); // Iniciar comunicación serial

  // Configurar pines de motores como salidas
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  // Configurar pines de sensores TCRT5000 como entradas
  pinMode(TCRT_FRONT, INPUT);
  pinMode(TCRT_BACK, INPUT);

  // Apagar motores inicialmente
  stopMotors();

  // Configurar pines XSHUT como salidas
  pinMode(XSHUT_PIN1, OUTPUT);
  pinMode(XSHUT_PIN2, OUTPUT);
  pinMode(XSHUT_PIN3, OUTPUT);

  // Apagar todos los sensores
  digitalWrite(XSHUT_PIN1, LOW);
  digitalWrite(XSHUT_PIN2, LOW);
  digitalWrite(XSHUT_PIN3, LOW);
  delay(5);

  // Iniciar Wire/I2C
  Wire.begin();

  // === INICIALIZAR SENSORES UNO POR UNO ===

  // Inicializar sensor frontal
  digitalWrite(XSHUT_PIN1, HIGH);
  delay(5);
  if (!sensor1.init())
  {
    Serial.println("Error inicializando sensor frontal");
  }
  sensor1.setAddress(LOX1_ADDRESS);
  sensor1.setTimeout(50);

  // Inicializar sensor izquierdo
  digitalWrite(XSHUT_PIN2, HIGH);
  delay(5);
  if (!sensor2.init())
  {
    Serial.println("Error inicializando sensor izquierdo");
  }
  sensor2.setAddress(LOX2_ADDRESS);
  sensor2.setTimeout(50);

  // Inicializar sensor derecho
  digitalWrite(XSHUT_PIN3, HIGH);
  delay(5);
  if (!sensor3.init())
  {
    Serial.println("Error inicializando sensor derecho");
  }
  sensor3.setAddress(LOX3_ADDRESS);
  sensor3.setTimeout(50);

  // Configurar sensores para modo rápido
  sensor1.setMeasurementTimingBudget(20000); // 20ms
  sensor2.setMeasurementTimingBudget(20000);
  sensor3.setMeasurementTimingBudget(20000);

  // Iniciar medición continua
  sensor1.startContinuous();
  sensor2.startContinuous();
  sensor3.startContinuous();

  // Esperar el tiempo reglamentario de 5 segundos antes de comenzar
  Serial.println("Esperando 5 segundos para iniciar...");
  delay(4000);

  Serial.println("¡Robot minisumo activado!");
}

void loop()
{
  // Leer sensores de distancia
  dist1 = sensor1.readRangeContinuousMillimeters();
  dist2 = sensor2.readRangeContinuousMillimeters();
  dist3 = sensor3.readRangeContinuousMillimeters();

  // Leer sensores de línea TCRT5000
  tcrtFrontValue = digitalRead(TCRT_FRONT);
  tcrtBackValue = digitalRead(TCRT_BACK);

  // Validar lecturas de sensores de distancia
  if (dist1 == 0 || dist1 > MAX_VALID_DISTANCE)
    dist1 = MAX_VALID_DISTANCE;
  if (dist2 == 0 || dist2 > MAX_VALID_DISTANCE)
    dist2 = MAX_VALID_DISTANCE;
  if (dist3 == 0 || dist3 > MAX_VALID_DISTANCE)
    dist3 = MAX_VALID_DISTANCE;

  // Mostrar datos para depuración
  Serial.print("F:");
  Serial.print(dist1);
  Serial.print(" I:");
  Serial.print(dist2);
  Serial.print(" D:");
  Serial.print(dist3);
  Serial.print(" TCRT-F:");
  Serial.print(tcrtFrontValue);
  Serial.print(" TCRT-B:");
  Serial.println(tcrtBackValue);

  // Verificar si estamos posiblemente estancados
  checkIfStuck();

  // Determinar la acción de mayor prioridad
  updateCurrentAction();

  // Ejecutar la acción de acuerdo a la prioridad
  switch (currentAction)
  {
  case PRIORITY_EDGE:
    // Maniobra para evitar salirse del dohyo
    avoidEdge();
    break;

  case PRIORITY_ATTACK:
    // Atacar al oponente
    attackOpponent();
    break;

  case PRIORITY_SEARCH:
    // Modo búsqueda
    if (isStuck)
    {
      escapeManeuver();
    }
    else
    {
      searchMode();
    }
    break;
  }

  // Pequeña pausa para estabilidad
  delay(10);
}
