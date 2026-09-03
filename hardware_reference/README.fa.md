# کتابخانه مرجع سخت‌افزار

**فارسی** | [نسخه انگلیسی](README.en.md)

این پوشه شواهد سخت‌افزاری و منابع سازنده‌ای را نگه می‌دارد که پروژه **<code dir="ltr">Shahbaz Interface Board (shahbaz_interface_board)</code>** بر اساس آن‌ها تصمیم‌های فنی می‌گیرد. این پوشه عمداً از <code dir="ltr">doc/</code> جدا است: <code dir="ltr">doc/</code> روش استفاده از پروژه را توضیح می‌دهد، اما <code dir="ltr">hardware_reference/</code> برای پاسخ به پرسش‌های مهندسی دقیق‌تر است؛ مثلاً اینکه برد یا <code dir="ltr">Module</code> واقعی چیست، هر ادعای مربوط به <code dir="ltr">Pin</code> یا <code dir="ltr">Protocol</code> به کدام منبع متکی است و کدام مورد هنوز روی سخت‌افزار واقعی تأیید نشده است.

## ترتیب بررسی

1. <code dir="ltr">01_esp32_s3_n16r8_development_board/</code> — منابع برد توسعه <code dir="ltr">ESP32-S3 N16R8</code>.
2. <code dir="ltr">02_sht30_i2c_module/</code> — عکس <code dir="ltr">Module</code> واقعی با برچسب <code dir="ltr">SHT3X-DIS</code> و منابع رسمی <code dir="ltr">Sensirion</code> برای <code dir="ltr">chip</code>.
3. <code dir="ltr">03_gy63_ms5611_i2c_module/</code> — عکس <code dir="ltr">Module</code> واقعی <code dir="ltr">GY-63</code> و منابع رسمی <code dir="ltr">TE Connectivity</code> برای <code dir="ltr">MS5611</code>.
4. <code dir="ltr">04_project_hardware_configuration/</code> — سیم‌بندی، سیاست <code dir="ltr">GPIO</code>، وضعیت راستی‌آزمایی و اندازه‌گیری‌های پروژه.
5. <code dir="ltr">05_project_hardware_diagrams/</code> — نمودارهای قابل ویرایش برای نمایش همین تنظیمات.
6. <code dir="ltr">06_vl53l0x_gy530_modules/</code> — قرارداد سیم‌بندی چهار نقش <code dir="ltr">VL53L0X/GY-530</code>، منابع رسمی <code dir="ltr">ST</code> و شواهد لازم پیش از فعال‌سازی.
7. <code dir="ltr">99_source_integrity/</code> — فهرست منابع و <code dir="ltr">hash</code> فایل‌های رسمی ذخیره‌شده در پروژه.

## سیاست انتخاب منبع

اولویت با مستندات شرکت سازنده است. مشخصات فعلی برد پروژه بیشترین تطابق را با طراحی رسمی <code dir="ltr">VCC-GND Studio YD-ESP32-S3</code> دارد؛ بنابراین این طراحی به‌عنوان **<span dir="ltr">candidate reference</span>** سطح برد نگه‌داری می‌شود و در کنار آن از مستندات رسمی <code dir="ltr">Espressif</code> برای <code dir="ltr">ESP32-S3</code> و <code dir="ltr">WCH</code> برای <code dir="ltr">CH343P</code> استفاده می‌شود. سازنده و <code dir="ltr">revision</code> دقیق برد فیزیکی تا زمان تطبیق عکس‌های <code dir="ltr">front/back</code> همچنان تأییدنشده است.

سازنده بردهای <code dir="ltr">breakout</code> دما/رطوبت و فشار از روی عکس‌های پروژه قابل شناسایی نیست. برای چهار <code dir="ltr">GY-530/VL53L0X Module</code> نیز هنوز عکس و رکورد سازنده همان برد واقعی در مخزن وجود ندارد. به همین دلیل هیچ صفحه فروشگاهی به‌عنوان <code dir="ltr">Datasheet</code> رسمی خود <code dir="ltr">Module</code> معرفی نشده است. در این موارد:

- عکس واقعی پروژه مرجع برچسب‌ها و <code dir="ltr">Pin</code>های قابل مشاهده روی <code dir="ltr">breakout</code> است؛
- مستندات <code dir="ltr">Sensirion</code> مرجع <code dir="ltr">Protocol</code> و حدود <code dir="ltr">chip SHT30/SHT3x</code> است؛
- مستندات <code dir="ltr">TE Connectivity</code> مرجع <code dir="ltr">Protocol</code> و حدود <code dir="ltr">chip MS5611</code> است؛
- مستندات <code dir="ltr">STMicroelectronics</code> مرجع <code dir="ltr">chip VL53L0X</code>، آدرس مشترک زمان روشن‌شدن، رفتار <code dir="ltr">XSHUT</code> و یکپارچه‌سازی چند قطعه است؛
- وجود <code dir="ltr">regulator</code>، <code dir="ltr">level shifter</code>، مقاومت‌های <code dir="ltr">pull-up</code> و محدوده تغذیه خود <code dir="ltr">breakout</code> تا زمانی که روی همان <code dir="ltr">Module</code> اندازه‌گیری یا شناسایی نشود، تأییدشده محسوب نمی‌شود.

کد فاصله‌سنج پیاده‌سازی شده، اما به‌صورت پیش‌فرض غیرفعال است. تخصیص <code dir="ltr">GPIO12..15</code> برای <code dir="ltr">XSHUT</code> فقط پیشنهاد است؛ اتصال چهار <code dir="ltr">Module</code> به <code dir="ltr">SDA/SCL</code> مشترک روی <code dir="ltr">GPIO8/GPIO9</code> بدون چهار مسیر بازبینی‌شده <code dir="ltr">XSHUT</code> قرارداد سخت‌افزاری را کامل نمی‌کند.

اگر ابتدا توضیح خواندنی می‌خواهید، فایل <code dir="ltr">../doc/فارسی/14_HARDWARE_REFERENCE_AND_DATASHEETS.fa.md</code> را بخوانید.
