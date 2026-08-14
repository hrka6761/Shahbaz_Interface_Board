# 00 — از اینجا شروع کنید

**فارسی** | [نسخه انگلیسی](../English/00_START_HERE.en.md)

این فایل نقشه راه پروژه **<code dir="ltr">Shahbaz Interface Board</code>** است. مسیر عملیاتی محصول، اتصال برد <code dir="ltr">ESP32-S3</code> از طریق <code dir="ltr">USB</code> داخلی به **گوشی <code dir="ltr">Android</code> دارای نرم‌افزار شهباز** است. ویندوز فقط برای توسعه و اعتبارسنجی <code dir="ltr">HIL</code> استفاده می‌شود و مقصد عملیاتی داده <code dir="ltr">Sensor</code>ها نیست.

## مسیر داده محصول

```text
SHT30 + MS5611
      |
      | I2C
      v
shahbaz_interface_board / ESP32-S3
      |
      | Native USB CDC — Shahbaz Protocol v2
      v
Android phone — USB Host
      |
      v
Shahbaz Android application
      |
      +-> live temperature / humidity / pressure
      +-> barometric altitude = pressure + app QNH
```

برد داده خام و اعتبارسنجی‌شده <code dir="ltr">Sensor</code>ها را از طریق <code dir="ltr">USB</code> داخلی ارسال می‌کند. نرم‌افزار شهباز در <code dir="ltr">Android</code>، <code dir="ltr">Client</code> عملیاتی است، مقدار <code dir="ltr">QNH</code> را تأمین می‌کند و ارتفاع بارومتریک را از فشار <code dir="ltr">MS5611</code> محاسبه می‌کند. ابزار <code dir="ltr">Windows HIL</code> همان <code dir="ltr">Protocol</code> را فقط برای جداکردن خطای برد و <code dir="ltr">Firmware</code> در زمان توسعه بررسی می‌کند.

## ترتیب مطالعه

| مرحله | سند | هدف |
|---|---|---|
| 1 | <code dir="ltr">01_PROJECT_OVERVIEW.fa.md</code> | نقش اجزای محصول و مرزهای سیستم را مشخص کنید. |
| 2 | <code dir="ltr">02_HARDWARE_CONNECTIONS.fa.md</code> | برد و <code dir="ltr">Sensor</code>ها را ایمن متصل کنید. |
| 3 | <code dir="ltr">03_DEVELOPMENT_TOOLS_SETUP.fa.md</code> | ابزارهای توسعه و <code dir="ltr">HIL</code> را آماده کنید. |
| 4 | <code dir="ltr">04_BUILD_FLASH_AND_USB.fa.md</code> | <code dir="ltr">Firmware</code> را بسازید و نقش دو مسیر <code dir="ltr">USB</code> را بشناسید. |
| 5 | <code dir="ltr">05_CODE_TESTING.fa.md</code> | تست‌ها و <code dir="ltr">Validator</code>های مستقل از سخت‌افزار را اجرا کنید. |
| 6 | <code dir="ltr">06_WINDOWS_BOARD_HIL_TEST.fa.md</code> | برد و <code dir="ltr">Firmware</code> را مستقل روی ویندوز بررسی کنید. |
| 7 | <code dir="ltr">07_ANDROID_SHAHBAZ_INTEGRATION_TEST.fa.md</code> | مسیر واقعی <code dir="ltr">Android + Shahbaz</code> را بررسی کنید. |
| 8 | <code dir="ltr">08_COMPLETE_SYSTEM_ACCEPTANCE.fa.md</code> | درباره پذیرش مرحله فعلی محصول تصمیم بگیرید. |
| 9 | <code dir="ltr">09_TROUBLESHOOTING.fa.md</code> | خطاهای <code dir="ltr">Android</code>، <code dir="ltr">USB</code>، <code dir="ltr">Protocol</code>، <code dir="ltr">Sensor</code> و برد را جدا کنید. |

سندهای 10 تا 14 درباره ایمنی، معماری، <code dir="ltr">Protocol</code>، توسعه و مدارک سخت‌افزار هستند.

## قانون اصلی معماری

**<code dir="ltr">Android + Shahbaz App</code> مسیر عملیاتی است و <code dir="ltr">Windows HIL</code> فقط مسیر تست و عیب‌یابی است.** موفقیت <code dir="ltr">Windows HIL</code> به‌تنهایی به معنی پذیرش کامل محصول نیست.

**مرحله بعد:** <code dir="ltr">01_PROJECT_OVERVIEW.fa.md</code>.
