# <code dir="ltr">Scheduler</code> <code dir="ltr">Sensor</code>ها (<code dir="ltr">Sensor Scheduler</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> کار <code dir="ltr">SHT30</code> و <code dir="ltr">MS5611</code> را روی <code dir="ltr">Bus</code> <code dir="ltr">I2C</code> مشترک طوری هماهنگ می‌کند که بقیه <code dir="ltr">Firmware</code> متوقف نشود. مهلت نمونه‌برداری، تلاش مجدد/بازیابی محدود و انتشار نمونه‌های کامل‌شده در این بخش مدیریت می‌شوند.

## فایل‌های اصلی

علاوه بر <code dir="ltr">SHT30</code> و <code dir="ltr">MS5611</code>، آرایهٔ اختیاری چهار <code dir="ltr">VL53L0X</code> نیز روی همین <code dir="ltr">I2C</code> مدیریت می‌شود.

- <code dir="ltr">include/shahbaz/sensors/sensor_scheduler.hpp</code>: وضعیت‌ها، مهلت‌ها، عملیات‌ها و قرارداد انتشار.
- <code dir="ltr">src/sensor_scheduler.cpp</code>: توالی‌های غیرمسدودکننده برای <code dir="ltr">SHT30/MS5611</code>، تغییر <code dir="ltr">Rate</code>، تلاش مجدد و بازیابی.
- <code dir="ltr">test/sensor_scheduler_test.cpp</code>: تست با <code dir="ltr">I2C/clock</code> جعلی برای <code dir="ltr">Scheduler</code>ی، عدالت <code dir="ltr">Scheduler</code>ی، خطا، بازیابی و نمونه منتشرشده.

<code dir="ltr">Scheduler</code> به <code dir="ltr">Interface</code>‌ها و <code dir="ltr">Domain</code> و هر سه <code dir="ltr">Component</code> مربوط به <code dir="ltr">Sensor</code> وابسته است، اما پیاده‌سازی واقعی <code dir="ltr">I2C</code> در خارج از آن قرار دارد.

## زمان اندازه‌گیری، عدم قطعیت و نرخ نمونه‌برداری

زمان یکنواخت برد پیش از فرمان شروع تبدیل و پس از خواندن موفق نتیجه ثبت می‌شود. <code dir="ltr">AcquisitionWindow</code> نقطهٔ میانی این بازه را در <code dir="ltr">monotonic_timestamp_us</code> و نیم‌عرض گرد‌شده رو به بالا را در فیلد 8 با نام <code dir="ltr">AcquisitionTimeUncertaintyMicros</code> قرار می‌دهد. نوع فیلد <code dir="ltr">Unsigned32</code> و واحد آن میکروثانیه است. تأخیر خواندن، عدم قطعیت را افزایش می‌دهد؛ انتشار، پاک‌کردن وقفه و دریافت در <code dir="ltr">Android</code> نباید زمان مشاهده را دوباره تولید کنند. مقدار <code dir="ltr">UINT32_MAX</code> یعنی عدم قطعیت نامحدود یا غیرقابل نمایش.

در <code dir="ltr">MS5611</code> زمان نمونه و فیلد 8 مربوط به تبدیل فشار <code dir="ltr">D1</code> است. دمای داخلی از آخرین <code dir="ltr">D2</code> معتبر برای جبران فشار استفاده می‌کند و مشاهدهٔ هم‌زمان مستقل نیست. تازگی دما از ابتدای تبدیل <code dir="ltr">D2</code> تا پایان خواندن فشار سنجیده می‌شود؛ حداکثر عمر پیش‌فرض 400 میلی‌ثانیه است. <code dir="ltr">PROM CRC</code> اعتبار دادهٔ کالیبراسیون را می‌سنجد و به معنی وجود <code dir="ltr">CRC</code> برای هر نتیجهٔ <code dir="ltr">ADC</code> نیست. تخمین ارتفاع، <code dir="ltr">QNH</code>، فیلتر، عدم قطعیت حالت و تصمیم پرواز در <code dir="ltr">Android</code> انجام می‌شود.

| تولیدکننده | فاصلهٔ درخواستی پیش‌فرض | رفتار |
|---|---:|---|
| <code dir="ltr">SHT30</code> | 500 میلی‌ثانیه | تکرارپذیری بالا، حداکثر زمان تبدیل به‌علاوه 2 میلی‌ثانیه؛ بررسی <code dir="ltr">CRC</code> نتیجه |
| فشار <code dir="ltr">MS5611 D1</code> | 40 میلی‌ثانیه | <code dir="ltr">OSR4096</code> با حاشیهٔ 200 میکروثانیه |
| دمای <code dir="ltr">MS5611 D2</code> | 250 میلی‌ثانیه | <code dir="ltr">OSR4096</code>؛ استفادهٔ محدود از دمای قبلی برای جبران |
| هر نقش <code dir="ltr">VL53L0X</code> | 100 میلی‌ثانیه | اندازه‌گیری ترتیبی، <code dir="ltr">Timeout</code> برابر 60 میلی‌ثانیه، فاصله از شروع تا شروع |

این فاصله‌ها نرخ تضمین‌شدهٔ دریافت نیستند. چهار فاصله‌سنج به‌صورت ترتیبی کار می‌کنند و تبدیل، دسترسی مشترک <code dir="ltr">I2C</code>، بازیابی و خدمات <code dir="ltr">USB/RTOS</code> بر نرخ واقعی اثر دارند. دریافت‌کننده باید شمارهٔ توالی و زمان هر مشاهده را استفاده کند.

تابع <code dir="ltr">service_ready(4)</code> تغییر وضعیت‌های آماده را تا اولین عملیات محدود <code dir="ltr">I2C</code> یا بازیابی، یا پایان بودجهٔ چهار تغییر پیش می‌برد. عدالت نوبت‌دهی و مانع توقف پس از بازیابی حفظ می‌شود. میان این بخش‌ها، دریافت فرمان، ارسال، ایمنی و <code dir="ltr">Watchdog</code> اجرا می‌شوند. حلقه یک <code dir="ltr">Tick</code> منتظر می‌ماند؛ مقدار پیش‌فرض <code dir="ltr">CONFIG_FREERTOS_HZ=1000</code> تضمین زمان واقعی نیست.

فیلد 8 از قالب موجود <code dir="ltr">Protocol v2</code> استفاده می‌کند و ساختار 22 بایتی <code dir="ltr">Header</code> یا <code dir="ltr">CRC/COBS</code> تغییر نمی‌کند. نسخهٔ جدید <code dir="ltr">Android</code> این عدم قطعیت را با خطای همگام‌سازی ساعت و عمر ارتباط ترکیب می‌کند. نمونهٔ قدیمی بدون فیلد 8 برای نمایش قابل‌خواندن است اما زمان نامعلوم آن نباید مجوز کنترل دقیق بدهد. نسخه‌های قدیمی با مجموعهٔ فیلد سخت‌گیرانه ممکن است نمونهٔ جدید را رد کنند؛ نسخه‌های هماهنگ نرم‌افزار و <code dir="ltr">Firmware</code> لازم‌اند. [قرارداد ارتباط](../../doc/فارسی/12_USB_PROTOCOL.fa.md) را ببینید.

تست‌های <code dir="ltr">Host</code> تأخیر خواندن و انتشار، محاسبات بازه و سرریز، نرخ درخواستی، بودجهٔ اجرا، عدالت، عمر جبران دما و مانع بازیابی را بررسی می‌کنند. بررسی نویز و زمان سخت‌افزار، لرزش، جریان ملخ، <code dir="ltr">HIL</code> و پذیرش پرواز همچنان به شواهد جداگانه نیاز دارند.
