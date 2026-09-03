# 02 — اتصال برد و شش <code dir="ltr">I2C Sensor Module</code>

**فارسی** | [نسخه انگلیسی](../English/02_HARDWARE_CONNECTIONS.en.md)

> در این مرحله موتور، <code dir="ltr">ESC</code>، <code dir="ltr">Servo</code> و پروانه را متصل نکنید.

در این پروژه از سخت‌افزارهای زیر استفاده می‌شود:

- مرجع برد توسعه سازگار با <code dir="ltr">VCC-GND Studio YD-ESP32-S3 N16R8</code>.
- <code dir="ltr">SHT30 I2C breakout</code> با برچسب <code dir="ltr">SHT3X-DIS</code> و چهار <code dir="ltr">Pin</code>.
- <code dir="ltr">GY-63 breakout</code> که انتظار می‌رود <code dir="ltr">MS5611-01BA03</code> روی آن قرار داشته باشد و در حالت <code dir="ltr">I2C</code> استفاده می‌شود.
- چهار <code dir="ltr">GY-530 breakout</code> که مالک پروژه آن‌ها را دارای <code dir="ltr">VL53L0X</code> معرفی کرده است: پایین، بالا، جلوی چپ و جلوی راست.

سازنده <code dir="ltr">breakout</code>ها مشخص نیست. سیم‌بندی <code dir="ltr">SHT30/MS5611</code> بر اساس برچسب‌های عکس‌های موجود و <code dir="ltr">Protocol</code> رسمی <code dir="ltr">Sensirion/TE Connectivity</code> است. هنوز عکس یا شماتیک همان <code dir="ltr">GY-530</code>های واقعی در مخزن وجود ندارد؛ بنابراین باید برچسب و طراحی الکتریکی هر قطعه با مستندات <code dir="ltr">VL53L0X</code> شرکت <code dir="ltr">ST</code> بررسی شود و به صفحه فروشگاهی اتکا نشود.

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
VL53L0X power-up address = 0x29 (all four devices)
VL53L0X runtime addresses = 0x30, 0x31, 0x32, 0x33
VL53L0X feature default = disabled
```

### علت اهمیت سه <code dir="ltr">Pin</code> اضافه روی <code dir="ltr">GY-63</code>

طبق <code dir="ltr">Datasheet</code> رسمی <code dir="ltr">MS5611</code>، حالت <code dir="ltr">PS=HIGH</code> رابط <code dir="ltr">I2C</code> را انتخاب می‌کند. در حالت <code dir="ltr">I2C</code>، <code dir="ltr">CSB</code> باید حتماً به سطح بالا یا پایین متصل باشد و نباید شناور بماند. شهباز از <code dir="ltr">CSB=LOW</code> استفاده می‌کند که با آدرس <code dir="ltr">0x77</code> هماهنگ است. <code dir="ltr">SDO</code> خروجی <code dir="ltr">SPI</code> است و در تنظیم <code dir="ltr">I2C</code> پروژه استفاده نمی‌شود.

## 2. اتصال چهار <code dir="ltr">VL53L0X</code>

هر چهار <code dir="ltr">Module</code> از <code dir="ltr">SDA=GPIO8</code>، <code dir="ltr">SCL=GPIO9</code> و زمین مشترک استفاده می‌کنند. پیش از اعمال تغذیه، ورودی <code dir="ltr">VCC</code> و سطح منطق همان <code dir="ltr">GY-530 breakout</code> واقعی بررسی شود؛ فقط منطق سازگار با 3.3 ولت می‌تواند به <code dir="ltr">ESP32-S3</code> برسد.

اتصال <code dir="ltr">SDA/SCL</code> به‌تنهایی کافی نیست. همه <code dir="ltr">VL53L0X</code>ها با آدرس هفت‌بیتی <code dir="ltr">0x29</code> روشن می‌شوند؛ بنابراین طراحی پیاده‌سازی‌شده برای هر <code dir="ltr">Module</code> یک سیگنال مستقل و فعال‌پایین <code dir="ltr">XSHUT</code> لازم دارد:

| <code dir="ltr">instance</code> ثابت | نقش فیزیکی | خط مشترک | <code dir="ltr">XSHUT</code> پیشنهادی | آدرس زمان اجرا |
|---:|---|---|---:|---:|
| <code dir="ltr">0</code> | پایین/زمین | <code dir="ltr">SDA GPIO8 / SCL GPIO9</code> | <code dir="ltr">GPIO12</code> | <code dir="ltr">0x30</code> |
| <code dir="ltr">1</code> | بالا | <code dir="ltr">SDA GPIO8 / SCL GPIO9</code> | <code dir="ltr">GPIO13</code> | <code dir="ltr">0x31</code> |
| <code dir="ltr">2</code> | جلوی چپ | <code dir="ltr">SDA GPIO8 / SCL GPIO9</code> | <code dir="ltr">GPIO14</code> | <code dir="ltr">0x32</code> |
| <code dir="ltr">3</code> | جلوی راست | <code dir="ltr">SDA GPIO8 / SCL GPIO9</code> | <code dir="ltr">GPIO15</code> | <code dir="ltr">0x33</code> |

مقدارهای <code dir="ltr">GPIO12..15</code> فقط پیش‌فرض پیشنهادی هستند و سیم‌بندی فیزیکی را اثبات نمی‌کنند. پیش از فعال‌سازی باید ترتیب <code dir="ltr">Pin</code> همان <code dir="ltr">Module</code>، پیوستگی هر <code dir="ltr">XSHUT</code> تا <code dir="ltr">GPIO</code>، نقش/جهت حسگر، نبود اتصال کوتاه و نبود تداخل با <code dir="ltr">Actuator</code> یا منابع برد ثبت شود. کد فعلی پایان اندازه‌گیری را با خواندن دوره‌ای بررسی می‌کند و خروجی وقفه یا <code dir="ltr">GPIO1</code> را به کار نمی‌برد.

<code dir="ltr">Firmware</code> ابتدا همه قطعات را با <code dir="ltr">XSHUT</code> غیرفعال می‌کند و سپس هر قطعه را جداگانه آزاد، شناسایی، آدرس‌دهی، بررسی و پیکربندی می‌کند. آدرس‌های جدید موقت‌اند و پس از هر بازنشانی دوباره ساخته می‌شوند. مسیر <code dir="ltr">I2C multiplexer</code> در این مخزن پیاده‌سازی نشده است.

دروازه فاصله‌سنج عمداً به‌صورت پیش‌فرض بسته است. تا زمانی که شواهد واقعی و ثبت‌شده سخت‌افزار عمومی حسگر و چهار مسیر اختصاصی <code dir="ltr">XSHUT</code> در <code dir="ltr">Kconfig</code> انتخاب نشده‌اند، <code dir="ltr">CONFIG_SHAHBAZ_VL53L0X_ENABLE=y</code> را تنظیم نکنید. تخصیص نامعتبر، گم‌شده یا هم‌پوشان <code dir="ltr">XSHUT</code> هر چهار قطعه را مسدود می‌کند.

## 3. بررسی‌های مهم قبل از روشن‌کردن

1. <code dir="ltr">VCC</code> و <code dir="ltr">GND</code> همه <code dir="ltr">Module</code>های واقعی را دوباره کنترل کنید.
2. <code dir="ltr">SDA</code> و <code dir="ltr">SCL</code> جابه‌جا نباشند.
3. در تنظیم این پروژه <code dir="ltr">SHT30</code> و <code dir="ltr">GY-63</code> از 3.3 ولت تغذیه می‌شوند؛ ورودی <code dir="ltr">VCC</code> همان <code dir="ltr">GY-530</code> واقعی پیش از اتصال بررسی شود.
4. سطح منطقی 5<code dir="ltr">V</code> را مستقیماً به <code dir="ltr">ESP32-S3 GPIO</code> وارد نکنید.
5. زمین برد <code dir="ltr">ESP32</code> و همه <code dir="ltr">Module</code>ها مشترک باشد.
6. درباره محدوده <code dir="ltr">VCC</code>، <code dir="ltr">regulator</code>، <code dir="ltr">level shifter</code> یا مقاومت‌های <code dir="ltr">pull-up</code> خود <code dir="ltr">breakout</code> بر اساس صفحات فروشگاهی فرض نکنید. این موارد تا زمان اندازه‌گیری یا شناسایی روی همان <code dir="ltr">Module</code> واقعی، تأییدنشده هستند.
7. مقاومت مؤثر <code dir="ltr">pull-up</code> و زمان خیز <code dir="ltr">SDA/SCL</code> با هر شش <code dir="ltr">Module</code> متصل اندازه‌گیری شود؛ مقاومت‌های موازی روی چند <code dir="ltr">breakout</code> ممکن است بار خط را بیش از حد زیاد کنند.

## 4. انتخاب <code dir="ltr">USB Type-C Connector</code> درست

طراحی رسمی <code dir="ltr">YD-ESP32-S3</code> **دو <code dir="ltr">Type-C Connector</code> متفاوت** دارد:

- **<code dir="ltr">Native ESP32-S3 USB</code>** — مستقیم به <code dir="ltr">USB</code> خود <code dir="ltr">ESP32-S3</code> متصل است و شهباز از همین <code dir="ltr">Connector</code> برای <code dir="ltr">Telemetry</code> استفاده می‌کند. <code dir="ltr">GPIO19 = D-</code> و <code dir="ltr">GPIO20 = D+</code>.
- **<code dir="ltr">USB-to-UART</code>** — از <code dir="ltr">WCH CH343P bridge</code> عبور می‌کند و در صورت نیاز برای <code dir="ltr">Programming</code> یا عیب‌یابی <code dir="ltr">Serial</code> استفاده می‌شود؛ این مسیر، لینک <code dir="ltr">native USB</code> شهباز نیست.

قبل از اتکا به محل فیزیکی <code dir="ltr">Connector</code>ها، برد واقعی خود را با مرجع رسمی <code dir="ltr">YD-ESP32-S3</code> در <code dir="ltr">hardware_reference/01_esp32_s3_n16r8_development_board/</code> تطبیق دهید.

## 5. چک سریع قبل از اتصال <code dir="ltr">USB</code>

- [ ] <code dir="ltr">SHT3X-DIS</code>: <code dir="ltr">VCC -> 3.3 V</code>، <code dir="ltr">GND -> GND</code>، <code dir="ltr">SDA -> GPIO8</code>، <code dir="ltr">SCL -> GPIO9</code>.
- [ ] <code dir="ltr">GY-63</code>: <code dir="ltr">VCC -> 3.3 V</code>، <code dir="ltr">GND -> GND</code>، <code dir="ltr">SDA -> GPIO8</code>، <code dir="ltr">SCL -> GPIO9</code>.
- [ ] <code dir="ltr">GY-63</code>: <code dir="ltr">PS -> 3.3 V</code>، <code dir="ltr">CSB -> GND</code>، <code dir="ltr">SDO -> NC</code>.
- [ ] هر <code dir="ltr">VL53L0X Module</code>: <code dir="ltr">VCC</code> بررسی‌شده، <code dir="ltr">GND</code> مشترک، <code dir="ltr">SDA -> GPIO8</code> و <code dir="ltr">SCL -> GPIO9</code>.
- [ ] پیوستگی <code dir="ltr">XSHUT</code> پایین/بالا/جلوی چپ/جلوی راست به‌ترتیب برای <code dir="ltr">GPIO12/13/14/15</code> ثبت شده است.
- [ ] هر چهار نقش فیزیکی و جهت محور نوری برچسب دارند و با <code dir="ltr">instance=0/1/2/3</code> یکسان‌اند.
- [ ] شناسه مدرک عمومی سخت‌افزار حسگر و شناسه اختصاصی مدرک <code dir="ltr">XSHUT</code> به رکورد واقعی اشاره می‌کنند؛ در غیر این صورت فاصله‌سنج‌ها غیرفعال می‌مانند.
- [ ] اتصال کوتاه بین 3.3<code dir="ltr">V</code> و <code dir="ltr">GND</code> وجود ندارد.
- [ ] <code dir="ltr">Actuator</code>ها غیرفعال یا از نظر فیزیکی جدا هستند.
- [ ] کابل <code dir="ltr">USB</code> قابلیت انتقال داده دارد.
- [ ] کابل به <code dir="ltr">Native ESP32-S3 USB Type-C</code> وصل شده است، نه <code dir="ltr">CH343P USB-to-UART Connector</code>.

برای منابع، عکس <code dir="ltr">Module</code>ها، محدودیت‌های <code dir="ltr">Pin</code> و وضعیت راستی‌آزمایی، فایل‌های <code dir="ltr">14_HARDWARE_REFERENCE_AND_DATASHEETS.fa.md</code>، <code dir="ltr">../../hardware_reference/README.fa.md</code> و <code dir="ltr">../../hardware_reference/06_vl53l0x_gy530_modules/README.fa.md</code> را ببینید.

**مرحله بعد:** <code dir="ltr">03_DEVELOPMENT_TOOLS_SETUP.fa.md</code>.
