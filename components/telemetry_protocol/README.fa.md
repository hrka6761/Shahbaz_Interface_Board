# <code dir="ltr">Shahbaz Telemetry Protocol</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> پیاده‌سازی بدون <code dir="ltr">memory allocation</code> از **<code dir="ltr">Shahbaz wire protocol v2</code>** است. قالب فیزیکی <code dir="ltr">Frame</code> همچنان شامل <code dir="ltr">Header</code> ثابت 22 بایتی <code dir="ltr">little-endian</code>، حداکثر 512 بایت <code dir="ltr">Payload</code>، <code dir="ltr">CRC-32C</code>، <code dir="ltr">COBS</code> و جداکننده صفر است. <code dir="ltr">Parser</code> افزایشی داده خراب را رد می‌کند و روی جداکننده بعدی <code dir="ltr">resync</code> می‌شود.

معنای <code dir="ltr">Session/Freshness</code> نسخه 2 در لایه‌های بالاتر اعمال می‌شود: فرمان‌های وابسته به <code dir="ltr">Session</code> یک <code dir="ltr">Session Token</code> 64 بیتی مذاکره‌شده دارند و لایه‌های <code dir="ltr">device_link/command_dispatcher</code> اعتبار <code dir="ltr">Token</code>، زمان فرستنده، شماره توالی، وضعیت ایمنی و فرمان <code dir="ltr">Actuator</code> را کنترل می‌کنند.
