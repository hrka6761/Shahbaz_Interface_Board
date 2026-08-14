# <code dir="ltr">Scheduler</code> <code dir="ltr">Sensor</code>ها (<code dir="ltr">Sensor Scheduler</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> کار <code dir="ltr">SHT30</code> و <code dir="ltr">MS5611</code> را روی <code dir="ltr">Bus</code> <code dir="ltr">I2C</code> مشترک طوری هماهنگ می‌کند که بقیه <code dir="ltr">Firmware</code> متوقف نشود. مهلت نمونه‌برداری، تلاش مجدد/بازیابی محدود و انتشار نمونه‌های کامل‌شده در این بخش مدیریت می‌شوند.

## فایل‌های اصلی

- <code dir="ltr">include/shahbaz/sensors/sensor_scheduler.hpp</code>: وضعیت‌ها، مهلت‌ها، عملیات‌ها و قرارداد انتشار.
- <code dir="ltr">src/sensor_scheduler.cpp</code>: توالی‌های غیرمسدودکننده برای <code dir="ltr">SHT30/MS5611</code>، تغییر <code dir="ltr">Rate</code>، تلاش مجدد و بازیابی.
- <code dir="ltr">test/sensor_scheduler_test.cpp</code>: تست با <code dir="ltr">I2C/clock</code> جعلی برای <code dir="ltr">Scheduler</code>ی، عدالت <code dir="ltr">Scheduler</code>ی، خطا، بازیابی و نمونه منتشرشده.

<code dir="ltr">Scheduler</code> به <code dir="ltr">Interface</code>‌ها و <code dir="ltr">Domain</code> و هر دو <code dir="ltr">Component</code> <code dir="ltr">Sensor Domain</code> وابسته است، اما پیاده‌سازی واقعی <code dir="ltr">I2C</code> در خارج از آن قرار دارد.
