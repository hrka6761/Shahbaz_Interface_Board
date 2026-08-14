# پشتیبانی برد و سیاست <code dir="ltr">Pin</code> (<code dir="ltr">Board Support</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

این بخش سیاست مرکزی و <code dir="ltr">fail-closed</code> برای هدف <code dir="ltr">ESP32-S3 N16R8</code> است. <code dir="ltr">I2C</code> پیش‌فرض روی <code dir="ltr">GPIO8/9</code> قرار دارد و <code dir="ltr">GPIO19/20</code> برای <code dir="ltr">USB</code> داخلی رزرو هستند. <code dir="ltr">Pin</code>‌های ناموجود <code dir="ltr">22..25</code>، مسیر حافظه <code dir="ltr">26..37</code>، <code dir="ltr">Pin</code>‌های حساس <code dir="ltr">strapping</code>، تشخیصی و منابع وابسته به <code dir="ltr">Revision</code> بدون بررسی قابل تخصیص نیستند.

<code dir="ltr">Pin</code> جایگزین <code dir="ltr">I2C</code> به بررسی فیزیکی برد دقیق نیاز دارد. <code dir="ltr">Pin</code>‌های <code dir="ltr">Actuator</code> نیز باید فقط از گروه <code dir="ltr">AvailableWithReview</code>، یکتا و همراه با <code dir="ltr">CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED=y</code> و <code dir="ltr">Evidence Record ID</code> باشند. <code dir="ltr">Contract Validator</code> زمان <code dir="ltr">Build</code> نیز بررسی می‌کند که این <code dir="ltr">ID</code> واقعاً به رکورد مجاز <code dir="ltr">Manifest</code> اشاره کند؛ یک رشته دلخواه مجوز محسوب نمی‌شود.
