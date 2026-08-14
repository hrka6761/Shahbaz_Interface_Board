# 14 — مرجع سخت‌افزار، <code dir="ltr">Module</code>ها، <code dir="ltr">Pin</code>ها و منابع شرکت سازنده

**فارسی** | [نسخه انگلیسی](../English/14_HARDWARE_REFERENCE_AND_DATASHEETS.en.md)

این سند آخرین فایل مسیر مطالعه است. منابع مهندسی خام در <code dir="ltr">hardware_reference/</code> نگه‌داری می‌شوند و ساختار آن ابتدا بر اساس **برد/<code dir="ltr">Module</code> واقعی پروژه** و سپس بر اساس اعتبار منبع طراحی شده است.

## 1. سخت‌افزار مورد استفاده پروژه

| سخت‌افزار پروژه | هویت فعلی | سیاست انتخاب منبع |
|---|---|---|
| برد توسعه <code dir="ltr">ESP32-S3</code> | برد سازگار با <code dir="ltr">YD-ESP32-S3</code> با هدف <code dir="ltr">ESP32-S3-WROOM-1-N16R8</code> | مستندات برد از <code dir="ltr">VCC-GND Studio</code> + مستندات <code dir="ltr">Module/SoC</code> از <code dir="ltr">Espressif</code> + مستندات <code dir="ltr">CH343</code> از <code dir="ltr">WCH</code> |
| <code dir="ltr">Module</code> دما/رطوبت | <code dir="ltr">breakout</code> <code dir="ltr">Generic</code> با برچسب <code dir="ltr">SHT3X-DIS</code> و انتظار <code dir="ltr">SHT30</code> | عکس واقعی پروژه + مستندات رسمی <code dir="ltr">Sensirion SHT3x/SHT30</code> |
| <code dir="ltr">Module</code> فشار | <code dir="ltr">GY-63 breakout</code> <code dir="ltr">Generic</code> با انتظار <code dir="ltr">MS5611-01BA03</code> | عکس واقعی پروژه + مستندات رسمی <code dir="ltr">TE Connectivity MS5611</code> |

سازنده خود دو برد <code dir="ltr">breakout</code> از روی عکس‌های ارائه‌شده قابل شناسایی نیست. بنابراین پروژه صفحات فروشگاهی را مرجع قطعی ویژگی‌های الکتریکی خود <code dir="ltr">breakout</code> در نظر نمی‌گیرد.

## 2. پروفایل سخت‌افزاری پروژه

```text
Board reference: VCC-GND Studio YD-ESP32-S3
ESP32 module: ESP32-S3-WROOM-1-N16R8
Flash: 16 MB Quad SPI
PSRAM: 8 MB Octal SPI
I2C: SDA GPIO8, SCL GPIO9, target 400 kHz
Native USB: GPIO19 D-, GPIO20 D+
SHT30 configured address: 0x44
MS5611 configured address: 0x77
GY-63 straps: PS HIGH, CSB LOW, SDO NC
```

<code dir="ltr">Espressif</code> گونه <code dir="ltr">N16R8</code> را با <code dir="ltr">16 MB flash</code> و <code dir="ltr">8 MB Octal SPI PSRAM</code> تعریف می‌کند. مرجع رسمی <code dir="ltr">VCC-GND YD-ESP32-S3</code> نیز یک <code dir="ltr">native USB Type-C</code> مستقیم روی <code dir="ltr">GPIO19/20</code>، یک <code dir="ltr">CH343P USB-to-UART Type-C</code> جدا، <code dir="ltr">WS2812</code> روی <code dir="ltr">GPIO48</code> و <code dir="ltr">UART0</code> روی <code dir="ltr">GPIO43/GPIO44</code> را مستند می‌کند.

## 3. سیاست مهم <code dir="ltr">GPIO</code>

| <code dir="ltr">GPIO</code> | وضعیت پروژه |
|---:|---|
| 0, 3, 45, 46 | حساس به <code dir="ltr">strapping</code>؛ قبل از استفاده بازبینی شوند |
| 4, 5, 6, 7 | <code dir="ltr">Actuator PWM</code> فقط در صورت فعال‌سازی آگاهانه و بازبینی |
| 8 | <code dir="ltr">I2C SDA</code> پروژه |
| 9 | <code dir="ltr">I2C SCL</code> پروژه |
| 10, 11 | <code dir="ltr">Servo PWM</code> فقط در صورت فعال‌سازی آگاهانه و بازبینی |
| 19 | <code dir="ltr">Native USB D-</code>؛ رزرو |
| 20 | <code dir="ltr">Native USB D+</code>؛ رزرو |
| 22..25 | شماره‌های <code dir="ltr">GPIO</code> غیرموجود در <code dir="ltr">ESP32-S3</code> |
| 26..34 | مسیر داخلی <code dir="ltr">flash/PSRAM</code>؛ تخصیص داده نشوند |
| 35..37 | در تنظیم <code dir="ltr">N16R8 8-line PSRAM</code> غیرقابل‌استفاده |
| 38 | بعد از بازبینی عادی پروژه قابل استفاده؛ در <code dir="ltr">Header</code> رسمی <code dir="ltr">YD</code> خارج شده است |
| 43, 44 | مسیر <code dir="ltr">UART0</code>/عیب‌یابی؛ رزرو |
| 48 | <code dir="ltr">WS2812 RGB LED</code> روی برد رسمی <code dir="ltr">YD</code>؛ رزرو |

## 4. ساختار پوشه و محل هر نوع فایل

```text
hardware_reference/
  01_esp32_s3_n16r8_development_board/
    01_official_vcc_gnd_board_docs/
    02_official_espressif_esp32_s3_docs/
  02_sht30_i2c_module/
    01_official_sensirion_chip_docs/
    02_actual_project_module_photos/
  03_gy63_ms5611_i2c_module/
    01_official_te_connectivity_chip_docs/
    02_actual_project_module_photos/
  04_project_hardware_configuration/
  05_project_hardware_diagrams/
  99_source_integrity/
```

هر پوشه اصلی یک <code dir="ltr">README</code> فارسی/انگلیسی دارد که توضیح می‌دهد کدام فایل مرجع قطعی است و کدام مورد هنوز تأیید فیزیکی نیاز دارد.

## 5. منابع رسمی ذخیره‌شده یا لینک‌شده

### برد توسعه و <code dir="ltr">ESP32</code>

- مخزن رسمی <code dir="ltr">VCC-GND Studio YD-ESP32-S3</code> و لینک شماتیک <code dir="ltr">V1.4</code>.
- تصاویر رسمی نمای برد و نمای کلی سخت‌افزار از <code dir="ltr">VCC-GND</code>.
- <code dir="ltr">Espressif ESP32-S3-WROOM-1/WROOM-1U Datasheet</code>.
- <code dir="ltr">Espressif ESP32-S3 SoC Datasheet</code>.
- مستندات رسمی پایدار <code dir="ltr">Espressif native USB/USB Device</code>.
- صفحه رسمی <code dir="ltr">WCH CH343 Datasheet</code> و <code dir="ltr">Windows Driver</code> برای <code dir="ltr">USB-to-UART bridge</code> جداگانه برد.

### <code dir="ltr">SHT30 Module</code>

- <code dir="ltr">Sensirion SHT3x-DIS Datasheet</code>.
- صفحه رسمی <code dir="ltr">Sensirion SHT30-DIS-B</code>.
- راهنمای رسمی نگهداری <code dir="ltr">SHT</code> از <code dir="ltr">Sensirion</code>.
- راهنمای رسمی تست در شرایط محیطی از <code dir="ltr">Sensirion</code>.
- عکس واقعی <code dir="ltr">SHT3X-DIS breakout</code> پروژه.

### <code dir="ltr">GY-63 / MS5611 Module</code>

- <code dir="ltr">TE Connectivity MS5611-01BA03 Datasheet</code>.
- صفحه رسمی محصول <code dir="ltr">TE Connectivity MS5611</code>.
- عکس واقعی <code dir="ltr">GY-63 breakout</code> پروژه.

## 6. چرا از <code dir="ltr">Datasheet</code> فروشگاهی <code dir="ltr">Generic</code> برای <code dir="ltr">GY-63/SHT30</code> استفاده نمی‌شود؟

نام <code dir="ltr">GY-63</code> و <code dir="ltr">SHT3X-DIS breakout</code> چهار <code dir="ltr">Pin</code> در این پروژه، به‌تنهایی سازنده دقیق برد را مشخص نمی‌کند. بدون شناسایی سازنده <code dir="ltr">breakout</code>، یک صفحه فروشگاهی نمی‌تواند وجود دقیق <code dir="ltr">regulator</code>، <code dir="ltr">level shifter</code>، شبکه <code dir="ltr">pull-up</code>، محدوده <code dir="ltr">VCC</code> یا نسخه <code dir="ltr">PCB</code> روی قطعه واقعی شما را اثبات کند.

به همین دلیل پروژه این موارد را جدا می‌کند:

- **اطلاعات سطح <code dir="ltr">chip</code>** — از <code dir="ltr">Sensirion</code>، <code dir="ltr">TE Connectivity</code>، <code dir="ltr">Espressif</code> یا <code dir="ltr">WCH</code>؛
- **اطلاعات سطح برد <code dir="ltr">YD</code>** — از <code dir="ltr">VCC-GND Studio</code>؛
- **اطلاعات قابل مشاهده روی <code dir="ltr">breakout</code>** — از عکس واقعی پروژه؛
- **اطلاعات اندازه‌گیری‌شده** — فقط بعد از اندازه‌گیری سخت‌افزار؛
- **تنظیم پروژه** — چیزی که <code dir="ltr">Firmware</code> و سیم‌بندی عمداً انتخاب کرده‌اند.

## 7. مواردی که هنوز باید روی سخت‌افزار واقعی تأیید شوند

- عکس واضح جلو و پشت برد <code dir="ltr">ESP32</code> و نوشته خوانای <code dir="ltr">Module</code>.
- تطبیق برد فیزیکی با مرجع/نسخه <code dir="ltr">VCC-GND YD-ESP32-S3</code>.
- <code dir="ltr">I2C scan</code> با مشاهده <code dir="ltr">SHT30=0x44</code> و <code dir="ltr">MS5611=0x77</code>.
- تأیید اتصال <code dir="ltr">PS/CSB</code> روی <code dir="ltr">GY-63 Module</code> مونتاژشده.
- ولتاژ حالت بیکار <code dir="ltr">I2C</code> و مقاومت مؤثر <code dir="ltr">pull-up</code> در صورت مشاهده مشکل سیگنال.
- <code dir="ltr">USB enumeration</code> روی <code dir="ltr">Native USB Type-C</code>.
- اندازه‌گیری‌های تغذیه/<code dir="ltr">backfeed</code> موردنیاز برای آرایش نهایی تغذیه پهپاد.

## 8. کنترل اصالت و <code dir="ltr">Validation</code>

فایل <code dir="ltr">hardware_reference/99_source_integrity/source_catalog.csv</code> مرجع صادرکننده و دلیل استفاده از هر فایل را ثبت می‌کند. فایل <code dir="ltr">official_files_sha256.txt</code> نیز برای <code dir="ltr">PDF</code>ها و تصاویر رسمی ذخیره‌شده به‌صورت محلی <code dir="ltr">SHA-256 hash</code> نگه می‌دارد.

اجرای <code dir="ltr">validator</code>:

```powershell
py -3 tools\validate_graphics_inputs.py
```

وضعیت <code dir="ltr">UNVERIFIED</code> خطای مستندسازی نیست؛ یعنی پروژه عمداً یک واقعیت فیزیکی اندازه‌گیری‌نشده را به ادعای قطعی تبدیل نکرده است.

این آخرین سند مسیر مطالعه است. برای شروع دوباره از ابتدا به <code dir="ltr">00_START_HERE.fa.md</code> برگردید.
