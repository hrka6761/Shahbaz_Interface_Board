# 13 — راهنمای توسعه پروژه

**فارسی** | [نسخه انگلیسی](../English/13_DEVELOPER_EXTENSION_GUIDE.en.md)

## اضافه‌کردن یک <code dir="ltr">Sensor</code> جدید

ترتیب پیشنهادی:

1. قرارداد داده و واحدها را تعریف کنید.
2. <code dir="ltr">driver</code> خالص/قابل تست را از <code dir="ltr">Adapter</code> سخت‌افزاری جدا کنید.
3. <code dir="ltr">CRC/</code>جبران‌سازی/تبدیل را با بردار آزمون تست کنید.
4. <code dir="ltr">State Machine</code> و <code dir="ltr">timeout/</code>تلاش مجدد کران‌دار طراحی کنید.
5. <code dir="ltr">Scheduler</code> <code dir="ltr">Sensor</code>ها را توسعه دهید بدون اینکه <code dir="ltr">Sensor</code>های موجود <code dir="ltr">starve</code> شوند.
6. <code dir="ltr">Telemetry</code> <code dir="ltr">ABI</code> را اضافه کنید.
7. <code dir="ltr">Python HIL</code>، کد <code dir="ltr">Kotlin Protocol/Session</code> و رابط یکپارچه‌سازی <code dir="ltr">Android</code> را همزمان به‌روزرسانی کنید.
8. تست واحد و یکپارچه‌سازی بنویسید.
9. <code dir="ltr">HIL</code> واقعی برای <code dir="ltr">Sensor</code> اضافه کنید.
10. مستندات 01، 02، 05، 06، 08 و 12 را در صورت اثرگذاری به‌روزرسانی کنید.

## قواعد توسعه

- <code dir="ltr">Buffer</code>ها و <code dir="ltr">Queue</code>ها کران‌دار باشند.
- تلاش مجدد یا <code dir="ltr">wait</code> بی‌نهایت وجود نداشته باشد.
- خرابی یک <code dir="ltr">subsystem</code>، <code dir="ltr">Heartbeat</code>/ایمنی را متوقف نکند.
- اتصال فیزیکی مجدد و بازشدن دوباره <code dir="ltr">CDC DTR</code> باید پیش از پذیرش <code dir="ltr">Session Token</code> تصادفی تازه، همه وضعیت‌های <code dir="ltr">RX/TX</code>، <code dir="ltr">Parser</code>، توالی، نگاشت زمان، <code dir="ltr">Telemetry</code> و دیگر وضعیت‌های <code dir="ltr">Session</code> را <code dir="ltr">Reset</code> کند.
- هر خروجی فیزیکی جدید پشت <code dir="ltr">SafetySupervisor</code> قرار گیرد.
- زمان اجرا در حالت پایدار به <code dir="ltr">memory allocation</code> بدون حد تکیه نکند.
- هر تغییر <code dir="ltr">Protocol</code> باید تست دو سمت <code dir="ltr">Firmware</code>/اندروید داشته باشد.

## محل فایل‌ها

- مستندات کاربر و توسعه‌دهنده: <code dir="ltr">doc/</code>.
- فایل‌های خام مرجع، <code dir="ltr">Datasheet</code>، عکس، فهرست ثبت و منبع نمودار: <code dir="ltr">hardware_reference/</code>.
- نتیجه تست ثبت‌شده: <code dir="ltr">TEST_RESULTS.fa.md</code>.

برای اطلاعات <code dir="ltr">Pin</code> و <code dir="ltr">Datasheet</code> فایل بعدی را بخوانید: <code dir="ltr">14_HARDWARE_REFERENCE_AND_DATASHEETS.fa.md</code>.
## قواعد افزوده برای توسعه <code dir="ltr">v2</code>

- <code dir="ltr">Reconnect</code> باید پیش از پذیرش <code dir="ltr">Session Token</code> تصادفی تازه، <code dir="ltr">RX/TX</code>، <code dir="ltr">Parser</code>، شماره توالی، نگاشت زمان، وضعیت <code dir="ltr">Telemetry</code> و هر وضعیت وابسته به <code dir="ltr">Session</code> را پاک کند.
- هر فرمان جدید وابسته به <code dir="ltr">Session</code> باید پیشوند <code dir="ltr">Session Token v2</code> و سیاست صریح <code dir="ltr">Sender-Time Freshness</code> داشته باشد.
- هر تغییر <code dir="ltr">Protocol</code> باید هم‌زمان در <code dir="ltr">Firmware</code>، منطق مستقل <code dir="ltr">Kotlin Session</code>، رابط <code dir="ltr">Android UsbManager</code> و <code dir="ltr">Python Windows HIL</code> پوشش داده شود.
- ادعای سخت‌افزار/<code dir="ltr">Evidence</code> باید در <code dir="ltr">Manifest</code> ماشین‌خوان و <code dir="ltr">tools/validate_firmware_contract.py</code> ثبت شود؛ یک رشته غیرخالی آزاد به‌تنهایی مجوز سخت‌افزاری نیست.
- <code dir="ltr">Initialize</code> شدن خروجی فیزیکی فقط بعد از اعتبارسنجی برد، <code dir="ltr">Pin</code> و <code dir="ltr">Evidence</code> مجاز است.
- هر سرویس جدیدی که می‌تواند <code dir="ltr">Block</code> شود باید محدود باشد تا <code dir="ltr">app_main</code> بتواند داخل <code dir="ltr">Watchdog Timeout</code> بررسی‌شده، <code dir="ltr">TWDT</code> را <code dir="ltr">Feed</code> کند.
