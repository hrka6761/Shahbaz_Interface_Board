# 08 — پذیرش کامل سیستم

**فارسی** | [نسخه انگلیسی](../English/08_COMPLETE_SYSTEM_ACCEPTANCE.en.md)

این سند تعیین می‌کند مرحله فعلی **یکپارچه‌سازی عملیاتی شهباز** قابل پذیرش است یا خیر.

## بخش <code dir="ltr">A</code> — کد و قراردادها

- [ ] همه تست‌های <code dir="ltr">C++</code> موفق هستند.
- [ ] تست مستقل <code dir="ltr">Python Protocol/HIL codec</code> موفق است.
- [ ] تست <code dir="ltr">Kotlin Protocol v2 + Android Session</code> موفق است.
- [ ] <code dir="ltr">Firmware/Hardware/Product Contract Validator</code> موفق است.
- [ ] بررسی ایمنی و مستندات موفق است.
- [ ] <code dir="ltr">idf.py build</code> برای <code dir="ltr">ESP32-S3</code> موفق است.

## بخش <code dir="ltr">B</code> — اعتبارسنجی برد

- [ ] <code dir="ltr">Firmware</code> روی برد اجرا می‌شود و <code dir="ltr">Reset</code> غیرمنتظره ندارد.
- [ ] <code dir="ltr">SHT30</code> پایدار است.
- [ ] <code dir="ltr">MS5611</code> پایدار است.
- [ ] <code dir="ltr">Windows HIL</code> به‌عنوان **دروازه عیب‌یابی مستقل** موفق است.

موفقیت این بخش به‌تنهایی پذیرش محصول نیست.

## بخش <code dir="ltr">C</code> — یکپارچه‌سازی عملیاتی <code dir="ltr">Android + Shahbaz</code>

- [ ] گوشی واقعی <code dir="ltr">Android</code> برای <code dir="ltr">Native USB</code> برد نقش <code dir="ltr">USB Host</code> دارد.
- [ ] نرم‌افزار شهباز مجوز <code dir="ltr">USB</code> را دریافت و مسیر <code dir="ltr">CDC</code> را باز می‌کند.
- [ ] <code dir="ltr">Protocol v2 TimeSync</code> و <code dir="ltr">Session Token</code> غیرصفر موفق هستند.
- [ ] <code dir="ltr">Heartbeat</code> و نگهداری <code dir="ltr">Session</code> سالم است.
- [ ] دما و رطوبت زنده <code dir="ltr">SHT30</code> وارد نرم‌افزار شهباز می‌شود.
- [ ] فشار زنده <code dir="ltr">MS5611</code> وارد نرم‌افزار شهباز می‌شود.
- [ ] نرم‌افزار شهباز ارتفاع را از فشار و <code dir="ltr">QNH</code> خودش محاسبه می‌کند.
- [ ] تغییر <code dir="ltr">QNH</code> ارتفاع را تغییر می‌دهد اما فشار خام را تغییر نمی‌دهد.
- [ ] قطع و اتصال مجدد یک <code dir="ltr">Session</code> تازه و بدون وضعیت مانده ایجاد می‌کند.
- [ ] رد مجوز و وقفه‌های چرخه عمر ایمن مدیریت می‌شوند.

## بخش <code dir="ltr">D</code> — ایمنی روی میز

- [ ] <code dir="ltr">CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n</code> استفاده می‌شود.
- [ ] هیچ موتور، <code dir="ltr">ESC</code> یا <code dir="ltr">Servo</code> ناخواسته فعال نمی‌شود.
- [ ] تغذیه <code dir="ltr">Sensor</code>ها با <code dir="ltr">3.3V</code> و زمین مشترک سازگار است.
- [ ] پیش از تغذیه هم‌زمان، الزامات تأییدشده <code dir="ltr">Android VBUS/backfeed</code> رعایت شده‌اند.

## نتیجه نهایی

فقط وقتی همه بخش‌های <code dir="ltr">A</code> تا <code dir="ltr">D</code> موفق باشند، این مسیر پذیرفته می‌شود:

```text
ESP32-S3 + SHT30 + MS5611 + Native USB
                -> Android phone / USB Host
                -> Shahbaz Android application
```

<code dir="ltr">Windows HIL</code> فقط نتیجه کمکی توسعه و عیب‌یابی است و هرگز جای بخش <code dir="ltr">C</code> را نمی‌گیرد.

در صورت شکست، <code dir="ltr">09_TROUBLESHOOTING.fa.md</code> را دنبال کنید.
