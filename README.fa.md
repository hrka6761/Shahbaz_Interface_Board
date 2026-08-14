# <code dir="ltr">Shahbaz Interface Board — ESP32-S3 N16R8</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

> **مسیر مستندات:** برای شروع از [<code dir="ltr">doc/فارسی/00_START_HERE.fa.md</code>](doc/فارسی/00_START_HERE.fa.md) استفاده کنید.

این پروژه <code dir="ltr">Firmware</code> و کد مرجع یکپارچه‌سازی <code dir="ltr">Android</code> برای **<code dir="ltr">shahbaz_interface_board</code>** است. این برد رابط میان <code dir="ltr">Sensor</code>ها و خروجی‌های اختیاری سمت پهپاد با نرم‌افزار شهباز در گوشی <code dir="ltr">Android</code> است. برد نقش <code dir="ltr">USB Device</code> دارد و گوشی <code dir="ltr">Android</code> دارای نرم‌افزار شهباز، <code dir="ltr">USB Host</code> عملیاتی است. ویندوز فقط برای توسعه، بارگذاری، عیب‌یابی و <code dir="ltr">HIL</code> مستقل استفاده می‌شود.

## مسیر عملیاتی

```text
SHT30 + MS5611
      |
ESP-IDF I2C master (GPIO8 SDA, GPIO9 SCL, 400 kHz)
      |
SharedSensorScheduler -> SensorTelemetryPublisher
      |
COBS + CRC32C Shahbaz Protocol v2
      |
TinyUSB CDC-ACM / Native ESP32-S3 USB (GPIO19 D-, GPIO20 D+)
      |
Android phone — operational USB Host
      |
Shahbaz Android application
      +-> live sensor data
      +-> barometric altitude from pressure + app QNH

Development-only alternate path:
ESP32-S3 native USB -> Windows PC -> Windows HIL
```

مسیر <code dir="ltr">Sensor</code> و <code dir="ltr">USB</code> به‌صورت پیش‌فرض فعال است. خروجی‌های فیزیکی <code dir="ltr">Actuator</code> پیاده‌سازی شده‌اند اما با <code dir="ltr">CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n</code> غیرفعال هستند.

## تنظیمات سخت‌افزاری پیش‌فرض

| مورد | مقدار |
|---|---|
| هدف | <code dir="ltr">ESP32-S3, 16 MiB Flash, 8 MiB Octal PSRAM</code> |
| <code dir="ltr">SHT30</code> | <code dir="ltr">0x44</code> |
| <code dir="ltr">GY-63 / MS5611</code> | <code dir="ltr">0x77</code> |
| <code dir="ltr">I2C</code> | <code dir="ltr">GPIO8 SDA, GPIO9 SCL, 400 kHz</code> |
| <code dir="ltr">Native USB</code> | <code dir="ltr">GPIO19 D-, GPIO20 D+</code> |

## توسعه و ساخت روی ویندوز

از <code dir="ltr">ESP-IDF 5.4+</code> استفاده کنید:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM_FLASH flash monitor
```

## تست‌های مستقل از سخت‌افزار

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_host_tests.ps1
py -3 tools\windows_hil_test.py --self-test
powershell -ExecutionPolicy Bypass -File tools\run_kotlin_protocol_tests.ps1
py -3 tools\validate_firmware_contract.py
py -3 tools\check_firmware_safety.py
py -3 tools\check_bilingual_docs.py
```

## <code dir="ltr">Windows HIL</code> فقط برای اعتبارسنجی برد

پس از بارگذاری <code dir="ltr">Firmware</code> می‌توان مسیر مستقل برد را بررسی کرد:

```powershell
py -3 tools\windows_hil_test.py --port COM_USB --qnh-hpa 1013.25
```

موفقیت این تست فقط نشان می‌دهد برد، <code dir="ltr">Sensor</code>ها، <code dir="ltr">Firmware</code>، <code dir="ltr">Native USB</code> و <code dir="ltr">Protocol</code> مستقل از نرم‌افزار <code dir="ltr">Android</code> سالم هستند. این تست پذیرش نهایی محصول نیست.

## یکپارچه‌سازی عملیاتی <code dir="ltr">Android</code>

کدهای زیر برای اتصال نرم‌افزار شهباز به برد فراهم شده‌اند:

| مسیر | مسئولیت |
|---|---|
| <code dir="ltr">android_reference/src/main/.../ProvisionalProtocol.kt</code> | <code dir="ltr">Protocol v2 codec</code> |
| <code dir="ltr">android_reference/src/main/.../ShahbazLinkSession.kt</code> | مدیریت <code dir="ltr">Session</code>، <code dir="ltr">TimeSync</code>، <code dir="ltr">Token</code> و <code dir="ltr">QNH</code> |
| <code dir="ltr">android_reference/src/android/.../ShahbazUsbCdcTransport.kt</code> | مسیر واقعی <code dir="ltr">UsbManager / CDC bulk IN/OUT</code> |
| <code dir="ltr">android_reference/src/android/.../ShahbazInterfaceBoardClient.kt</code> | رابط سطح نرم‌افزار شهباز برای داده زنده و نگهداری لینک |

تست کامل مسیر واقعی در <code dir="ltr">doc/فارسی/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.fa.md</code> تعریف شده است.

## <code dir="ltr">Protocol v2</code>

هر اتصال فیزیکی <code dir="ltr">USB</code> یک <code dir="ltr">Session Token</code> تازه دارد. قطع اتصال، وضعیت <code dir="ltr">RX/TX/Parser/Sequence/TimeSync</code> را پاک می‌کند. فرمان‌های وابسته به <code dir="ltr">Session</code> با <code dir="ltr">Token</code> قدیمی رد می‌شوند و تازگی زمان فرستنده نیز بررسی می‌شود. هسته <code dir="ltr">Android</code> هر 30 ثانیه <code dir="ltr">TimeSync</code> را تازه می‌کند.

ارتفاع عملیاتی در **نرم‌افزار شهباز** از فشار <code dir="ltr">MS5611</code> و <code dir="ltr">QNH</code> نرم‌افزار محاسبه می‌شود. <code dir="ltr">Windows HIL</code> فقط برای تست مستقل می‌تواند همین محاسبه را تکرار کند.

## مرز پذیرش

پذیرش نهایی مرحله فعلی فقط وقتی معتبر است که برد واقعی به گوشی واقعی <code dir="ltr">Android</code> متصل شود، نرم‌افزار شهباز مجوز <code dir="ltr">USB</code> بگیرد، <code dir="ltr">Protocol v2</code> را ایجاد کند، داده زنده هر دو <code dir="ltr">Sensor</code> را دریافت کند، ارتفاع را از فشار و <code dir="ltr">QNH</code> محاسبه کند و پس از قطع و اتصال مجدد یک <code dir="ltr">Session</code> تازه بسازد. موفقیت <code dir="ltr">Windows HIL</code> جای این تست را نمی‌گیرد.
