# فایل‌های تنظیم سخت‌افزار پروژه

**فارسی** | [نسخه انگلیسی](README.en.md)

این فایل‌ها رکوردهای مهندسی قابل پردازش توسط ابزارهای پروژه هستند. آن‌ها مشخص می‌کنند **شهباز برای چه سخت‌افزاری تنظیم شده است**، **کدام ادعا به منبع سازنده متکی است**، **کدام مورد هنوز به بررسی سخت‌افزار واقعی نیاز دارد** و قرارداد نقش محصول چیست: <code dir="ltr">shahbaz_interface_board</code> نقش <code dir="ltr">USB Device</code> دارد، <code dir="ltr">Android + Shahbaz</code> مسیر عملیاتی است و ویندوز فقط برای توسعه و <code dir="ltr">HIL</code> استفاده می‌شود.

- <code dir="ltr">project_hardware_profile.yaml</code> — برد هدف، <code dir="ltr">Module</code>، <code dir="ltr">I2C</code>، <code dir="ltr">USB</code>، حافظه، تغذیه، وضعیت هویت <code dir="ltr">Module</code>های اندازه‌گیری و تنظیمات ایمنی پیش‌فرض.
- <code dir="ltr">project_wiring.csv</code> — قرارداد اتصال‌ها به تفکیک هر مسیر.
- <code dir="ltr">gpio_usage_rules.csv</code> — سیاست تخصیص، رزرو و محدودیت <code dir="ltr">GPIO</code>ها.
- <code dir="ltr">hardware_photos.yaml</code> — عکس‌های موردنیاز برای راستی‌آزمایی، <code dir="ltr">hash</code> و وضعیت هر شاهد.
- <code dir="ltr">hardware_measurements.yaml</code> — اندازه‌گیری‌هایی که باید روی سخت‌افزار مونتاژشده ثبت شوند.

مقداری را صرفاً برای <code dir="ltr">PASS</code> شدن <code dir="ltr">validator</code> تغییر ندهید. وضعیت‌هایی مانند <code dir="ltr">UNVERIFIED</code> وقتی شاهد فیزیکی دقیق موجود نیست، عمدی و ضروری هستند.

## مجوزدهی <code dir="ltr">Evidence</code> در <code dir="ltr">Firmware</code>

ابزار <code dir="ltr">tools/validate_firmware_contract.py</code> تنظیمات <code dir="ltr">Firmware</code> و نسخه‌های <code dir="ltr">Protocol</code> را با این رکوردها تطبیق می‌دهد. هنگام <code dir="ltr">Build ESP-IDF</code> نیز ادعاهای فعال‌شده در <code dir="ltr">Kconfig</code> با <code dir="ltr">Manifest</code> واقعی بررسی می‌شوند. صرفاً غیرخالی بودن <code dir="ltr">Evidence ID</code> مجوز نیست؛ رکورد باید موجود، دارای وضعیت فیزیکی/اندازه‌گیری مجاز و مناسب همان هدف باشد.
