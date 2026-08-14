# نوع‌های داده <code dir="ltr">Domain</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> نوع‌های خنثی مربوط به اندازه‌گیری را تعریف می‌کند تا <code dir="ltr">Sensor</code>ها، <code dir="ltr">Scheduler</code>، <code dir="ltr">Telemetry</code> و تست‌ها بدون <code dir="ltr">dependency</code> مستقیم به یکدیگر از یک قرارداد داده مشترک استفاده کنند.

## فایل‌های اصلی

- <code dir="ltr">include/shahbaz/domain/measurement.hpp</code>: اندازه‌گیری‌های نوع‌دار، مُهر زمانی، اعتبار، کیفیت و سلامت/وضعیت.
- <code dir="ltr">test/measurement_test.cpp</code>: ساخت داده، مقادیر ذخیره‌شده و رفتار اعتبار را تست می‌کند.
- <code dir="ltr">CMakeLists.txt</code>: <code dir="ltr">Component</code> <code dir="ltr">header-only</code> با <code dir="ltr">C++17</code>.

در این بخش هیچ کد وابسته به <code dir="ltr">ESP-IDF</code>، <code dir="ltr">FreeRTOS</code>، <code dir="ltr">USB</code> یا سخت‌افزار وجود ندارد.
