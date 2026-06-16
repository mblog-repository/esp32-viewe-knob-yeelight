# esp32-viewe-knob-yeelight

Прошивки для платы **VIEWE UEDX24240013-MD50E** — круглый дисплей 1.28″ 240×240
(контроллер **GC9A01**) на **ESP32-C3** с энкодером и кнопкой. Конечная цель —
управлять монитор-лампой **Yeelight** напрямую по локальной сети (LAN-протокол,
TCP 55443).

Код к статье на [nikitakiselev.ru](https://nikitakiselev.ru). Разбираемся с платой
по шагам: экран → энкодер → Wi-Fi → управление лампой. Каждый шаг — в своей папке.

## Железо

Плата: **VIEWE UEDX24240013-MD50E** — ESP32-C3 + круглый IPS GC9A01 240×240, 4-wire SPI,
поворотный энкодер и кнопка.

| Функция | GPIO | Примечание |
|---|---|---|
| SPI SCLK | 1 | нестандартный пин: `SPI.begin(1, -1, 0, 10)` |
| SPI MOSI/SDA | 0 | |
| CS | 10 | |
| DC | 4 | |
| RST | — | не разведён, программный сброс (`-1`) |
| Подсветка (BL) | 8 | **active-LOW: `digitalWrite(8, LOW)` = ВКЛ.** HIGH = тёмный экран |
| Энкодер PHA | 7 | |
| Энкодер PHB | 6 | |
| Кнопка | 9 | active-LOW, `INPUT_PULLUP`; делит пин с boot-strapping |

> **Главная грабля:** инверсная подсветка. Код может работать идеально, но экран
> остаётся чёрным, если на GPIO8 подать HIGH. Всегда выставляйте LOW.

## Шаги (директории)

| Папка | Что делает |
|---|---|
| [`hello/`](hello/) | «Привет, читатель моего блога!» — первый вывод текста на экран |

(дальше будут добавляться: энкодер, Wi-Fi, управление лампой)

## Сборка и прошивка

Используется `arduino-cli` + `Makefile`.

```bash
# собрать + прошить + сбросить (по умолчанию SKETCH=hello)
make hello

# отдельные шаги
make build          # только компиляция
make upload         # только заливка
make reset          # аппаратный сброс
make monitor        # смотреть Serial
make ports          # список плат/портов
```

- **FQBN:** `esp32:esp32:esp32c3:CDCOnBoot=cdc`
  (в Arduino IDE: плата **ESP32C3 Dev Module**, **USB CDC On Boot: Enabled**).
- Порт определяется автоматически (`/dev/cu.usbmodem*`), переопределить:
  `make hello PORT=/dev/cu.usbmodemXXXX`.

### Зависимости

Ядро: `esp32:esp32`. Библиотеки:

```bash
arduino-cli lib install "Adafruit GFX Library" "Adafruit GC9A01A" "U8g2_for_Adafruit_GFX"
```

### Serial

На этой плате нативный USB Serial/JTAG (CDC), и `arduino-cli monitor` часто молчит.
Используйте `serial_tool.py` (нужен `pip3 install pyserial`):

```bash
python3 serial_tool.py monitor [секунды]   # читать Serial
python3 serial_tool.py reset               # аппаратный сброс
```

## Заметки по коду

- Исходники — строго **UTF-8**. Встроенный шрифт Adafruit GFX не рендерит кириллицу;
  используется `U8g2_for_Adafruit_GFX` со шрифтом `*_t_cyrillic`.
- В `loop()` избегаем создания `String` — фрагментация кучи на C3 приводит к
  перезагрузочному циклу. Статические буферы + `snprintf`/`strtok`.
