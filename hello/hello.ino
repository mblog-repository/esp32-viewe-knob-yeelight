/*
 * Шаг 1 — «Привет, читатель моего блога».
 *
 * Плата: VIEWE UEDX24240013-MD50E — круглый дисплей 1.28" 240x240 на ESP32-C3 (GC9A01).
 * Среда: Arduino (ESP32C3 Dev Module, USB CDC On Boot: Enabled).
 * ВАЖНО: файл сохранён в UTF-8 — иначе кириллица превратится в «кракозябры».
 *
 * Распиновка платы (из CLAUDE.md, по коду не определяется):
 *   SCLK=1, MOSI=0, CS=10, DC=4, RST=программный(-1), BL=8 (подсветка инверсная!).
 */

#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>

#define TFT_SCLK 1
#define TFT_MOSI 0
#define TFT_CS   10
#define TFT_DC   4
#define TFT_RST  -1   // не разведён — программный сброс
#define TFT_BL   8    // active-LOW: LOW = подсветка ВКЛ, HIGH = экран тёмный

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

const int SCREEN_W  = 240;
const int SCREEN_H  = 240;
const int MAX_WIDTH = 200;   // круглый экран — оставляем поля по краям
const int LINE_H    = 26;
const int ASCENT    = 16;

// Перенос по словам + центрирование блока по вертикали.
// Без String — на статических буферах, чтобы не фрагментировать кучу.
void drawWrapped(const char *text, uint16_t color) {
  tft.fillScreen(GC9A01A_BLACK);
  u8g2.setForegroundColor(color);

  char buf[160];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char lines[10][80];
  int  lineCount = 0;
  char cur[80];
  cur[0] = '\0';

  char *word = strtok(buf, " ");
  while (word != NULL) {
    char trial[80];
    if (cur[0] == '\0') {
      strncpy(trial, word, sizeof(trial) - 1);
      trial[sizeof(trial) - 1] = '\0';
    } else {
      snprintf(trial, sizeof(trial), "%s %s", cur, word);
    }

    if (u8g2.getUTF8Width(trial) <= MAX_WIDTH) {
      strncpy(cur, trial, sizeof(cur) - 1);
      cur[sizeof(cur) - 1] = '\0';
    } else {
      if (cur[0] != '\0' && lineCount < 10) {
        strncpy(lines[lineCount], cur, 79);
        lines[lineCount][79] = '\0';
        lineCount++;
      }
      strncpy(cur, word, sizeof(cur) - 1);
      cur[sizeof(cur) - 1] = '\0';
    }
    word = strtok(NULL, " ");
  }
  if (cur[0] != '\0' && lineCount < 10) {
    strncpy(lines[lineCount], cur, 79);
    lines[lineCount][79] = '\0';
    lineCount++;
  }

  int totalH = lineCount * LINE_H;
  int y = (SCREEN_H - totalH) / 2 + ASCENT;
  for (int i = 0; i < lineCount; i++) {
    int w = u8g2.getUTF8Width(lines[i]);
    u8g2.setCursor((SCREEN_W - w) / 2, y);
    u8g2.print(lines[i]);
    y += LINE_H;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] hello: setup start");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);   // подсветка инверсная: LOW = включена
  Serial.println("[BOOT] backlight ON (LOW)");

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);

  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_10x20_t_cyrillic);

  // Основное сообщение по центру.
  drawWrapped("Привет, читатель моего блога!", GC9A01A_WHITE);

  // Тонкое цветное кольцо по краю круглого экрана — для фото.
  int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
  tft.drawCircle(cx, cy, 118, GC9A01A_CYAN);
  tft.drawCircle(cx, cy, 117, GC9A01A_CYAN);

  // Маленький подзаголовок снизу.
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  u8g2.setForegroundColor(GC9A01A_CYAN);
  const char *sub = "ESP32-C3 + GC9A01";
  int sw = u8g2.getUTF8Width(sub);
  u8g2.setCursor((SCREEN_W - sw) / 2, 200);
  u8g2.print(sub);

  Serial.println("[BOOT] text drawn");
}

void loop() {
  // Статичный экран — перерисовывать ничего не нужно.
  delay(1000);
}
