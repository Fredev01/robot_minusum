#include <Wire.h>
#include <VL53L0X.h>

// Pines XSHUT para sensores VL53L0X
#define XSHUT_PIN1 11 // Sensor frontal
#define XSHUT_PIN2 12 // Sensor  derecho
#define XSHUT_PIN3 13 // Sensor izquierdo

// Direcciones I2C para sensores
constexpr uint8_t LOX1_ADDRESS = 0x30;
constexpr uint8_t LOX2_ADDRESS = 0x31;
constexpr uint8_t LOX3_ADDRESS = 0x32;

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
const uint16_t MAX_VALID_DISTANCE = 900;       // Distancia máxima válida (mm)
const unsigned long SEARCH_CHANGE_TIME = 5000; // Tiempo para cambiar dirección de búsqueda (ms)
const unsigned long STUCK_TIMEOUT = 3500;      // Tiempo para detectar estancamiento (ms)

// Variables para almacenar mediciones
uint16_t dist1, dist2, dist3;
bool isLineFrontDetected = true;
bool isLineBackDetected = true;

// Variables de control
unsigned long lastSearchChange = 0; // Tiempo de último cambio de dirección de búsqueda
unsigned long lastMoveChange = 0;   // Tiempo de último cambio de movimiento
bool searchDirection = true;        // true = horario, false = antihorario
bool isStuck = false;               // Bandera de estancamiento
bool edgeDetected = false;          // Bandera de detección de borde

const uint8_t PRIORITY_EDGE = 1;   // Máxima prioridad: evitar salirse del dohyo
const uint8_t PRIORITY_ATTACK = 2; // Segunda prioridad: atacar al oponente
const uint8_t PRIORITY_SEARCH = 3; // Menor prioridad: buscar al oponente

uint8_t currentAction = PRIORITY_SEARCH;

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
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENB, 0);
}

void updateCurrentAction()
{
  if (!isLineFrontDetected || !isLineBackDetected)
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

  currentAction = PRIORITY_SEARCH;
}

// Maniobra para evitar salirse del dohyo
void avoidEdge()
{
  stopMotors();
  delay(50);

  if (!isLineFrontDetected)
  {
    moveBackward(EDGE_RETREAT_SPEED);
    delay(300);

    turnRight(TURN_SPEED);
    delay(200);
  }
  else if (!isLineBackDetected)
  {
    moveForward(EDGE_RETREAT_SPEED);
    delay(300);

    turnRight(TURN_SPEED);
    delay(200);
  }

  // Actualizar tiempo de último movimiento para evitar detección de estancamiento
  lastMoveChange = millis();
}

void attackOpponent()
{
  if (dist1 < OPPONENT_THRESHOLD && dist1 > MIN_VALID_DISTANCE)
  {
    forwardAttack();
  }
  else if (dist2 < OPPONENT_THRESHOLD && dist2 > MIN_VALID_DISTANCE)
  {
    turnLeft(TURN_SPEED);
    delay(100);
    forwardAttack();
  }
  else if (dist3 < OPPONENT_THRESHOLD && dist3 > MIN_VALID_DISTANCE)
  {
    turnRight(TURN_SPEED);
    delay(100);
    forwardAttack();
  }

  lastMoveChange = millis();
}

void checkIfStuck()
{
  if (millis() - lastMoveChange > STUCK_TIMEOUT)
  {
    isStuck = true;
    lastMoveChange = millis();
  }
  else
  {
    isStuck = false;
  }
}

void escapeManeuver()
{

  moveBackward(ESCAPE_SPEED);
  delay(300);

  if (random(2) == 0)
  {
    turnLeft(TURN_SPEED);
  }
  else
  {
    turnRight(TURN_SPEED);
  }
  delay(200);

  isStuck = false;
  lastMoveChange = millis();
}

void searchMode()
{
  unsigned long currentTime = millis();

  if (currentTime - lastSearchChange > SEARCH_CHANGE_TIME)
  {
    searchDirection = !searchDirection;
    lastSearchChange = currentTime;
  }

  if (searchDirection)
  {
    turnRight(SEARCH_SPEED);
  }
  else
  {
    turnLeft(SEARCH_SPEED);
  }
}

void setup()
{
  Serial.begin(9600);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  pinMode(TCRT_FRONT, INPUT);
  pinMode(TCRT_BACK, INPUT);

  stopMotors();

  pinMode(XSHUT_PIN1, OUTPUT);
  pinMode(XSHUT_PIN2, OUTPUT);
  pinMode(XSHUT_PIN3, OUTPUT);

  digitalWrite(XSHUT_PIN1, LOW);
  digitalWrite(XSHUT_PIN2, LOW);
  digitalWrite(XSHUT_PIN3, LOW);
  delay(5);

  Wire.begin();

  digitalWrite(XSHUT_PIN1, HIGH);
  delay(5);
  if (!sensor1.init())
  {
    Serial.println("Error inicializando sensor frontal");
  }
  sensor1.setAddress(LOX1_ADDRESS);
  sensor1.setTimeout(50);

  digitalWrite(XSHUT_PIN2, HIGH);
  delay(5);
  if (!sensor2.init())
  {
    Serial.println("Error inicializando sensor izquierdo");
  }
  sensor2.setAddress(LOX2_ADDRESS);
  sensor2.setTimeout(50);

  digitalWrite(XSHUT_PIN3, HIGH);
  delay(5);
  if (!sensor3.init())
  {
    Serial.println("Error inicializando sensor derecho");
  }
  sensor3.setAddress(LOX3_ADDRESS);
  sensor3.setTimeout(50);

  sensor1.setMeasurementTimingBudget(20000); // 20ms
  sensor2.setMeasurementTimingBudget(20000);
  sensor3.setMeasurementTimingBudget(20000);

  sensor1.startContinuous();
  sensor2.startContinuous();
  sensor3.startContinuous();

  delay(4000);
}

void loop()
{
  dist1 = sensor1.readRangeContinuousMillimeters();
  dist2 = sensor2.readRangeContinuousMillimeters();
  dist3 = sensor3.readRangeContinuousMillimeters();
  // Serial.print("Distancias: ");
  // Serial.print("frente: ");
  // Serial.print(dist1);
  // Serial.print(" mm, ");
  // Serial.print("izquierdo: ");
  // Serial.print(dist2);
  // Serial.print(" mm, ");
  // Serial.print("derecho: ");
  // Serial.print(dist3);
  // Serial.println(" mm");

  // isLineFrontDetected = digitalRead(TCRT_FRONT);
  // isLineBackDetected = digitalRead(TCRT_BACK);
  // Serial.print("Lineas: ");
  // Serial.print("frente: ");
  // Serial.print(isLineFrontDetected);
  // Serial.print(", ");
  // Serial.print("atrás: ");
  // Serial.println(isLineBackDetected);

  if (dist1 == 0 || dist1 > MAX_VALID_DISTANCE)
    dist1 = MAX_VALID_DISTANCE;
  if (dist2 == 0 || dist2 > MAX_VALID_DISTANCE)
    dist2 = MAX_VALID_DISTANCE;
  if (dist3 == 0 || dist3 > MAX_VALID_DISTANCE)
    dist3 = MAX_VALID_DISTANCE;

  checkIfStuck();

  updateCurrentAction();

  switch (currentAction)
  {
  case PRIORITY_EDGE:
    avoidEdge();
    break;

  case PRIORITY_ATTACK:
    attackOpponent();
    break;

  case PRIORITY_SEARCH:
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

  delay(10);
}
