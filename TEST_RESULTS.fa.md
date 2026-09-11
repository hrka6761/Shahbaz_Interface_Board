# گزارش اعتبارسنجی شهباز

**فارسی** | [نسخه انگلیسی](TEST_RESULTS.en.md)

تاریخ: <code dir="ltr">2026-09-11</code><br>
هدف: <code dir="ltr">shahbaz_interface_board / ESP32-S3 N16R8</code>  
<code dir="ltr">Host</code> عملیاتی: گوشی <code dir="ltr">Android</code> دارای نرم‌افزار شهباز  
<code dir="ltr">Host</code> عیب‌یابی توسعه: <code dir="ltr">Windows HIL</code>  
<code dir="ltr">Protocol</code>: <code dir="ltr">Shahbaz wire protocol v2</code> روی <code dir="ltr">Native USB CDC-ACM</code>

## موارد اجراشده در این محیط

- <code dir="ltr">CMake Configure/Build</code> برای <code dir="ltr">test/host</code>: **<code dir="ltr">PASS</code>** با تبدیل هشدار به خطا.
- <code dir="ltr">CTest</code>: **18/18 <code dir="ltr">PASS</code>** و بدون شکست در <code dir="ltr">build-host-vl53</code> با <code dir="ltr">C++17 / Release</code>؛ شامل تست انتقال واقعی <code dir="ltr">USB</code> با رابط شبیه‌سازی‌شده، پنجرهٔ زمان اندازه‌گیری، <code dir="ltr">Sensor Scheduler</code>، مرز مهلت فرمان و دوره و سرریز <code dir="ltr">PWM</code>. برای اجرای فایل‌های تست باید پوشهٔ <code dir="ltr">bin</code> ابزار <code dir="ltr">WinLibs</code> در <code dir="ltr">PATH</code> باشد.
- <code dir="ltr">tools/windows_hil_test.py --self-test</code>: **<code dir="ltr">PASS</code>** برای <code dir="ltr">Python diagnostic codec</code>، <code dir="ltr">CRC32C/COBS</code>، داده <code dir="ltr">SHT30/MS5611</code>، ساختار <code dir="ltr">Session</code> و ریاضیات ارتفاع/<code dir="ltr">QNH</code>.
- تست <code dir="ltr">Kotlin Protocol v2 + operational session</code>: **<code dir="ltr">PASS</code>**؛ شامل پاک‌سازی مرز اتصال فیزیکی، ایجاد <code dir="ltr">TimeSync/Session Token</code>، مسدودشدن فرمان وابسته به <code dir="ltr">Session</code> پیش از <code dir="ltr">TimeSync</code>، رمزگشایی <code dir="ltr">Telemetry</code> و ارتفاع با <code dir="ltr">QNH</code>.
- تست مستقل <code dir="ltr">Kotlin</code> مرجع مستقل از چارچوب را پوشش می‌دهد و چرخهٔ واقعی مجوز و اتصال دستگاه <code dir="ltr">Android USB</code> را اجرا نمی‌کند.
- <code dir="ltr">tools/validate_firmware_contract.py</code>: **<code dir="ltr">PASS</code>**. علاوه بر قرارداد سخت‌افزار و ایمنی، نقش محصول را نیز بررسی می‌کند: <code dir="ltr">shahbaz_interface_board</code> نقش <code dir="ltr">USB Device</code> دارد، <code dir="ltr">Android + Shahbaz</code> مسیر عملیاتی است، ویندوز فقط برای توسعه/<code dir="ltr">HIL</code> است و فایل‌های یکپارچه‌سازی و پذیرش <code dir="ltr">Android</code> باید وجود داشته باشند.
- <code dir="ltr">tools/check_firmware_safety.py</code>: **<code dir="ltr">PASS</code>** روی 61 فایل تولیدی. هشدارهای کشف ابزار اختیاری این اسکریپت جای ساخت مستقل <code dir="ltr">Host</code> و هدف را نمی‌گیرند.
- خودآزمایی <code dir="ltr">tools/capture_boot_log.py --self-test</code> و <code dir="ltr">tools/verify_production_build.py --self-test</code>: **<code dir="ltr">PASS</code>**؛ اولی گزارش ساختگی راه‌اندازی را بررسی می‌کند، نه راه‌اندازی فیزیکی.
- ساخت، پیوند و تولید تصویر <code dir="ltr">ESP32-S3</code> با <code dir="ltr">ESP-IDF 5.4.4</code>: **<code dir="ltr">PASS</code>** با فرمان <code dir="ltr">idf.py -B build-vl53-idf -D SHAHBAZ_ACTUATOR_BACKEND=null build</code>.
- اجرای <code dir="ltr">tools/verify_production_build.py --build-dir build-vl53-idf --require-build --actuator-backend null</code>: **<code dir="ltr">PASS</code>** روی فایل اجرایی، نقشهٔ پیوند، تنظیمات، گراف اجزا و تصویر برنامهٔ **290,544 بایتی**. اجزای خروجی فیزیکی و <code dir="ltr">LEDC</code> همچنان حذف شده‌اند و ادعای فعال‌سازی سخت‌افزار نمی‌شود.
- <code dir="ltr">tools/validate_graphics_inputs.py</code>: **<code dir="ltr">PASS</code> با 4 هشدار** که همگی فقط مربوط به عکس‌های فیزیکی مفقود هستند.
- <code dir="ltr">tools/check_bilingual_docs.py</code> و خودآزمایی کشف فایل‌های آن: **<code dir="ltr">PASS</code>**؛ 82 فایل <code dir="ltr">Markdown</code> در 41 جفت فارسی/انگلیسی با لینک‌های معتبر و سیاست صحیح جهت متن هستند.

## اصلاحات بازبینی

کار دریافت <code dir="ltr">USB</code> در هر فراخوانی و هر نوبت برنامه به هشت قطعه محدود شده است. جای پیام‌های ارسال‌نشدهٔ منقضی سریع آزاد می‌شود و پیام ناقص منقضی‌شدهٔ <code dir="ltr">COBS</code> حتی زیر فشار برگشتی، پیش از پیام بعدی با جداکننده خاتمه می‌یابد. مهلت فرمان هنگام پذیرش بررسی می‌شود تا فرمان دیررس آن را تمدید نکند. تنظیم <code dir="ltr">PWM</code> دوره‌ای را که برای همهٔ پالس‌های مجاز کافی نیست رد می‌کند و محاسبات نسبت وظیفه در برابر سرریز ایمن است. تست‌های بازتولید خطا این اصلاحات را پوشش می‌دهند؛ این نتایج اثبات مهلت بلادرنگ یا عملکرد پرواز فیزیکی نیستند.

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

در این بازبینی به برد واقعی، <code dir="ltr">Sensor</code>ها، گوشی واقعی <code dir="ltr">Android</code> و اجرای نرم‌افزار شهباز و سخت‌افزار <code dir="ltr">Windows HIL</code> دسترسی نبود. بنابراین موارد زیر **اجراشده علامت نخورده‌اند**:

- بارگذاری تصویر تأییدشدهٔ <code dir="ltr">ESP-IDF</code> و راه‌اندازی روی برد واقعی؛
- عملکرد الکتریکی <code dir="ltr">I2C</code> در <code dir="ltr">400 kHz</code>؛
- <code dir="ltr">Windows Board-level HIL</code> فیزیکی؛
- **مجوز، بازشدن و چرخه اتصال/قطع اتصال واقعی <code dir="ltr">Android USB</code> در نرم‌افزار شهباز**؛
- **دریافت کامل داده زنده <code dir="ltr">SHT30/MS5611</code> در نرم‌افزار واقعی شهباز**؛
- **رفتار واقعی محاسبه ارتفاع از <code dir="ltr">QNH</code> نرم‌افزار شهباز**؛
- تأیید فیزیکی <code dir="ltr">VBUS/backfeed</code>؛
- رفتار فیزیکی خروجی و <code dir="ltr">Watchdog</code> در صورت فعال‌سازی آینده <code dir="ltr">Actuator</code>.

ترتیب لازم روی سخت‌افزار واقعی: ابتدا ساخت و بارگذاری، سپس سند 06 برای جداسازی وضعیت برد/<code dir="ltr">Firmware</code>، بعد سند 07 با گوشی واقعی و نرم‌افزار واقعی شهباز، و در پایان سند 08 برای پذیرش کامل مرحله فعلی.
