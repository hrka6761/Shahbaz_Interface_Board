# 02 — اتصال برد و دو <code dir="ltr">I2C Module</code>

**فارسی** | [نسخه انگلیسی](../English/02_HARDWARE_CONNECTIONS.en.md)

> در این مرحله موتور، <code dir="ltr">ESC</code>، <code dir="ltr">Servo</code> و پروانه را متصل نکنید.

در این پروژه از سخت‌افزارهای زیر استفاده می‌شود:

- مرجع برد توسعه سازگار با <code dir="ltr">VCC-GND Studio YD-ESP32-S3 N16R8</code>.
- <code dir="ltr">SHT30 I2C breakout</code> با برچسب <code dir="ltr">SHT3X-DIS</code> و چهار <code dir="ltr">Pin</code>.
- <code dir="ltr">GY-63 breakout</code> که انتظار می‌رود <code dir="ltr">MS5611-01BA03</code> روی آن قرار داشته باشد و در حالت <code dir="ltr">I2C</code> استفاده می‌شود.

سازنده دو <code dir="ltr">breakout</code> از روی عکس‌های پروژه قابل شناسایی نیست. به همین دلیل اتصال‌ها بر اساس برچسب‌های قابل مشاهده روی <code dir="ltr">Module</code> واقعی پروژه و <code dir="ltr">Protocol</code> رسمی <code dir="ltr">Sensirion</code>/<code dir="ltr">TE Connectivity</code> تعریف شده‌اند، نه بر اساس ادعاهای صفحات فروشگاهی.

## 1. اتصال مشترک <code dir="ltr">I2C</code>

| کاربرد | برد <code dir="ltr">ESP32-S3</code> | <code dir="ltr">SHT3X-DIS Module</code> | <code dir="ltr">GY-63 / MS5611 Module</code> |
|---|---|---|---|
| تغذیه 3.3<code dir="ltr">V</code> | <code dir="ltr">3V3</code> | <code dir="ltr">VCC</code> | <code dir="ltr">VCC</code> |
| زمین | <code dir="ltr">GND</code> | <code dir="ltr">GND</code> | <code dir="ltr">GND</code> |
| داده <code dir="ltr">I2C</code> | <code dir="ltr">GPIO8</code> | <code dir="ltr">SDA</code> | <code dir="ltr">SDA</code> |
| کلاک <code dir="ltr">I2C</code> | <code dir="ltr">GPIO9</code> | <code dir="ltr">SCL</code> | <code dir="ltr">SCL</code> |
| انتخاب <code dir="ltr">Interface</code> | — | — | <code dir="ltr">PS -> 3.3 V / HIGH</code> |
| انتخاب آدرس | — | — | <code dir="ltr">CSB -> GND / LOW</code> |
| خروجی داده <code dir="ltr">SPI</code> | — | — | <code dir="ltr">SDO -> NC</code> |

تنظیم فعلی <code dir="ltr">Firmware</code>:

```text
SDA = GPIO8
SCL = GPIO9
SHT30 address = 0x44
MS5611 address = 0x77
I2C target frequency = 400 kHz
```

### علت اهمیت سه <code dir="ltr">Pin</code> اضافه روی <code dir="ltr">GY-63</code>

طبق <code dir="ltr">Datasheet</code> رسمی <code dir="ltr">MS5611</code>، حالت <code dir="ltr">PS=HIGH</code> رابط <code dir="ltr">I2C</code> را انتخاب می‌کند. در حالت <code dir="ltr">I2C</code>، <code dir="ltr">CSB</code> باید حتماً به سطح بالا یا پایین متصل باشد و نباید شناور بماند. شهباز از <code dir="ltr">CSB=LOW</code> استفاده می‌کند که با آدرس <code dir="ltr">0x77</code> هماهنگ است. <code dir="ltr">SDO</code> خروجی <code dir="ltr">SPI</code> است و در تنظیم <code dir="ltr">I2C</code> پروژه استفاده نمی‌شود.

## 2. بررسی‌های مهم قبل از روشن‌کردن

1. <code dir="ltr">VCC</code> و <code dir="ltr">GND</code> هر دو <code dir="ltr">Module</code> واقعی را دوباره کنترل کنید.
2. <code dir="ltr">SDA</code> و <code dir="ltr">SCL</code> جابه‌جا نباشند.
3. در تنظیم این پروژه، هر دو <code dir="ltr">Module</code> را از 3.3<code dir="ltr">V</code> تغذیه کنید.
4. سطح منطقی 5<code dir="ltr">V</code> را مستقیماً به <code dir="ltr">ESP32-S3 GPIO</code> وارد نکنید.
5. زمین برد <code dir="ltr">ESP32</code> و هر دو <code dir="ltr">Module</code> مشترک باشد.
6. درباره محدوده <code dir="ltr">VCC</code>، <code dir="ltr">regulator</code>، <code dir="ltr">level shifter</code> یا مقاومت‌های <code dir="ltr">pull-up</code> خود <code dir="ltr">breakout</code> بر اساس صفحات فروشگاهی فرض نکنید. این موارد تا زمان اندازه‌گیری یا شناسایی روی همان <code dir="ltr">Module</code> واقعی، تأییدنشده هستند.

## 3. انتخاب <code dir="ltr">USB Type-C Connector</code> درست

طراحی رسمی <code dir="ltr">YD-ESP32-S3</code> **دو <code dir="ltr">Type-C Connector</code> متفاوت** دارد:

- **<code dir="ltr">Native ESP32-S3 USB</code>** — مستقیم به <code dir="ltr">USB</code> خود <code dir="ltr">ESP32-S3</code> متصل است و شهباز از همین <code dir="ltr">Connector</code> برای <code dir="ltr">Telemetry</code> استفاده می‌کند. <code dir="ltr">GPIO19 = D-</code> و <code dir="ltr">GPIO20 = D+</code>.
- **<code dir="ltr">USB-to-UART</code>** — از <code dir="ltr">WCH CH343P bridge</code> عبور می‌کند و در صورت نیاز برای <code dir="ltr">Programming</code> یا عیب‌یابی <code dir="ltr">Serial</code> استفاده می‌شود؛ این مسیر، لینک <code dir="ltr">native USB</code> شهباز نیست.

قبل از اتکا به محل فیزیکی <code dir="ltr">Connector</code>ها، برد واقعی خود را با مرجع رسمی <code dir="ltr">YD-ESP32-S3</code> در <code dir="ltr">hardware_reference/01_esp32_s3_n16r8_development_board/</code> تطبیق دهید.

## 4. چک سریع قبل از اتصال <code dir="ltr">USB</code>

- [ ] <code dir="ltr">SHT3X-DIS</code>: <code dir="ltr">VCC -> 3.3 V</code>، <code dir="ltr">GND -> GND</code>، <code dir="ltr">SDA -> GPIO8</code>، <code dir="ltr">SCL -> GPIO9</code>.
- [ ] <code dir="ltr">GY-63</code>: <code dir="ltr">VCC -> 3.3 V</code>، <code dir="ltr">GND -> GND</code>، <code dir="ltr">SDA -> GPIO8</code>، <code dir="ltr">SCL -> GPIO9</code>.
- [ ] <code dir="ltr">GY-63</code>: <code dir="ltr">PS -> 3.3 V</code>، <code dir="ltr">CSB -> GND</code>، <code dir="ltr">SDO -> NC</code>.
- [ ] اتصال کوتاه بین 3.3<code dir="ltr">V</code> و <code dir="ltr">GND</code> وجود ندارد.
- [ ] <code dir="ltr">Actuator</code>ها غیرفعال یا از نظر فیزیکی جدا هستند.
- [ ] کابل <code dir="ltr">USB</code> قابلیت انتقال داده دارد.
- [ ] کابل به <code dir="ltr">Native ESP32-S3 USB Type-C</code> وصل شده است، نه <code dir="ltr">CH343P USB-to-UART Connector</code>.

برای منابع، عکس <code dir="ltr">Module</code>ها، محدودیت‌های <code dir="ltr">Pin</code> و وضعیت راستی‌آزمایی، فایل <code dir="ltr">14_HARDWARE_REFERENCE_AND_DATASHEETS.fa.md</code> و <code dir="ltr">../../hardware_reference/README.fa.md</code> را ببینید.

**مرحله بعد:** <code dir="ltr">03_DEVELOPMENT_TOOLS_SETUP.fa.md</code>.
