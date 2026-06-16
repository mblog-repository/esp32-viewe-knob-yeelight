/*
 * Шаг 3 — управление лампой Yeelight энкодером (LAN-протокол).
 *
 * Плата: VIEWE UEDX24240013-MD50E — круглый дисплей 1.28" 240x240 (GC9A01) на ESP32-C3.
 * Модель управления: список параметров. Короткое нажатие кнопки — листать активный
 * параметр, поворот энкодера — менять его значение. Управляются обе зоны:
 *   Свет: вкл/выкл, яркость, цветовая температура.
 *   Подсветка (зад.): вкл/выкл, яркость, цвет (hue).
 *
 * Под лимит лампы ~60 команд/мин: значение применяется локально сразу, а в лампу
 * уезжает с задержкой (debounce). Соединение с лампой держим постоянным.
 *
 * ВАЖНО: Wi-Fi пароль и IP лампы — в secrets.h (он в .gitignore).
 *        Скопируй secrets.h.example -> secrets.h и впиши свои данные.
 * ВАЖНО: файл сохранён в UTF-8.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>
#include <WiFi.h>
#include <math.h>
#include "secrets.h"

// ---------- Распиновка платы (см. README) ----------
#define TFT_SCLK 1
#define TFT_MOSI 0
#define TFT_CS   10
#define TFT_DC   4
#define TFT_RST  -1
#define TFT_BL   8     // подсветка инверсная: LOW = ВКЛ
#define ENC_A    7     // энкодер фаза A
#define ENC_B    6     // энкодер фаза B
#define PIN_BTN  9     // кнопка энкодера, active-LOW

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ---------- Параметры Yeelight ----------
const uint16_t YEE_PORT = 55443;        // TCP-порт управления
String      lampIp = YEELIGHT_IP;       // адрес лампы (задаётся в secrets.h)
WiFiClient  lampClient;                  // постоянное соединение с лампой

// ======================================================================
//  ЭНКОДЕР — автомат Бена Бакстона (полушаг), устойчив к дребезгу.
//  1 отсчёт на щелчок (2 фронта). ISR в IRAM, таблица в DRAM.
// ======================================================================
#define R_START       0x0
#define H_CCW_BEGIN   0x1
#define H_CW_BEGIN    0x2
#define H_START_M     0x3
#define H_CW_BEGIN_M  0x4
#define H_CCW_BEGIN_M 0x5
#define DIR_CW        0x10
#define DIR_CCW       0x20

DRAM_ATTR static const uint8_t ttable[6][4] = {
  { H_START_M,           H_CW_BEGIN,    H_CCW_BEGIN,   R_START },
  { H_START_M | DIR_CCW, R_START,       H_CCW_BEGIN,   R_START },
  { H_START_M | DIR_CW,  H_CW_BEGIN,    R_START,       R_START },
  { H_START_M,           H_CCW_BEGIN_M, H_CW_BEGIN_M,  R_START },
  { H_START_M,           H_START_M,     H_CW_BEGIN_M,  R_START | DIR_CW },
  { H_START_M,           H_CCW_BEGIN_M, H_START_M,     R_START | DIR_CCW },
};
volatile uint8_t qstate = R_START;
volatile long    encDetents = 0;

void IRAM_ATTR onEncoder() {
  uint8_t pin = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  qstate = ttable[qstate & 0x07][pin];
  uint8_t dir = qstate & 0x30;
  if (dir == DIR_CW)       encDetents--;   // подобрано так, чтобы вправо = +
  else if (dir == DIR_CCW) encDetents++;
}

// ======================================================================
//  Модель: список управляемых параметров
// ======================================================================
enum ParamId { P_POWER, P_BRIGHT, P_CT, P_BGPOWER, P_BGBRIGHT, P_HUE, PARAM_COUNT };
const char *LABELS[PARAM_COUNT] = {
  "Свет", "Яркость", "Темп-ра", "Подсветка", "Зад. ярк.", "Цвет"
};

struct {
  bool power    = false;
  int  bright   = 50;
  int  ct       = 4000;
  bool bgPower  = false;
  int  bgBright = 50;
  int  hue      = 0;
} st;

int active = P_BRIGHT;        // активный параметр (с него начинаем)

// debounce-отправка значений
bool     pending     = false; // есть несохранённое изменение значения
int      pendingId   = -1;
uint32_t lastChangeMs = 0;
uint32_t lastSendMs   = 0;
const uint32_t COMMIT_IDLE = 200;   // мс тишины после последнего поворота -> отправляем
const uint32_t COMMIT_MAX  = 1000;  // при непрерывном кручении -> ~1 команда/сек (лимит 60/мин)
const uint32_t MIN_GAP     = 300;   // минимум между отправками (бережём лимит лампы)

uint16_t ACCENT, WHITE, GREY, BLACK, GREEN, REDC;

// ======================================================================
//  Утилиты
// ======================================================================
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// HSV -> RGB565 для образца цвета подсветки.
uint16_t hsv565(int h, int s, int v) {
  float S = s / 100.0f, V = v / 100.0f;
  float C = V * S;
  float X = C * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
  float m = V - C, r = 0, g = 0, b = 0;
  int seg = (h / 60) % 6;
  switch (seg) {
    case 0: r = C; g = X; break;
    case 1: r = X; g = C; break;
    case 2: g = C; b = X; break;
    case 3: g = X; b = C; break;
    case 4: r = X; b = C; break;
    default: r = C; b = X; break;
  }
  return tft.color565((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

// ======================================================================
//  Соединение и команды Yeelight
// ======================================================================
// Неблокирующий реконнект: при потере связи пробуем не чаще раза в RECONNECT_BACKOFF
// и с коротким таймаутом — иначе разрыв морозил бы UI на сотни мс на каждой команде.
uint32_t lastConnAttempt = 0;
const uint32_t RECONNECT_BACKOFF = 1200;

bool ensureConnected() {
  if (lampClient.connected()) return true;
  uint32_t now = millis();
  if (now - lastConnAttempt < RECONNECT_BACKOFF) return false;   // ждём — UI остаётся живым
  lastConnAttempt = now;
  lampClient.stop();
  bool ok = lampClient.connect(lampIp.c_str(), YEE_PORT, 400);
  if (!ok) Serial.println("[YEE] лампа недоступна, повтор позже");
  return ok;
}

// Вычитываем входящие данные построчно. ОШИБКИ лампы логируем (rate-limit и пр.).
void drainLamp() {
  while (lampClient.available()) {
    String l = lampClient.readStringUntil('\n');
    if (l.indexOf("error") >= 0) Serial.printf("[YEE] !! ответ-ошибка: %s\n", l.c_str());
  }
}

// Одна команда без разбора ответа. На статических буферах — без String в цикле.
bool sendRaw(const char *method, const char *params) {
  if (lampIp.isEmpty() || !ensureConnected()) return false;
  char cmd[160];
  snprintf(cmd, sizeof(cmd),
           "{\"id\":1,\"method\":\"%s\",\"params\":[%s]}\r\n", method, params);
  lampClient.print(cmd);
  Serial.printf("[YEE] -> %s", cmd);
  return true;
}

// Чтение состояния обеих зон одним get_prop (только при старте).
bool refreshState() {
  if (!ensureConnected()) return false;
  lampClient.print("{\"id\":1,\"method\":\"get_prop\",\"params\":"
                   "[\"main_power\",\"bright\",\"ct\",\"bg_power\",\"bg_bright\",\"bg_hue\"]}\r\n");

  String line;
  uint32_t t = millis();
  while (millis() - t < 1500) {
    if (lampClient.available()) {
      line = lampClient.readStringUntil('\n');
      if (line.indexOf("result") >= 0) break;   // пропускаем props-уведомления
      line = "";
    } else delay(5);
  }
  Serial.printf("[YEE] <- %s\n", line.c_str());

  int lb = line.indexOf('['), rb = line.indexOf(']', lb);
  if (lb < 0 || rb < 0) return false;

  char arr[160];
  int len = clampi(rb - lb - 1, 0, (int)sizeof(arr) - 1);
  memcpy(arr, line.c_str() + lb + 1, len);
  arr[len] = '\0';
  for (char *p = arr; *p; p++) if (*p == '"') *p = ' ';

  char *tok = strtok(arr, ",");
  int idx = 0;
  while (tok) {
    while (*tok == ' ') tok++;
    switch (idx) {
      case 0: st.power    = (strncmp(tok, "on", 2) == 0); break;
      case 1: st.bright   = atoi(tok); break;
      case 2: st.ct       = atoi(tok); break;
      case 3: st.bgPower  = (strncmp(tok, "on", 2) == 0); break;
      case 4: st.bgBright = atoi(tok); break;
      case 5: st.hue      = atoi(tok); break;
    }
    tok = strtok(NULL, ",");
    idx++;
  }
  return true;
}

// Надёжное включение подсветки: bg_set_scene атомарно «включает + задаёт режим».
// Простой bg_set_power "on" на этой прошивке не будит «спящую» зону.
void bgTurnOn() {
  char params[48];
  snprintf(params, sizeof(params), "\"hsv\",%d,100,%d", st.hue, st.bgBright);
  sendRaw("bg_set_scene", params);
}

// Отправка текущего значения конкретного параметра.
void sendValue(int p) {
  char params[48];
  switch (p) {
    case P_POWER:
      sendRaw("set_power", st.power ? "\"on\",\"smooth\",300" : "\"off\",\"smooth\",300");
      break;
    case P_BRIGHT:
      snprintf(params, sizeof(params), "%d,\"sudden\",30", st.bright);   // ручка -> мгновенно
      sendRaw("set_bright", params);
      break;
    case P_CT:
      snprintf(params, sizeof(params), "%d,\"sudden\",30", st.ct);
      sendRaw("set_ct_abx", params);
      break;
    case P_BGPOWER:
      if (st.bgPower) bgTurnOn();                                        // вкл — через сцену
      else            sendRaw("bg_set_power", "\"off\",\"smooth\",300"); // выкл — обычно
      break;
    case P_BGBRIGHT:
      if (st.bgPower) {
        snprintf(params, sizeof(params), "%d,\"sudden\",30", st.bgBright);
        sendRaw("bg_set_bright", params);
      } else {                          // подсветка была выкл -> включаем сценой с этой яркостью
        st.bgPower = true; bgTurnOn(); drawRow(P_BGPOWER);
      }
      break;
    case P_HUE:
      if (st.bgPower) {
        snprintf(params, sizeof(params), "%d,100,\"sudden\",30", st.hue);  // sat=100
        sendRaw("bg_set_hsv", params);
      } else {                          // подсветка была выкл -> включаем сценой с этим цветом
        st.bgPower = true; bgTurnOn(); drawRow(P_BGPOWER);
      }
      break;
  }
}

// ======================================================================
//  Экран: список параметров с курсором, перерисовка по одной строке
// ======================================================================
const int ROW_Y0 = 78, ROW_H = 24, VAL_X = 210;

void drawTitle() {
  u8g2.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2.setForegroundColor(st.power ? ACCENT : GREY);
  const char *t = "Yeelight";
  int w = u8g2.getUTF8Width(t);
  u8g2.setCursor((240 - w) / 2, 44);
  u8g2.print(t);
}

void drawRow(int i) {
  int y = ROW_Y0 + i * ROW_H;
  tft.fillRect(0, y - 15, 240, ROW_H, BLACK);   // чистим только эту полосу

  bool act = (i == active);
  uint16_t col = act ? ACCENT : WHITE;
  u8g2.setFont(u8g2_font_9x15_t_cyrillic);

  // курсор
  u8g2.setForegroundColor(act ? ACCENT : BLACK);
  u8g2.setCursor(20, y);
  u8g2.print(">");

  // подпись
  u8g2.setForegroundColor(col);
  u8g2.setCursor(38, y);
  u8g2.print(LABELS[i]);

  // значение
  char vbuf[16];
  switch (i) {
    case P_POWER:
      u8g2.setForegroundColor(st.power ? GREEN : GREY);
      strcpy(vbuf, st.power ? "вкл" : "выкл"); break;
    case P_BRIGHT:   snprintf(vbuf, sizeof(vbuf), "%d%%", st.bright); break;
    case P_CT:       snprintf(vbuf, sizeof(vbuf), "%dK", st.ct); break;
    case P_BGPOWER:
      u8g2.setForegroundColor(st.bgPower ? GREEN : GREY);
      strcpy(vbuf, st.bgPower ? "вкл" : "выкл"); break;
    case P_BGBRIGHT: snprintf(vbuf, sizeof(vbuf), "%d%%", st.bgBright); break;
    case P_HUE:      vbuf[0] = '\0'; break;   // рисуем образцом цвета
  }

  if (i == P_HUE) {
    // образец цвета подсветки + число hue
    tft.fillCircle(VAL_X - 6, y - 5, 7, hsv565(st.hue, 100, 100));
    tft.drawCircle(VAL_X - 6, y - 5, 7, WHITE);
    char hb[8]; snprintf(hb, sizeof(hb), "%d", st.hue);
    int w = u8g2.getUTF8Width(hb);
    u8g2.setForegroundColor(col);
    u8g2.setCursor(VAL_X - 18 - w, y);
    u8g2.print(hb);
  } else {
    int w = u8g2.getUTF8Width(vbuf);
    u8g2.setCursor(VAL_X - w, y);
    u8g2.print(vbuf);
  }
}

void drawAll() {
  tft.fillScreen(BLACK);
  drawTitle();
  for (int i = 0; i < PARAM_COUNT; i++) drawRow(i);
}

// ======================================================================
//  Логика ввода
// ======================================================================
void markPending(int p) {
  pending = true;
  pendingId = p;
  lastChangeMs = millis();
}

// Применяем поворот к активному параметру.
void applyDelta(long d) {
  switch (active) {
    case P_POWER:                                  // вправо = вкл, влево = выкл
      st.power = (d > 0);
      sendValue(P_POWER); lastSendMs = millis();
      drawTitle();
      break;
    case P_BRIGHT:
      st.bright = clampi(st.bright + 2 * d, 1, 100); markPending(P_BRIGHT); break;
    case P_CT:
      st.ct = clampi(st.ct + 100 * d, 2700, 6500);  markPending(P_CT); break;
    case P_BGPOWER:                                // вправо = вкл (сценой), влево = выкл
      st.bgPower = (d > 0);
      sendValue(P_BGPOWER); lastSendMs = millis();
      break;
    case P_BGBRIGHT:                               // подстройка сама включит подсветку (см. sendValue)
      st.bgBright = clampi(st.bgBright + 2 * d, 1, 100); markPending(P_BGBRIGHT); break;
    case P_HUE:
      st.hue = (st.hue + 6 * (int)d) % 360; if (st.hue < 0) st.hue += 360;
      markPending(P_HUE); break;
  }
  drawRow(active);
}

// debounce-отправка отложенного значения.
void commitPending() {
  if (!pending) return;
  uint32_t now = millis();
  bool idle   = now - lastChangeMs >= COMMIT_IDLE;
  bool forced = now - lastSendMs   >= COMMIT_MAX;
  if ((idle || forced) && now - lastSendMs >= MIN_GAP) {
    sendValue(pendingId);
    lastSendMs = now;
    pending = false;
  }
}

// Короткое нажатие кнопки -> следующий параметр (с антидребезгом).
void handleButton() {
  static bool stable = false, lastRead = false;
  static uint32_t lastChange = 0;
  bool reading = (digitalRead(PIN_BTN) == LOW);
  if (reading != lastRead) { lastChange = millis(); lastRead = reading; }
  if (millis() - lastChange > 35 && reading != stable) {
    stable = reading;
    if (stable) {                       // момент нажатия
      if (pending) { sendValue(pendingId); lastSendMs = millis(); pending = false; }
      int prev = active;
      active = (active + 1) % PARAM_COUNT;
      Serial.printf("[BTN] active -> %s\n", LABELS[active]);
      drawRow(prev);
      drawRow(active);
    }
  }
}

// ======================================================================
//  setup / loop
// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] lamp: setup start");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(PIN_BTN, INPUT_PULLUP);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);
  u8g2.begin(tft);
  u8g2.setFontMode(1);

  ACCENT = GC9A01A_YELLOW;
  WHITE  = GC9A01A_WHITE;
  GREY   = tft.color565(130, 130, 130);
  BLACK  = GC9A01A_BLACK;
  GREEN  = tft.color565(0, 230, 80);
  REDC   = tft.color565(255, 60, 60);

  attachInterrupt(digitalPinToInterrupt(ENC_A), onEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), onEncoder, CHANGE);

  // экран загрузки
  tft.fillScreen(BLACK);
  u8g2.setFont(u8g2_font_9x15_t_cyrillic);
  u8g2.setForegroundColor(GC9A01A_CYAN);
  const char *m = "Подключение...";
  u8g2.setCursor((240 - u8g2.getUTF8Width(m)) / 2, 120);
  u8g2.print(m);

  // Wi-Fi
  Serial.printf("[WiFi] connecting to \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    tft.fillScreen(BLACK);
    u8g2.setForegroundColor(REDC);
    u8g2.setCursor(40, 120); u8g2.print("Нет Wi-Fi");
    return;
  }
  Serial.print("[WiFi] IP="); Serial.println(WiFi.localIP());

  if (lampIp.isEmpty()) {
    Serial.println("[YEE] YEELIGHT_IP не задан в secrets.h");
  } else {
    Serial.printf("[YEE] IP лампы: %s\n", lampIp.c_str());
    // несколько попыток: лампа могла ещё держать старый TCP-слот после сброса
    for (int i = 0; i < 5 && !refreshState(); i++) delay(1300);
  }

  drawAll();
}

void loop() {
  // Wi-Fi-watchdog: если связь отвалилась — переподключаемся в фоне.
  static uint32_t lastWifi = 0;
  if (millis() - lastWifi > 3000) {
    lastWifi = millis();
    if (WiFi.status() != WL_CONNECTED) { Serial.println("[WiFi] reconnect"); WiFi.reconnect(); }
  }

  drainLamp();   // читаем входящие уведомления/ошибки лампы

  // поворот энкодера
  long enc; noInterrupts(); enc = encDetents; interrupts();
  static long lastEnc = 0;
  long d = enc - lastEnc;
  if (d != 0) { lastEnc = enc; applyDelta(d); }

  handleButton();
  commitPending();

  delay(2);
}
