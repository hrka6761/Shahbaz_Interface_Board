# گزارش اعتبارسنجی شهباز

**فارسی** | [نسخه انگلیسی](TEST_RESULTS.en.md)

تاریخ: <code dir="ltr">2026-08-13</code>  
هدف: <code dir="ltr">shahbaz_interface_board / ESP32-S3 N16R8</code>  
<code dir="ltr">Host</code> عملیاتی: گوشی <code dir="ltr">Android</code> دارای نرم‌افزار شهباز  
<code dir="ltr">Host</code> عیب‌یابی توسعه: <code dir="ltr">Windows HIL</code>  
<code dir="ltr">Protocol</code>: <code dir="ltr">Shahbaz wire protocol v2</code> روی <code dir="ltr">Native USB CDC-ACM</code>

## موارد اجراشده در این محیط

- <code dir="ltr">CMake Configure/Build</code> برای <code dir="ltr">test/host</code>: **<code dir="ltr">PASS</code>** با تبدیل هشدار به خطا.
- <code dir="ltr">CTest</code>: **13/13 <code dir="ltr">PASS</code>** و بدون شکست.
- <code dir="ltr">tools/windows_hil_test.py --self-test</code>: **<code dir="ltr">PASS</code>** برای <code dir="ltr">Python diagnostic codec</code>، <code dir="ltr">CRC32C/COBS</code>، داده <code dir="ltr">SHT30/MS5611</code>، ساختار <code dir="ltr">Session</code> و ریاضیات ارتفاع/<code dir="ltr">QNH</code>.
- تست <code dir="ltr">Kotlin Protocol v2 + operational session</code>: **<code dir="ltr">PASS</code>**؛ شامل پاک‌سازی مرز اتصال فیزیکی، ایجاد <code dir="ltr">TimeSync/Session Token</code>، مسدودشدن فرمان وابسته به <code dir="ltr">Session</code> پیش از <code dir="ltr">TimeSync</code>، رمزگشایی <code dir="ltr">Telemetry</code> و ارتفاع با <code dir="ltr">QNH</code>.
- کد وابسته به چارچوب <code dir="ltr">Android</code> با <code dir="ltr">API stub</code> حداقلی از نظر ساختار و نحو بررسی شد: **<code dir="ltr">PASS</code>**. ساخت واقعی با <code dir="ltr">Android SDK</code> و نرم‌افزار شهباز همچنان روی محیط واقعی لازم است.
- <code dir="ltr">tools/validate_firmware_contract.py</code>: **<code dir="ltr">PASS</code>**. علاوه بر قرارداد سخت‌افزار و ایمنی، نقش محصول را نیز بررسی می‌کند: <code dir="ltr">shahbaz_interface_board</code> نقش <code dir="ltr">USB Device</code> دارد، <code dir="ltr">Android + Shahbaz</code> مسیر عملیاتی است، ویندوز فقط برای توسعه/<code dir="ltr">HIL</code> است و فایل‌های یکپارچه‌سازی و پذیرش <code dir="ltr">Android</code> باید وجود داشته باشند.
- <code dir="ltr">tools/check_firmware_safety.py</code>: **<code dir="ltr">PASS</code>** روی 53 فایل تولیدی. چون <code dir="ltr">idf.py</code> موجود نیست، ادعای ساخت هدف <code dir="ltr">ESP-IDF</code> نمی‌شود.
- <code dir="ltr">tools/validate_graphics_inputs.py</code>: **<code dir="ltr">PASS</code> با 4 هشدار** که همگی فقط مربوط به عکس‌های فیزیکی مفقود هستند.
- <code dir="ltr">tools/check_bilingual_docs.py</code>: **<code dir="ltr">PASS</code>**؛ 78 فایل <code dir="ltr">Markdown</code> در 39 جفت فارسی/انگلیسی با لینک‌های معتبر و سیاست صحیح جهت متن هستند.

## اصلاح معماری انجام‌شده

اکنون پروژه فقط یک معماری اصلی دارد:

```text
SHT30 + MS5611
 -> shahbaz_interface_board / ESP32-S3
 -> Native USB CDC / Shahbaz Protocol v2
 -> Android phone / USB Host
 -> Shahbaz Android application
 -> live sensor values + pressure/QNH barometric altitude
```

<code dir="ltr">Windows HIL</code> مسیر جداگانه عیب‌یابی سطح برد است و دیگر مقصد عملیاتی یا معیار پذیرش کامل سیستم معرفی نمی‌شود.

مرجع <code dir="ltr">Android</code> اکنون شامل هسته <code dir="ltr">Protocol v2</code>، <code dir="ltr">ShahbazLinkSession</code>، مسیر واقعی <code dir="ltr">android.hardware.usb CDC bulk</code>، درخواست صریح <code dir="ltr">UsbManager.requestPermission</code>، <code dir="ltr">ShahbazInterfaceBoardClient</code>، تعریف قابلیت <code dir="ltr">USB Host</code> و سند پذیرش فیزیکی <code dir="ltr">07_ANDROID_SHAHBAZ_INTEGRATION_TEST</code> است.

## تست‌های فیزیکی باقی‌مانده

این محیط به برد واقعی، <code dir="ltr">Sensor</code>ها، گوشی واقعی <code dir="ltr">Android</code> و اجرای نرم‌افزار شهباز، سخت‌افزار <code dir="ltr">Windows HIL</code> و <code dir="ltr">idf.py</code> دسترسی ندارد. بنابراین موارد زیر **اجراشده علامت نخورده‌اند**:

- ساخت هدف <code dir="ltr">ESP-IDF</code>، بارگذاری و راه‌اندازی روی برد واقعی؛
- عملکرد الکتریکی <code dir="ltr">I2C</code> در <code dir="ltr">400 kHz</code>؛
- <code dir="ltr">Windows Board-level HIL</code> فیزیکی؛
- **مجوز، بازشدن و چرخه اتصال/قطع اتصال واقعی <code dir="ltr">Android USB</code> در نرم‌افزار شهباز**؛
- **دریافت کامل داده زنده <code dir="ltr">SHT30/MS5611</code> در نرم‌افزار واقعی شهباز**؛
- **رفتار واقعی محاسبه ارتفاع از <code dir="ltr">QNH</code> نرم‌افزار شهباز**؛
- تأیید فیزیکی <code dir="ltr">VBUS/backfeed</code>؛
- رفتار فیزیکی خروجی و <code dir="ltr">Watchdog</code> در صورت فعال‌سازی آینده <code dir="ltr">Actuator</code>.

ترتیب لازم روی سخت‌افزار واقعی: ابتدا ساخت و بارگذاری، سپس سند 06 برای جداسازی وضعیت برد/<code dir="ltr">Firmware</code>، بعد سند 07 با گوشی واقعی و نرم‌افزار واقعی شهباز، و در پایان سند 08 برای پذیرش کامل مرحله فعلی.
