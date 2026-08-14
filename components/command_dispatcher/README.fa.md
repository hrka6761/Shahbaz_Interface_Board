# توزیع‌کننده فرمان (<code dir="ltr">Command Dispatcher</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> ورودی کنترل‌شده فرمان‌های <code dir="ltr">Decode</code>شده است و شکل <code dir="ltr">Envelope</code>، ترتیب شماره توالی در هر <code dir="ltr">Session</code>، تازگی زمان فرستنده بعد از نگاشت ساعت، سن محلی فرمان، وضعیت ایمنی و محدودیت <code dir="ltr">Payload</code> را بررسی می‌کند.

اعتبار <code dir="ltr">Session Token</code> و حذف پیشوند آن در <code dir="ltr">device_link::ProtocolEngine</code> انجام می‌شود. فرمان قدیمی یا بدون همگام‌سازی لازم، بدون تازه‌کردن <code dir="ltr">link/control freshness</code> عادی رد می‌شود. <code dir="ltr">EmergencyStop</code> و <code dir="ltr">Disarm</code> همچنان <code dir="ltr">Safety Override</code> هستند.

## فایل‌های اصلی

- <code dir="ltr">command_dispatcher.hpp</code>: <code dir="ltr">API</code> و نتیجه‌های اعتبارسنجی/<code dir="ltr">Freshness</code>.
- <code dir="ltr">command_dispatcher.cpp</code>: شماره توالی، زمان، وضعیت، <code dir="ltr">Schema</code> و مسیریابی ایمنی.
- <code dir="ltr">test_command_dispatcher.cpp</code>: حالت‌های معتبر، بدساخت، قدیمی، بدون همگام‌سازی، تکراری/خارج از ترتیب و محدودیت‌های ایمنی.
