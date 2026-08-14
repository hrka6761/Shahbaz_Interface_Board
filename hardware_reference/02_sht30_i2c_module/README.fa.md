# مرجع <code dir="ltr">SHT30 I2C Module</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

پروژه از یک <code dir="ltr">breakout</code> <code dir="ltr">Generic</code> با چهار <code dir="ltr">Pin</code> و برچسب **<code dir="ltr">SHT3X-DIS</code>** استفاده می‌کند. سازنده خود برد <code dir="ltr">breakout</code> مشخص نیست؛ به همین دلیل عکس <code dir="ltr">Module</code> واقعی پروژه از مستندات رسمی <code dir="ltr">Sensirion</code> برای <code dir="ltr">chip</code> جدا نگه داشته شده است.

## محتوا

- <code dir="ltr">01_official_sensirion_chip_docs/</code> — <code dir="ltr">Datasheet</code> رسمی <code dir="ltr">SHT3x-DIS</code>، صفحه رسمی <code dir="ltr">SHT30</code> و راهنماهای رسمی نگهداری و تست محیطی.
- <code dir="ltr">02_actual_project_module_photos/</code> — عکس همان <code dir="ltr">Module</code> واقعی که برای پروژه ارائه شده است.

## موارد تأییدشده برای پروژه

برچسب‌های قابل مشاهده روی <code dir="ltr">breakout</code> عبارت‌اند از <code dir="ltr">VCC</code>، <code dir="ltr">GND</code>، <code dir="ltr">SDA</code> و <code dir="ltr">SCL</code>. <code dir="ltr">Firmware</code> برای آدرس <code dir="ltr">I2C 0x44</code> تنظیم شده است. <code dir="ltr">Sensirion</code> آدرس <code dir="ltr">0x44</code> را در حالت <code dir="ltr">ADDR=Low</code> به‌عنوان آدرس پیش‌فرض و <code dir="ltr">0x45</code> را برای <code dir="ltr">ADDR=High</code> مستند کرده است.

چون سازنده <code dir="ltr">breakout</code> مشخص نیست، درباره <code dir="ltr">regulator</code>، <code dir="ltr">level shifting</code>، مقدار مقاومت‌های <code dir="ltr">pull-up</code> یا محدوده ورودی <code dir="ltr">VCC</code> بر اساس صفحات فروشگاهی نامرتبط فرض نمی‌کنیم. در پروژه این <code dir="ltr">Module</code> از ریل <code dir="ltr">3.3 V</code> برد <code dir="ltr">ESP32</code> تغذیه می‌شود و ارتباط روی سخت‌افزار مونتاژشده تست می‌شود.
