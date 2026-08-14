# 01 — نمای کلی پروژه

**فارسی** | [نسخه انگلیسی](../English/01_PROJECT_OVERVIEW.en.md)

## هدف

نام رسمی این بخش از سیستم **<code dir="ltr">shahbaz_interface_board</code>** است. این برد مبتنی بر <code dir="ltr">ESP32-S3 N16R8</code> رابط میان <code dir="ltr">Sensor</code>ها و خروجی‌های اختیاری سمت پهپاد با نرم‌افزار شهباز در <code dir="ltr">Android</code> است.

مسیر عملیاتی فعلی:

```text
SHT30 --------\
               -> I2C -> shahbaz_interface_board / ESP32-S3
MS5611 -------/                         |
                                        | Native USB CDC
                                        | Shahbaz Protocol v2
                                        v
                               Android phone / USB Host
                                        |
                                        v
                               Shahbaz Android application
```

مسیر توسعه و اعتبارسنجی مستقل:

```text
shahbaz_interface_board -> Native USB -> Windows PC -> Windows HIL
                                                   [development/diagnostics only]
```

## مسئولیت‌های برد

<code dir="ltr">shahbaz_interface_board</code> باید داده <code dir="ltr">SHT30</code> و <code dir="ltr">MS5611</code> را دریافت و اعتبارسنجی کند، <code dir="ltr">Shahbaz Protocol v2</code> را روی <code dir="ltr">USB CDC-ACM</code> داخلی ارائه دهد، قواعد <code dir="ltr">Session</code>، تازگی زمان، <code dir="ltr">Heartbeat</code> و ایمنی را اعمال کند و به‌عنوان <code dir="ltr">USB Device</code> پایدار در برابر گوشی <code dir="ltr">Android</code> عمل کند.

## مسئولیت‌های نرم‌افزار شهباز

نرم‌افزار شهباز در <code dir="ltr">Android</code> مسئول دریافت مجوز <code dir="ltr">USB</code>، مدیریت اتصال و قطع اتصال، بازکردن مسیر <code dir="ltr">CDC</code>، ایجاد <code dir="ltr">TimeSync</code> و <code dir="ltr">Session Token</code>، نگهداری <code dir="ltr">Heartbeat</code>، نمایش داده زنده و محاسبه ارتفاع بارومتریک بر پایه فشار و <code dir="ltr">QNH</code> است.

<code dir="ltr">Windows HIL</code> فقط برای اعتبارسنجی مستقل برد و <code dir="ltr">Firmware</code> استفاده می‌شود و بخشی از مسیر عملیاتی محصول نیست.

## وضعیت <code dir="ltr">Actuator</code>

خروجی‌های فیزیکی به‌صورت پیش‌فرض غیرفعال هستند:

```text
CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n
```

برای پذیرش مسیر <code dir="ltr">Sensor/USB/Android</code> نیازی به خروجی فیزیکی نیست.

## مواردی که بدون سخت‌افزار قابل تست هستند

ریاضیات <code dir="ltr">Sensor</code>، <code dir="ltr">CRC/COBS</code>، ساختار <code dir="ltr">Protocol</code>، منطق <code dir="ltr">Session</code>، ایمنی، سریال‌سازی، <code dir="ltr">Python HIL codec</code> و هسته <code dir="ltr">Kotlin</code> سمت <code dir="ltr">Android</code> را می‌توان مستقل از برد بررسی کرد.

## مواردی که به سخت‌افزار واقعی نیاز دارند

- ساخت و اجرای واقعی <code dir="ltr">ESP-IDF</code> روی برد.
- عملکرد <code dir="ltr">USB</code> داخلی روی برد فیزیکی.
- داده واقعی <code dir="ltr">SHT30/MS5611</code> و کیفیت الکتریکی <code dir="ltr">I2C</code>.
- <code dir="ltr">Windows HIL</code> برای اعتبارسنجی مستقل برد.
- **تست کامل گوشی واقعی <code dir="ltr">Android</code> با نرم‌افزار شهباز.**

موفقیت <code dir="ltr">Windows HIL</code> لازم و مفید است، اما برای پذیرش نهایی محصول کافی نیست.

**مرحله بعد:** <code dir="ltr">02_HARDWARE_CONNECTIONS.fa.md</code>.
