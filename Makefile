# Makefile для платы VIEWE UEDX24240013-MD50E (ESP32-C3, дисплей GC9A01)
#
# Использование:
#   make hello      — собрать и прошить скетч «Привет, читатель» (hello)
#   make flash      — собрать+прошить+сбросить текущий SKETCH (по умолч. hello)
#   make build      — только компиляция
#   make upload     — только заливка
#   make reset      — аппаратный сброс платы
#   make monitor    — смотреть Serial (Ctrl+C для выхода)
#   make ports      — список плат/портов
#   make clean      — удалить каталоги сборки
#
# Переопределить порт/скетч:  make flash SKETCH=hello PORT=/dev/cu.usbmodemXXXX

FQBN   ?= esp32:esp32:esp32c3:CDCOnBoot=cdc
PORT   ?= $(shell ls /dev/cu.usbmodem* 2>/dev/null | head -1)
SKETCH ?= hello

.PHONY: hello flash build upload reset monitor ports clean help

# --- быстрые цели для конкретных скетчей ---
hello:
	$(MAKE) flash SKETCH=hello

# --- основной конвейер ---
flash: build upload reset
	@echo "Готово: $(SKETCH) залит на $(PORT)"

build:
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

reset:
	@python3 serial_tool.py reset

monitor:
	python3 serial_tool.py monitor

ports:
	arduino-cli board list

clean:
	rm -rf */build

help:
	@grep -E '^#( |  )' Makefile | sed 's/^# //'
