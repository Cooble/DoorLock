/*
  Controlling a servo position using a potentiometer (variable resistor)
  by Michal Rinott <http://people.interaction-ivrea.it/m.rinott>

  modified on 8 Nov 2013
  by Scott Fitzgerald
  http://www.arduino.cc/en/Tutorial/Knob
*/

#include <Servo.h>

Servo myservo;  // create servo object to control a servo

#define PIN_DOOR 6
#define PIN_SWITCH 7
#define PIN_LED 8
#define PIN_SERVO 9
#define PIN_KNOCK 5

#define RELAY_0 10
#define RELAY_1 11
#define RELAY_2 12
#define RELAY_3 13



#define MAX_KNOCKS 3
#define MAX_KNOCKS_DELAY 2000//in millis, after this listening to knocks resets
#define MIN_KNOCKS_DELAY 100 //in millis, wait for this period before listening to next knock
#define KNOCK_PRECISION 0.10 //+- this thing is vlaid
#define KNOCK_THRESHOLD 100 //+- in bits how big number read by analog read is valid knock
#define SAFETY_UNLOCK_TIME 0//300000 //+- in ticks how many seconds it takes to unlock door automaticaly 0 if disabled


bool ignoreSwitch;
bool locked;
bool pressed;
bool superlock;
bool closed;
bool shouldLock;
uint16_t ticks;
int waitForLock;
int ticksAttached;
int maxLoc = 10;
long servoUsed;
unsigned long lifeTime;

bool relays[4];
long startTimes[4];
long stopTimes[4];
long currentMinutesMEGA;//expressed in mins, resets each day true time of atmega
long currentMinutes;//expressed in mins, resets each day
long timeOffset;//expressed in mins, difference between atmega clock and user selected time
int socketTime;

void setup() {
  for (int i = 0; i < 4; i++) {
    relays[i] = true;
  }
  refreshSockets();
  pinMode(RELAY_0, OUTPUT);
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  for (int i = 0; i < 4; i++) {
    relays[i] = true;
  }
  refreshSockets();
  Serial.begin(9600);
  /* while(true){
     uint16_t val = analogRead(5);
     if(val>100){
       Serial.println(val);
     }
     delay(1);
    }*/
  socketTime = -1;
  delay(1000);
  lock(false);
  Serial.println("Ahoj");
}
long last24;//number of mins last midnight
long last;
void timeUpdate() {
  long now = millis();
  if (now - last > 60000) { //more than min passed
    currentMinutesMEGA = now / 60000 - last24;
    if (currentMinutesMEGA > 60 * 24) { //more than a day
      last24 += 60 * 24;
      currentMinutesMEGA %= 60 * 24;
    }
    currentMinutes = timeOffset + currentMinutesMEGA;
    currentMinutes %= 60 * 24;
    last = now;
    clockUpdate();
  }
}
void setTime(long targetMins) {
  timeOffset = targetMins - currentMinutesMEGA;
  currentMinutes = timeOffset + currentMinutesMEGA;
}
void clockUpdate() {
  for (int i = 0; i < 4; i++) {
    if (startTimes[i] != 0 && startTimes[i] == currentMinutes) {
      Serial.print("enabling ");
      Serial.println(i);
      relays[i] = true;
    }
    else if (stopTimes[i] != 0 && stopTimes[i] == currentMinutes) {
      relays[i] = false;
      Serial.print("disabling ");
      Serial.println(i);
    }
  }
  refreshSockets();

}
bool recording = false;
double knocks[] = {0.69, 1.00, 0.31, 0.35};
double closeKnocks[] = {1, 1, 1, 1};
uint16_t currentKnocks[MAX_KNOCKS];
uint8_t knockNumber = 1;
uint8_t currentKnock;
long lastMillis;
bool knocking;
uint8_t knockingDelay;

void knockUpdate() {
  if (knockingDelay > millis())
    return;
  uint16_t value = analogRead(PIN_KNOCK);
  if (value > KNOCK_THRESHOLD) {
    if (knocking) {
      if ((millis() - lastMillis) < MIN_KNOCKS_DELAY)
        return;
    }
  }
  if (value > KNOCK_THRESHOLD) {
    if (currentKnock == 0 && !knocking) {
      Serial.println("first knok");
      knocking = true;
      lastMillis = millis();
    }
    else if (currentKnock == MAX_KNOCKS) {
      currentKnock == 0;
      currentKnocks[MAX_KNOCKS - 1] = millis() - lastMillis;
      knockCheck();
      return;
    }
    else {
      Serial.print("knok: ");
      Serial.println(currentKnock);
      currentKnock++;
      long now = millis();
      currentKnocks[currentKnock - 1] = now - lastMillis;
      lastMillis = now;
    }
  } else if (knocking && (millis() - lastMillis > MAX_KNOCKS_DELAY)) {
    Serial.println("Timeout!");
    knockClear();

  }
}
void knockClear() {
  knocking = false;
  currentKnock = 0;
}

void knockCheck() {
  knockClear();
  Serial.println("Checking knocking!");
  knockingDelay = millis() + 500;
  double biggestElement = 0;
  for (int i = 0; i < MAX_KNOCKS; i++) {
    biggestElement = max(biggestElement, currentKnocks[i]);
  }
  bool success = true;
  for (int i = 0; i < MAX_KNOCKS; i++) {
    double v = ((double)currentKnocks[i]) / biggestElement;
    double prec = KNOCK_PRECISION;
    if (recording)
      prec = 1;
    if (!(v > knocks[i] - prec && v < knocks[i] + prec)) { //is not in bounds
      success = false;
      Serial.print("Invalid knocking!");
      //Serial.println(v);
      break;
    } //else Serial.println(v);
  }
  if (success) {
    Serial.println("Knock success!");
  }
  if (success && isClosed() && locked)
    lock(false);
}


void commandInput() {
  if (Serial.available()) {
    lifeTime = 0;
    String s = Serial.readString();
    if (s.equalsIgnoreCase("lock")) {
      if (isClosed()) {
        lock(true);
      } else Serial.println("Door must be closed!");
    } else if (s.equals("unlock")) {
      lock(false);
    }
    else if (s.equalsIgnoreCase("autolock")) {
      if (!isClosed()) {
        waitForLock = maxLoc;
        Serial.println("Autolock active");
      } else Serial.println("Not possible! Door is closed");
    }
    else if (s.equalsIgnoreCase("switch")) {
      ignoreSwitch = !ignoreSwitch;
      Serial.print("Ignore switch mode ");
      Serial.println(ignoreSwitch ? "active" : "deactive");
    }
    else if (s.equalsIgnoreCase("door")) {
      Serial.print("Door is: ");
      Serial.println(locked ? "locked" : (isClosed() ? "closed" : "open"));
    }
    else if (s.equalsIgnoreCase("superlock")) {
      superlock = !superlock;
      Serial.print("Superlock ");
      Serial.println(superlock ? "active" : "deactive");
      if (superlock && isClosed()) {
        lock(true);
      }
    }
    else if (s.equalsIgnoreCase("seal")) {
      superlock = true;
      if (isClosed())
        lock(true);
      ignoreSwitch = true;
      Serial.println("Door locked, switch ignored");
    }
    else if (s.equalsIgnoreCase("free")) {
      if (isClosed())
        lock(false);
      ignoreSwitch = true;
      superlock = false;
      Serial.println("Door constantly onlocked, switch ignored");
    }
    else if (s.equalsIgnoreCase("reset")) {
      superlock = false;
      ignoreSwitch = false;
      shouldLock = false;
      Serial.println("Normal mode active");
    }
    else if (s.startsWith("on")) {
      if (s.length() == 2) {
        for (int i = 0; i < 4; i++) {
          relays[i] = true;
        }
        Serial.println("All Sockets are on");
        refreshSockets();
        return;
      }
      s = s.substring(2);

      int i = s.toInt();
      i %= 4;
      relays[i] = true;
      Serial.println("Socket " + s + " is on");
      refreshSockets();
    }
    else if (s.startsWith("off")) {
      if (s.length() == 3) {
        for (int i = 0; i < 4; i++) {
          relays[i] = false;
        }
        refreshSockets();
        Serial.println("All Sockets are off");
        return;
      }
      s = s.substring(3);
      int i = s.toInt();
      i %= 4;
      relays[i] = false;
      Serial.println("Socket " + s + " is off");
      refreshSockets();
    }
    else if (s.startsWith("time")) {
      if (s.length() == 4) {
        Serial.print("Current time: ");
        Serial.print(currentMinutes / 60);
        Serial.print(":");
        Serial.println(currentMinutes % 60);
        return;
      }
      s = s.substring(4);
      int i = s.toInt();
      Serial.print("Editing time of socket: ");
      Serial.println(i);
      socketTime = i;
    }
    else if (s.startsWith("start")) {
      if (socketTime < 0) {
        Serial.println("You need to choose socket first using \"time[socket_index]\"");
        return;
      }
      String ss = s.substring(5, 7);
      int hour = ss.toInt();

      ss = s.substring(8, 10);
      int min = ss.toInt();
      startTimes[socketTime] = hour * 60 + min;


      Serial.print("Socket will be set on at ");
      printTime(hour * 60 + min);
    }
    else if (s.startsWith("stop")) {
      if (socketTime < 0) {
        Serial.println("You need to choose socket first using \"time[socket_index]\"");
        return;
      }
      String ss = s.substring(4, 6);
      int hour = ss.toInt();

      ss = s.substring(7, 9);
      int min = ss.toInt();
      stopTimes[socketTime] = hour * 60 + min;


      Serial.print("Socket will be set off at ");
      printTime(hour * 60 + min);
    }
    else if (s.startsWith("settm")) {
      String ss = s.substring(5, 7);
      int hour = ss.toInt();

      ss = s.substring(8, 10);
      int min = ss.toInt();
      setTime(hour * 60 + min);

      Serial.print("Time set to ");
      printTime(hour * 60 + min);
    }
  }
}
void printTime(long minutes) {
  Serial.print(minutes / 60);
  Serial.print(":");
  Serial.println(minutes % 60);
}
void refreshSockets() {
  digitalWrite(RELAY_0, relays[0]);
  digitalWrite(RELAY_1, relays[1]);
  digitalWrite(RELAY_2, relays[2]);
  digitalWrite(RELAY_3, relays[3]);
}
void loop() {
  timeUpdate();
  commandInput();
  knockUpdate();
  if (!closed && digitalRead(PIN_DOOR)) {
    isClosed();
    onDoorClosed();
  } else if (closed && !digitalRead(PIN_DOOR)) {
    isClosed();
    onDoorOpen();
  }
  if (!digitalRead(PIN_SWITCH) && !ignoreSwitch) {
    if (!pressed) {
      pressed = true;

      if (!locked) {
        if (!isClosed()) {
          digitalWrite(PIN_LED, true);
          delay(100);
          digitalWrite(PIN_LED, false);

        } else
          lock(true);
      }
      else {
        lock(false);
      }
    }
  } else {
    pressed = false;
    ticks = 0;
  }
  if (!isClosed()) {
    ticks++;
    if (ticks > 1000) {
      ticks = 0;
      shouldLock = true;
      digitalWrite(PIN_LED, true);
    }
  }


  if (isClosed()) {
    if (waitForLock > 0) {
      waitForLock--;
      if (waitForLock == 0) {
        lock(true);
        shouldLock = false;
      }
    }
  }
  if (servoUsed < millis() && servoUsed != 0) {
    servoUsed = 0;
    myservo.detach();
    digitalWrite(PIN_SERVO, false);
  }
}
void onDoorClosed() {
  if (superlock) {
    shouldLock = true;
  }
  if (shouldLock)
    waitForLock = maxLoc;
}
void onDoorOpen() {
  if (superlock) {
    digitalWrite(PIN_LED, true);
  }
}
bool isClosed() {
  closed = digitalRead(PIN_DOOR);
  return closed;
}
void lock(bool loc) {
  myservo.attach(PIN_SERVO);
  servoUsed = millis() + 2000; //10sec
  Serial.println(loc ? "Locked" : "Unlocked");
  locked = loc;
  myservo.write(locked ? 0 : 180);
  digitalWrite(PIN_LED, locked);
}

