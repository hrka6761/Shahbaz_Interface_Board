# 03 — آماده‌سازی ابزارهای توسعه

**فارسی** | [نسخه انگلیسی](../English/03_DEVELOPMENT_TOOLS_SETUP.en.md)

این سند محیط **توسعه** را آماده می‌کند. ویندوز در این بخش برای اجرای اسکریپت‌های <code dir="ltr">PowerShell</code>، ساخت <code dir="ltr">Firmware</code> و <code dir="ltr">HIL</code> استفاده می‌شود. مسیر عملیاتی محصول همچنان گوشی <code dir="ltr">Android</code> دارای نرم‌افزار شهباز است.

## ابزارهای لازم

| ابزار | کاربرد |
|---|---|
| <code dir="ltr">ESP-IDF 5.4+</code> | ساخت و بارگذاری <code dir="ltr">Firmware</code> روی <code dir="ltr">ESP32-S3</code> |
| <code dir="ltr">Python 3</code> | <code dir="ltr">Validator</code>ها و <code dir="ltr">Windows HIL</code> |
| <code dir="ltr">CMake + C++ compiler</code> | تست‌های مستقل از سخت‌افزار |
| <code dir="ltr">PowerShell</code> | اسکریپت‌های کمکی توسعه |
| <code dir="ltr">Kotlin compiler + Java</code> | تست مستقل <code dir="ltr">Protocol/Session</code> |
| <code dir="ltr">Android Studio / Android SDK</code> | یکپارچه‌سازی <code dir="ltr">USB Host</code> واقعی در نرم‌افزار شهباز |

## بررسی <code dir="ltr">ESP-IDF</code>

```powershell
idf.py --version
idf.py set-target esp32s3
idf.py reconfigure
```

## وابستگی تست <code dir="ltr">Python</code>

```powershell
py -3 -m pip install -r tools\requirements-test.txt
```

## تست هسته <code dir="ltr">Kotlin</code>

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_kotlin_protocol_tests.ps1
```

این تست به <code dir="ltr">Android SDK</code> نیاز ندارد، چون هسته مستقل <code dir="ltr">Protocol/Session</code> را بررسی می‌کند. کد وابسته به چارچوب <code dir="ltr">Android</code> در مسیر <code dir="ltr">android_reference/src/android/</code> باید در نرم‌افزار شهباز یا یک ماژول <code dir="ltr">Android</code> با <code dir="ltr">Android SDK</code> ساخته شود.

## پیش‌نیاز یکپارچه‌سازی <code dir="ltr">Android</code>

نرم‌افزار شهباز باید بتواند از <code dir="ltr">android.hardware.usb.UsbManager</code> استفاده کند، مجوز <code dir="ltr">USB</code> را از کاربر بگیرد، رویدادهای اتصال و قطع اتصال را مدیریت کند، <code dir="ltr">CDC bulk IN/OUT</code> را باز کند، داده را به <code dir="ltr">Shahbaz Protocol v2</code> بدهد و <code dir="ltr">QNH</code> را برای محاسبه ارتفاع نگهداری کند.

کد مرجع عملیاتی در <code dir="ltr">android_reference/src/android/kotlin/com/shahbaz/androidusb/</code> قرار دارد.

## نقش <code dir="ltr">Windows HIL</code>

<code dir="ltr">Windows HIL</code> مسیر مستقل عیب‌یابی است. اگر برد در <code dir="ltr">Windows HIL</code> هم شکست بخورد، ابتدا باید برد و <code dir="ltr">Firmware</code> بررسی شوند. اگر <code dir="ltr">Windows HIL</code> موفق باشد اما مسیر <code dir="ltr">Android</code> شکست بخورد، تمرکز باید روی مجوز <code dir="ltr">USB</code>، چرخه عمر اتصال و یکپارچه‌سازی نرم‌افزار شهباز باشد.

**مرحله بعد:** <code dir="ltr">04_BUILD_FLASH_AND_USB.fa.md</code>.
