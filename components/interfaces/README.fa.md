# <code dir="ltr">Interface</code>ها مستقل از <code dir="ltr">Platform</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> قراردادهای مستقل از سخت‌افزار و <code dir="ltr">Hardware Platform</code> را تعریف می‌کند تا بخش عمده منطق <code dir="ltr">Firmware</code> بدون <code dir="ltr">ESP-IDF</code> یا برد واقعی قابل تست باشد.

## <code dir="ltr">Interface</code>ها اصلی

- <code dir="ltr">byte_view.hpp</code>: نمای بدون مالکیت از محدوده بایت برای عبور <code dir="ltr">Buffer</code> بدون <code dir="ltr">memory allocation</code>.
- <code dir="ltr">board_validator.hpp</code>: قرارداد اعتبارسنجی زمان اجرا برد و حافظه.
- <code dir="ltr">i2c_bus.hpp</code>: مرز <code dir="ltr">Transaction</code> مورد استفاده <code dir="ltr">Sensor</code>ها و <code dir="ltr">Scheduler</code>.
- <code dir="ltr">monotonic_clock.hpp</code>: منبع زمان یکنواخت برای <code dir="ltr">Scheduler</code>ی و <code dir="ltr">Timeout</code>.
- <code dir="ltr">telemetry_transport.hpp</code>: قرارداد ارسال/دریافت بایت‌های <code dir="ltr">Telemetry</code>.
- <code dir="ltr">sample_publisher.hpp</code>: قرارداد خروجی نمونه‌های کامل‌شده <code dir="ltr">Sensor</code>.

پیاده‌سازی واقعی <code dir="ltr">Hardware Platform</code> در <code dir="ltr">Adapter</code>هایی مانند <code dir="ltr">platform_espidf</code> و <code dir="ltr">usb_transport_espidf</code> قرار می‌گیرد.
