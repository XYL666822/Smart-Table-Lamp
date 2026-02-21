#include <LiquidCrystal.h>
#include <Countimer.h>
#include <Wire.h>
#include <NewPing.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>

// ====================== 引脚定义 ======================
#define LCD_RS 12
#define LCD_E 11
#define LCD_D4 5
#define LCD_D5 4
#define LCD_D6 3
#define LCD_D7 2
#define LDR A0
#define PIR 13
#define US_TRIG 9
#define US_ECHO 10
#define NEOPIXEL 6
SoftwareSerial espSerial(7, 8);

// ====================== 自动模式参数 ======================
#define AUTO_LIGHT_ON_VAL  500
#define MIN_BRIGHT         20
#define MAX_BRIGHT         255
#define BRIGHT_STEP        2

// ====================== 器件初始化 ======================
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
NewPing sonar(US_TRIG, US_ECHO, 50);
Adafruit_NeoPixel strip(12, NEOPIXEL, NEO_GRB + NEO_KHZ800);
Countimer timer;
Countimer studyTimer;

// ====================== 全局变量 ======================
bool isLampOn = false;
bool isItAutomatic = false;
bool isItExamMode = false;
bool studyMode = false;

int targetBright = 0;
int currentBright = 0;

int hours = 0, minutes = 0;
int studyModeHours = 0, studyModeMin = 0, studyModeInterval = 0;
int pir_state = LOW;

unsigned long lampTotalTime = 0;
unsigned long lampContiTime = 0;
unsigned long badPostureTime = 0;
unsigned long weakLightTime = 0;
unsigned long strongLightTime = 0;

#define BAD_POSTURE_DIST 30
#define WEAK_LIGHT_VAL 200
#define LIGHT_WARN 3600

String espCmdBuffer = "";

const char WIFI_SSID[] PROGMEM = "HUAWEI-0HII2";
const char WIFI_PWD[] PROGMEM = "dd20070527";

unsigned long lastRefresh = 0;
unsigned long lastSensorRefresh = 0;

// ====================== 函数声明 ======================
void initDevice();
void initESP();
void readSensor();
void countTimeStat();
void healthWarn();
void lampControl(bool state);
void updateAutoBright();
void neopixelWarn(uint8_t r, uint8_t g, uint8_t b, bool flash);
void serialInputNum(int &num);
void checkAutoLight();
void turnOnOffStudyMode();
void turnOnOffExamMode();
void turnOnOffManualLightMode();
void turnOnOffAutomaticLightMode();
void refreshClock();
void displayTheCurrentTimerDetails();
void onComplete();
void alertTheInterval();
void alertTheEndOfTheStudyMode();
void processSingleCharCmd(char cmdChar);
void handleEspStringCmd();
void espSyncData();
void readSerialCmd();
void showStatusLine2();
void updateLine1();

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  Wire.begin();
  initDevice();
  initESP();
  lcd.clear();
  updateLine1();
  showStatusLine2();

  targetBright = MIN_BRIGHT;
  currentBright = MIN_BRIGHT;
}

void loop() {
  readSensor();

  if (millis() - lastRefresh >= 1000) {
    lastRefresh = millis();
    countTimeStat();
  }

  timer.run();
  studyTimer.run();

  readSerialCmd();
  espSyncData();
  handleEspStringCmd();
  checkAutoLight();

  if (isItAutomatic && isLampOn) {
    updateAutoBright();
  }

  healthWarn();
}

// ====================== 第一行状态 ======================
void updateLine1() {
  lcd.setCursor(0, 0);
  if (isItAutomatic) {
    lcd.print("Auto Mode ON   ");
  } else if (isLampOn) {
    lcd.print("Lamp ON        ");
  } else {
    lcd.print("Standby        ");
  }
}

// ====================== 第二行显示 ======================
void showStatusLine2() {
  if (millis() - lastSensorRefresh < 2000) return;
  lastSensorRefresh = millis();

  int dist = sonar.ping_cm();
  if (dist == 0 || dist > 50) dist = 50;
  int light = analogRead(LDR);

  lcd.setCursor(0, 1);
  lcd.print("D:"); lcd.print(dist);
  lcd.print(" L:"); lcd.print(light);
  lcd.print(" P:");
  lcd.print(pir_state ? "ON " : "OFF");
}

// ====================== 初始化 ======================
void initDevice() {
  lcd.begin(16, 2);
  lcd.print("Vision Care");
  delay(1000);
  strip.begin();
  strip.clear();
  strip.show();
  pinMode(PIR, INPUT);
}

void initESP() {
  espSerial.println("AT");
  delay(500);
  espSerial.println("AT+CWMODE=1");
  delay(500);
  espSerial.println("AT+CWJAP=\"HUAWEI-0HII2\",\"dd20070527\"");
  delay(500);
}

void readSensor() {
  pir_state = digitalRead(PIR);
}

void countTimeStat() {
  if (isLampOn && pir_state) {
    lampContiTime++;
  } else {
    lampContiTime = 0;
  }
}

void healthWarn() {
  if (!studyMode && !isItExamMode) {
    updateLine1();
    showStatusLine2();
  }
}

// ====================== 基础灯控 ======================
void lampControl(bool state) {
  isLampOn = state;
  if (state) {
    strip.fill(strip.Color(255, 240, 200));
    strip.show();
  } else {
    strip.clear();
    strip.show();
  }
}

// ====================== 平滑自动调光 ======================
void updateAutoBright() {
  if (currentBright < targetBright) {
    currentBright += BRIGHT_STEP;
    if (currentBright > targetBright) currentBright = targetBright;
  } else if (currentBright > targetBright) {
    currentBright -= BRIGHT_STEP;
    if (currentBright < targetBright) currentBright = targetBright;
  }

  uint8_t r = map(currentBright, 0, 255, 0, 255);
  uint8_t g = map(currentBright, 0, 255, 0, 240);
  uint8_t b = map(currentBright, 0, 255, 0, 200);
  strip.fill(strip.Color(r, g, b));
  strip.show();
}

// 警告闪烁
void neopixelWarn(uint8_t r, uint8_t g, uint8_t b, bool flash) {
  for (int i = 0; i < 3; i++) {
    strip.fill(strip.Color(r, g, b));
    strip.show();
    delay(200);
    strip.clear();
    strip.show();
    delay(200);
  }
  if (isLampOn && isItAutomatic) {
    updateAutoBright();
  }
}

// ====================== 指令处理 ======================
void processSingleCharCmd(char c) {
  if (c == 'M' || c == 'm') turnOnOffManualLightMode();
  if (c == 'A' || c == 'a') turnOnOffAutomaticLightMode();
  if (c == 'S' || c == 's') turnOnOffStudyMode();
  if (c == 'E' || c == 'e') turnOnOffExamMode();
}

void handleEspStringCmd() {
  if (espCmdBuffer.length() == 0) return;
  String cmd = espCmdBuffer;
  cmd.trim();
  espCmdBuffer = "";

  if (cmd.length() == 1) {
    processSingleCharCmd(cmd[0]);
    return;
  }

  if (cmd.equalsIgnoreCase("on")) {
    isItAutomatic = false;
    lampControl(true);
    lcd.clear();
    updateLine1();
    showStatusLine2();
  } else if (cmd.equalsIgnoreCase("off")) {
    isItAutomatic = false;
    lampControl(false);
    lcd.clear();
    updateLine1();
    showStatusLine2();
  }
}

void espSyncData() {
  while (espSerial.available()) {
    char c = espSerial.read();
    if (!studyMode && !isItExamMode) {
      if (c == '\n' || c == '\r') {
        espCmdBuffer = "";
      } else {
        espCmdBuffer += c;
      }
    }
  }
}

void readSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    processSingleCharCmd(c);
  }
}

// ====================== 手动 / 自动模式 ======================
void turnOnOffManualLightMode() {
  isItAutomatic = false;
  lampControl(!isLampOn);
  lcd.clear();
  updateLine1();
  showStatusLine2();
}

void turnOnOffAutomaticLightMode() {
  isItAutomatic = !isItAutomatic;
  lcd.clear();
  updateLine1();
}

// ====================== 自动模式核心 ======================
void checkAutoLight() {
  if (!isItAutomatic) return;

  int val = analogRead(LDR);

  if (val < AUTO_LIGHT_ON_VAL) {
    isLampOn = true;
    targetBright = map(val, 0, AUTO_LIGHT_ON_VAL, MAX_BRIGHT, MIN_BRIGHT);
    targetBright = constrain(targetBright, MIN_BRIGHT, MAX_BRIGHT);
  } else {
    isLampOn = false;
    strip.clear();
    strip.show();
  }
}

// ====================== 数字输入 ======================
void serialInputNum(int &num) {
  char buf[16] = {0};
  int i = 0;
  lcd.clear();
  lcd.print("Input Num #");
  showStatusLine2();

  while (1) {
    if (espSerial.available()) {
      char c = espSerial.read();
      if (c == '#') {
        buf[i] = 0;
        num = atoi(buf);
        lcd.clear();
        lcd.print("Num:");
        lcd.print(num);
        showStatusLine2();
        delay(500);
        return;
      } else if (c >= '0' && c <= '9') {
        buf[i++] = c;
        lcd.setCursor(i, 0);
        lcd.print(c);
      }
    }
  }
}

// ====================== 考试模式 ======================
// 【这里修复：Exam结束强制同步关灯】
void onComplete() {
  isItExamMode = false;
  lcd.clear();
  lcd.print("Time Up!");
  showStatusLine2();
  neopixelWarn(255, 0, 0, true);
  delay(2000);

  // 修复：考试结束 → 强制关灯，状态同步
  lampControl(false);

  lcd.clear();
  updateLine1();
  showStatusLine2();
}

void refreshClock() {
  lcd.setCursor(0, 0);
  lcd.print(timer.getCurrentTime());
  showStatusLine2();
}

void turnOnOffExamMode() {
  if (!isItExamMode) {
    isItAutomatic = false;
    lampControl(true);
    lcd.clear();
    lcd.print("Exam Mode ON");
    showStatusLine2();
    delay(1000);
    lcd.clear();
    lcd.print("Set Hours");
    showStatusLine2();
    serialInputNum(hours);
    lcd.clear();
    lcd.print("Set Mins");
    showStatusLine2();
    serialInputNum(minutes);
    timer.setCounter(hours, minutes, 0, timer.COUNT_DOWN, onComplete);
    timer.setInterval(refreshClock, 1000);
    timer.start();
    isItExamMode = true;
    lcd.clear();
    lcd.print(timer.getCurrentTime());
    showStatusLine2();
  }
}

// ====================== 学习模式 ======================
void displayTheCurrentTimerDetails() {
  lcd.setCursor(0, 0);
  lcd.print(studyTimer.getCurrentTime());
  showStatusLine2();
}

void alertTheInterval() {
  studyTimer.pause();
  lcd.clear();
  lcd.print("Break Time!");
  showStatusLine2();
  neopixelWarn(0, 255, 255, true);
  delay(2000);
  studyTimer.start();
}

void alertTheEndOfTheStudyMode() {
  studyMode = false;
  lcd.clear();
  lcd.print("Study Done!");
  showStatusLine2();
  neopixelWarn(0, 255, 0, true);
  delay(2000);

  // 修复：学习结束 → 强制关灯
  lampControl(false);

  lcd.clear();
  updateLine1();
  showStatusLine2();
}

void turnOnOffStudyMode() {
  if (!studyMode) {
    isItAutomatic = false;
    lampControl(true);
    lcd.clear();
    lcd.print("Study Mode ON");
    showStatusLine2();
    delay(1000);
    lcd.clear();
    lcd.print("Set Study H");
    showStatusLine2();
    serialInputNum(studyModeHours);
    lcd.clear();
    lcd.print("Set Study M");
    showStatusLine2();
    serialInputNum(studyModeMin);
    lcd.clear();
    lcd.print("Set Break M");
    showStatusLine2();
    serialInputNum(studyModeInterval);
    studyTimer.setCounter(studyModeHours, studyModeMin, 0, studyTimer.COUNT_UP, alertTheEndOfTheStudyMode);
    studyTimer.setInterval(alertTheInterval, studyModeInterval * 60000);
    studyTimer.setInterval(displayTheCurrentTimerDetails, 1000);
    studyTimer.start();
    studyMode = true;
    lcd.clear();
    lcd.print(studyTimer.getCurrentTime());
    showStatusLine2();
  }
}