# نمودارهای سخت‌افزار پروژه

**فارسی** | [نسخه انگلیسی](README.en.md)

این فایل‌های <code dir="ltr">Mermaid</code> نمودارهای قابل ویرایش هستند و از همان فرضیات سخت‌افزاری ثبت‌شده در <code dir="ltr">../04_project_hardware_configuration/</code> استفاده می‌کنند.

ترتیب بررسی:

1. <code dir="ltr">01_system_wiring.mmd</code> — مسیر عملیاتی <code dir="ltr">Android + Shahbaz</code> و مسیر جداگانه عیب‌یابی <code dir="ltr">Windows HIL</code>.
2. <code dir="ltr">02_i2c_bus.mmd</code> — <code dir="ltr">I2C Bus</code> مشترک و تنظیم آدرس/<code dir="ltr">strap</code> مخصوص هر <code dir="ltr">Module</code>.
3. <code dir="ltr">03_usb_ports.mmd</code> — تفاوت <code dir="ltr">native ESP32-S3 USB</code> با <code dir="ltr">CH343P USB-to-UART Connector</code> جداگانه.
4. <code dir="ltr">04_power_and_usb_vbus.mmd</code> — مرز ایمنی تغذیه و <code dir="ltr">USB Host</code>.
5. <code dir="ltr">05_gpio_usage.mmd</code> — گروه‌های <code dir="ltr">GPIO</code> تخصیص‌یافته، رزروشده، غیرقابل‌استفاده و نیازمند بازبینی.

این نمودارها منبع مستندسازی هستند و اثبات فیزیکی محسوب نمی‌شوند. هرجا ادعا به تطبیق یا اندازه‌گیری سخت‌افزار واقعی وابسته است، برچسب <code dir="ltr">UNVERIFIED</code> باید باقی بماند.
