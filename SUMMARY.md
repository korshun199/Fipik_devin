# Fipik Project Summary

## Project Overview
Fipik - это проект полетного контроллера на базе ESP32-WROOM-32 для квадрокоптера Quad-X, включающий прошивку для микроконтроллера, Android-приложение для конфигурации и веб-интерфейс.

## 1. Flight Controller Firmware (ESP32)

### Структура проекта
- **Основной файл**: `src/main.cpp` (510 строк)
- **Конфигурация сборки**: `platformio.ini`
- **Данные конфигурации**: `data/flight-controller.json`
- **Платформа**: PlatformIO (Espressif32)
- **Фреймворк**: Arduino

### Основные модули прошивки
1. **Конфигурация**: Загрузка настроек из JSON через LittleFS
2. **Обработка RC сигнала**: SBUS протокол через UART2 (RX900 приемник)
3. **Управление моторами**: PWM для 4 ESC (Quad-X микшер)
4. **Дисплей**: OLED SSD1306 через I2C
5. **Сенсоры**: Магнитометр (HMC5883L/QMC5883L) через I2C
6. **Сеть**: WiFi AP + WebServer для удаленной конфигурации
7. **Безопасность**: ARM переключатель, таймауты, безопасные пределы

### Файлы конфигурации
- `platformio.ini` - настройки PlatformIO, зависимости библиотек
- `data/flight-controller.json` - конфигурация прошивки (пины, параметры ESC, радио и т.д.)

### Статус компиляции
✅ **УСПЕШНО** - прошивка скомпилирована без ошибок
- RAM: 14.1% (46152 bytes from 327680 bytes)
- Flash: 66.9% (877281 bytes from 1310720 bytes)
- Время сборки: 8.21 секунды

## 2. Android Application

### Структура проекта
- **Основной файл**: `app/src/main/java/ru/korshun199/fipikconfig/MainActivity.kt` (258 строк)
- **Конфигурация сборки**: 
  - `build.gradle.kts` (root)
  - `app/build.gradle.kts` (app module)
  - `settings.gradle.kts`
- **Gradle Wrapper**: `gradle/wrapper/gradle-wrapper.properties` (Gradle 8.9)

### Основные модули приложения
1. **UI**: Программная генерация интерфейса (без XML layouts)
2. **Сеть**: HTTP запросы к ESP32 Web API
3. **Конфигурация**: 
   - Подключение к ESP32 WiFi AP
   - Настройка каналов RC приемника
   - Настройка пределов PWM для моторов
4. **Дизайн**: Темная тема, кастомные компоненты

### Файлы конфигурации
- `build.gradle.kts` - корневая конфигурация проекта
- `app/build.gradle.kts` - конфигурация приложения (Android SDK 34, Kotlin 1.9.20)
- `settings.gradle.kts` - настройки репозиториев и модулей
- `app/src/main/AndroidManifest.xml` - манифест приложения

### Статус компиляции
✅ **УСПЕШНО** - APK собран без ошибок
- **Вывод**: `app/build/outputs/apk/debug/app-debug.apk`
- **Время сборки**: 59 секунд
- **Исправленные проблемы**: Обновлен Gradle с 8.5 до 8.9 для совместимости с Android Gradle Plugin 8.7.0

## 3. Web Server (Python Flask)

### Структура проекта
- **Основной файл**: `server/app.py`
- **Файл зависимостей**: `requirements.txt`
- **Веб-интерфейс**: `web/index.html`

### Функциональность
- REST API для конфигурации прошивки
- Веб-интерфейс для настройки параметров
- Локальный доступ только (127.0.0.1:5000)

## Технологический стек

### Firmware
- ESP32-WROOM-32
- Arduino Framework
- PlatformIO
- Библиотеки: ArduinoJson, Adafruit GFX, Adafruit SSD1306

### Android
- Kotlin
- Android SDK 34 (minSdk 26)
- Gradle 8.9
- Android Gradle Plugin 8.7.0

### Web Server
- Python Flask
- HTML/CSS/JS

## Текущий статус

Все основные компоненты проекта успешно компилируются:

1. ✅ **Прошивка ESP32** - собрана, готова к загрузке
2. ✅ **Android APK** - собран, готов к установке
3. ✅ **Gradle Wrapper** - настроен и работает
4. ✅ **Конфигурация** - файлы настроек присутствуют

## Следующие шаги

Проект готов к разработке и тестированию. Возможные направления:
- Добавление IMU для стабилизации
- Реализация полного управления моторами
- Расширение функционала Android-приложения
- Добавление telemetry и логирования
