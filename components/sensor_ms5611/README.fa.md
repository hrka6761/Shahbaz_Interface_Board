# منطق <code dir="ltr">Sensor</code> <code dir="ltr">MS5611</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> محاسبات تعریف‌شده در <code dir="ltr">Datasheet</code> <code dir="ltr">MS5611</code> را از <code dir="ltr">I2C</code> و <code dir="ltr">Scheduler</code>ی جدا نگه می‌دارد تا کالیبراسیون و جبران‌سازی با بردارهای آزمون قطعی قابل بررسی باشند.

## فایل‌های اصلی

- <code dir="ltr">include/sensor_ms5611/ms5611_domain.hpp</code>: داده <code dir="ltr">PROM</code> کالیبراسیون، <code dir="ltr">ADC</code> خام، اعتبارسنجی <code dir="ltr">CRC4</code> و <code dir="ltr">API</code> نتیجه جبران‌شده.
- <code dir="ltr">src/ms5611_domain.cpp</code>: اعتبارسنجی <code dir="ltr">PROM</code> و جبران‌سازی فشار/دما در مرتبه اول و دوم.
- <code dir="ltr">test/ms5611_domain_test.cpp</code>: بردارهای <code dir="ltr">Datasheet</code>، <code dir="ltr">CRC</code> خراب، <code dir="ltr">PROM</code> نامعتبر، جبران‌سازی دمای پایین و مرزهای عددی.

<code dir="ltr">Transaction</code>های واقعی <code dir="ltr">I2C</code> در لایه <code dir="ltr">Scheduler</code>/<code dir="ltr">Hardware Platform</code> انجام می‌شوند، نه در این <code dir="ltr">Component</code> <code dir="ltr">Domain</code>.
