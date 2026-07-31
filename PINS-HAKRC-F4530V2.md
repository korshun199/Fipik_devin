# Пины HAKRC F405 V2 и Arduino UNO Q

## HAKRC F405 V2

Ориентация: **белая стрелка направлена вверх**, USB Type-C находится слева.

### Верхняя сторона

| Верхний ряд | CAM | 12V | GND | E1 | E3 | E5 | E7 | RX5 |
|-------------|-----|-----|-----|----|----|----|----|-----|
| Нижний ряд | 5V | GND | BAT | E2 | E4 | E6 | E8 | CURR |

### Правая сторона

| Верхний ряд | 5V | RX6 | SCL |
|-------------|----|-----|-----|
| Нижний ряд | GND | TX6 | SDA | 5V | GND | RX1 | TX1 |

### Нижняя сторона

| Верхний ряд | 4V5 | TX2 | VCC | VTX | TX4 | 5V | BB- |
|-------------|-----|-----|-----|-----|-----|----|-----|
| Нижний ряд | GND | RX2 | 5V | GND | RX4 | GND | LED |

### Левая сторона

```text
12V, SW-G, USB Type-C: GND, D+, D-, +5V
```

## Arduino UNO Q

Ориентация: кнопка POWER и USB Type-C находятся слева.

### Верхняя сторона слева направо

```text
SCL, SDA, AREF, GND,
D13, D12, D11, D10, D9, D8,
D7, D6, D5, D4, D3, D2, D1/TX, D0/RX
```

### Нижняя сторона слева направо

```text
A5, A4, A3, A2, A1, A0,
VIN, GND, GND, 5V, 3.3V, RESET, IOREF, NC
```

### Правая сторона сверху вниз

```text
JSPI 2x3
QWIIC
монтажное отверстие
```

### Левая сторона

```text
кнопка POWER
USB Type-C
```

## Соединение UNO Q с HAKRC F405 V2

Используется свободный **UART6** полётного контроллера и аппаратный **USART1** микроконтроллера UNO Q.

| HAKRC F405 V2 / физический пин | Соединение | Arduino UNO Q / физический пин |
|--------------------------------|------------|--------------------------------|
| RX6, правая сторона | ← | D1/TX, верхняя сторона |
| TX6, правая сторона | → | D0/RX, верхняя сторона |
| GND, правая сторона | ↔ | GND, верхняя или нижняя сторона |

В виде списка:

```text
HAKRC / RX6  <->  UNO Q / D1-TX
HAKRC / TX6  <->  UNO Q / D0-RX
HAKRC / GND  <->  UNO Q / GND
```

### Что не соединять

```text
HAKRC / 5V   X   UNO Q / 5V
HAKRC / VCC  X   UNO Q / VIN
```

Платы питаются отдельно. Между ними соединяется только общая земля и две сигнальные линии UART.

## Компьютерное зрение: камера → HAKRC → EasyCAP → UNO Q

### Камера → HAKRC

| Аналоговая камера / провод | Соединение | HAKRC F405 V2 / физический пин |
|----------------------------|------------|--------------------------------|
| VIDEO | → | CAM, верхняя сторона |
| +5V | ← | 5V под пином CAM |
| GND | ↔ | GND, верхняя сторона |

### HAKRC → EasyCAP

| HAKRC F405 V2 / физический пин | Соединение | EasyCAP MS2106 |
|--------------------------------|------------|----------------|
| VTX, нижняя сторона | → | жёлтый RCA VIDEO IN, центральный контакт |
| GND рядом с VTX | ↔ | жёлтый RCA, внешний контакт |

HAKRC принимает аналоговый сигнал на `CAM`, накладывает OSD и выдаёт его через `VTX`. Название `VTX` здесь означает аналоговый видеовыход, а не питание видеопередатчика.

### EasyCAP → UNO Q

| EasyCAP | Соединение | UNO Q |
|---------|------------|-------|
| USB | → | USB-порт питаемого USB-хаба |
| USB-хаб, upstream | → | USB Type-C UNO Q |

EasyCAP получает питание от USB-хаба и появляется в Linux как видеоустройство `/dev/videoN`. OpenCV выбирает его автоматически.

### Питание UNO Q

| Источник | Соединение | UNO Q |
|----------|------------|-------|
| DC-DC OUT +5V | → | 5V, нижняя сторона |
| DC-DC OUT GND | → | GND, нижняя сторона |

DC-DC должен выдавать стабилизированные 5 В и не менее 3 А, лучше 5 А. Земли DC-DC, UNO Q, HAKRC, камеры и EasyCAP должны быть общими.

## Единая схема

```text
УПРАВЛЕНИЕ

ИИ Linux на UNO Q
        ↓ Arduino Bridge
UNO Q D1/TX  →  HAKRC RX6
UNO Q D0/RX  ←  HAKRC TX6
UNO Q GND    ↔  HAKRC GND
        ↓ MSP
Betaflight → ESC → моторы

КОМПЬЮТЕРНОЕ ЗРЕНИЕ

Камера +5V   ← HAKRC 5V
Камера GND   ↔ HAKRC GND
Камера VIDEO → HAKRC CAM
                    ↓ OSD
               HAKRC VTX
                    ↓ аналоговое видео
           EasyCAP VIDEO IN
                    ↓ USB
              питаемый USB-хаб
                    ↓ USB Type-C
                 UNO Q Linux
                    ↓ OpenCV
 объект / влево / вправо / приближается / удаляется

ПИТАНИЕ UNO Q

Аккумулятор → DC-DC 5V/3-5A → UNO Q 5V и GND
```

## Программный путь команды

```text
ИИ в Linux на QRB2210
        ↓ Arduino Bridge
STM32U585 на UNO Q
        ↓ D1/TX и D0/RX
UART6 HAKRC F405 V2
        ↓ MSP
Betaflight
```

Прошивка Betaflight не заменяется. Для связи потребуется только включить MSP на UART6 в настройках Betaflight. Логические уровни UART обеих плат — 3.3 В.

Управление


text
UNO Q D1/TX → HAKRC RX6
UNO Q D0/RX ← HAKRC TX6
UNO Q GND   ↔ HAKRC GND
Компьютерное зрение


text
Камера VIDEO → HAKRC CAM
Камера +5V   ← HAKRC 5V
Камера GND   ↔ HAKRC GND
 
HAKRC VTX → EasyCAP жёлтый RCA VIDEO IN
HAKRC GND → EasyCAP жёлтый RCA GND
 
EasyCAP USB → питаемый USB-хаб
USB-хаб     → USB Type-C UNO Q
UNO Q       → OpenCV
Питание UNO Q


text
Аккумулятор → DC-DC 5V/3–5A
DC-DC +5V  → UNO Q 5V
DC-DC GND  → UNO Q GND
HAKRC накладывает OSD на видео, EasyCAP оцифровывает его, а UNO Q определяет объект и направление движения. Изменения пока не закоммичены.