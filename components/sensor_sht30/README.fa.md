# منطق <code dir="ltr">Sensor</code> <code dir="ltr">SHT30</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> اعتبارسنجی پاسخ <code dir="ltr">SHT30</code> و فرمول‌های تبدیل <code dir="ltr">Datasheet</code> را از <code dir="ltr">I2C</code> و <code dir="ltr">Scheduler</code>ی جدا نگه می‌دارد.

## فایل‌های اصلی

- <code dir="ltr">include/sensor_sht30/sht3x_domain.hpp</code>: تجزیه پاسخ خام، <code dir="ltr">CRC8</code> و <code dir="ltr">API</code> نتیجه دما/رطوبت.
- <code dir="ltr">src/sht3x_domain.cpp</code>: بررسی <code dir="ltr">CRC</code> <code dir="ltr">Sensor</code> <code dir="ltr">Sensirion</code> و تبدیل به واحدهای فیزیکی.
- <code dir="ltr">test/sht3x_domain_test.cpp</code>: بردارهای شناخته‌شده، <code dir="ltr">CRC</code> مستقل هر واژه داده، <code dir="ltr">Frame</code> خراب و مرزهای تبدیل.

<code dir="ltr">Transaction</code>های واقعی <code dir="ltr">I2C</code> در لایه <code dir="ltr">Scheduler</code>/<code dir="ltr">Hardware Platform</code> انجام می‌شوند.
