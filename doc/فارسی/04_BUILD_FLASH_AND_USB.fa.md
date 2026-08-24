# 04 — ساخت، بارگذاری و نقش‌های <code dir="ltr">USB</code>

**فارسی** | [نسخه انگلیسی](../English/04_BUILD_FLASH_AND_USB.en.md)

## 1. تنظیم ایمن پیش‌فرض

برای تست <code dir="ltr">Sensor/USB</code> خروجی‌های فیزیکی غیرفعال بمانند:

```text
CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n
```

برای <code dir="ltr">Session</code> تعاملی، فایل <code dir="ltr">sdkconfig.defaults</code> مقدار بازبینی‌شده <code dir="ltr">1000 ms</code> را برای <code dir="ltr">Heartbeat Timeout</code> و دورهٔ <code dir="ltr">350 ms</code> سمت <code dir="ltr">Android/Windows HIL</code> انتخاب می‌کند. اگر این تنظیم پروژه وجود نداشته باشد، مقدار جایگزین <code dir="ltr">Kconfig</code> در جزء قابل‌استفادهٔ مجدد همچنان عمداً <code dir="ltr">0 ms</code> و بسته و ایمن می‌ماند.

مسیر اصلی خروجی عیب‌یابی باید روی <code dir="ltr">UART0</code> بماند و <code dir="ltr">CONFIG_ESP_CONSOLE_SECONDARY_NONE=y</code> تنظیم شود. خروجی ثانویه <code dir="ltr">USB Serial/JTAG</code> باید غیرفعال بماند تا با مسیر داده <code dir="ltr">native USB-OTG TinyUSB CDC</code> رقابت نکند.

## 2. ساخت و بارگذاری

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM_FLASH flash monitor
```

فرآیند ساخت، <code dir="ltr">Firmware/Hardware Contract Validator</code> را نیز اجرا می‌کند.

پس از تغییر کد <code dir="ltr">Firmware</code>، هیچ فایل تولیدشدهٔ قبلی از پوشه <code dir="ltr">build/</code> را روی برد بارگذاری نکنید. تصویر فعلی موجود در مخزن مربوط به <code dir="ltr">2026-08-15</code> است و از پیاده‌سازی <code dir="ltr">Android/CDC logical session</code> در <code dir="ltr">2026-08-23</code> قدیمی‌تر است؛ بنابراین با یکپارچه‌سازی فعلی سازگار نیست و باید دوباره ساخته شود. اسکریپت <code dir="ltr">tools/build_esp32.ps1</code> ساخت پاک انجام می‌دهد و اعتبارسنج ساخت تولیدی اکنون هر فایل <code dir="ltr">ELF</code> یا تصویر برنامه را که از ورودی‌های مرتبط <code dir="ltr">Firmware</code> قدیمی‌تر باشد رد می‌کند.

## 3. دو مسیر متفاوت <code dir="ltr">USB</code>

- <code dir="ltr">Programming/diagnostic USB-UART</code>: برای ساخت، بارگذاری و مشاهده خروجی توسعه.
- <code dir="ltr">Native ESP32-S3 USB</code>: روی <code dir="ltr">GPIO19 D- / GPIO20 D+</code> و مسیر عملیاتی <code dir="ltr">Shahbaz Protocol v2</code>.

مسیر عملیاتی:

```text
Native ESP32-S3 USB <-> Android phone USB Host <-> Shahbaz application
```

ویندوز فقط برای <code dir="ltr">HIL</code> توسعه می‌تواند به همین <code dir="ltr">Native USB</code> متصل شود.

## 4. بررسی توسعه روی ویندوز

برای تست مستقل برد، ویندوز باید رابط <code dir="ltr">Native USB CDC</code> را به‌صورت یک <code dir="ltr">COM</code> نشان دهد که در مستندات <code dir="ltr">COM_USB</code> نامیده می‌شود. این نام و این الزام بخشی از محصول <code dir="ltr">Android</code> نیست.

## 5. بررسی عملیاتی <code dir="ltr">Android</code>

در مسیر واقعی، گوشی <code dir="ltr">Android</code> برد را به‌عنوان <code dir="ltr">USB Device</code> شناسایی می‌کند. نرم‌افزار شهباز باید مجوز <code dir="ltr">USB</code> را دریافت و <code dir="ltr">CDC bulk endpoint</code>ها را باز کند. نام <code dir="ltr">Windows COM</code> در <code dir="ltr">Android</code> هیچ کاربردی ندارد.

کد <code dir="ltr">Android</code> موجود در <code dir="ltr">android_reference/src/android/</code> رابط <code dir="ltr">CDC</code> را بر اساس کلاس و <code dir="ltr">endpoint</code>های <code dir="ltr">USB</code> پیدا می‌کند.

## 6. هشدار تغذیه

گوشی <code dir="ltr">Android</code> در مسیر عملیاتی <code dir="ltr">USB Host</code> است، اما <code dir="ltr">VBUS</code> نباید به مسیر ناخواسته تغذیه برد، <code dir="ltr">Sensor</code>ها یا باتری تبدیل شود. تا قبل از تأیید مسیر واقعی <code dir="ltr">VBUS/backfeed</code>، تغذیه خارجی هم‌زمان با <code dir="ltr">USB</code> گوشی استفاده نشود.

**مرحله بعد:** <code dir="ltr">05_CODE_TESTING.fa.md</code>.
