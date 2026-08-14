# ابزارهای لایه برنامه

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> ابزارهای کوچک و قطعی مورد نیاز لایه برنامه را نگه می‌دارد.

## فایل‌های اصلی

- <code dir="ltr">include/shahbaz/application/fixed_priority_queue.hpp</code>: <code dir="ltr">Queue</code> اولویت کران‌دار برای مرتب‌کردن کارها بدون <code dir="ltr">memory allocation</code> از <code dir="ltr">dynamic heap</code>.
- <code dir="ltr">test/fixed_priority_queue_test.cpp</code>: ترتیب، ظرفیت، حالت پر/خالی و مرزهای <code dir="ltr">Queue</code> را تست می‌کند.
- <code dir="ltr">CMakeLists.txt</code>: <code dir="ltr">Component</code> <code dir="ltr">header-only</code> با <code dir="ltr">C++17</code> را ثبت می‌کند.

این بخش به زمان اجرا مربوط به <code dir="ltr">ESP-IDF</code> یا سخت‌افزار وابسته نیست.
