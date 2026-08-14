# مرجع یکپارچه‌سازی <code dir="ltr">Android</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این پوشه مرز <code dir="ltr">Client</code> عملیاتی برای <code dir="ltr">shahbaz_interface_board</code> را تعریف می‌کند و برای یکپارچه‌سازی در نرم‌افزار واقعی شهباز در <code dir="ltr">Android</code> در نظر گرفته شده است.

- <code dir="ltr">src/main/.../ProvisionalProtocol.kt</code>: <code dir="ltr">Protocol v2 codec</code> محدود و ریاضیات داده <code dir="ltr">Sensor/QNH</code>.
- <code dir="ltr">src/main/.../ShahbazLinkSession.kt</code>: وضعیت <code dir="ltr">Session</code> هر اتصال فیزیکی، <code dir="ltr">TimeSync/Token</code>، ساخت <code dir="ltr">Heartbeat/Telemetry</code>، پاک‌سازی اتصال مجدد و ارتفاع مبتنی بر <code dir="ltr">QNH</code>.
- <code dir="ltr">src/android/.../ShahbazUsbCdcTransport.kt</code>: مسیر واقعی <code dir="ltr">android.hardware.usb</code> برای <code dir="ltr">CDC bulk</code>.
- <code dir="ltr">src/android/.../ShahbazUsbPermission.kt</code>: درخواست و نتیجه مجوز <code dir="ltr">Android USB</code>.
- <code dir="ltr">src/android/.../ShahbazInterfaceBoardClient.kt</code>: ترکیب سطح نرم‌افزار برای ایجاد <code dir="ltr">Session</code>، دریافت <code dir="ltr">Telemetry</code>، <code dir="ltr">Heartbeat</code>، <code dir="ltr">TimeSync</code> دوره‌ای، قطع اتصال و <code dir="ltr">QNH</code>.
- <code dir="ltr">AndroidManifest.integration.xml</code>: تعریف قابلیت <code dir="ltr">USB Host</code> که باید با فایل نرم‌افزار شهباز ادغام شود.

اسکریپت <code dir="ltr">tools/run_kotlin_protocol_tests.ps1</code> فقط کد مستقل از چارچوب را می‌سازد. فایل‌های وابسته به <code dir="ltr">Android</code> به <code dir="ltr">Android SDK</code> نیاز دارند و باید داخل نرم‌افزار شهباز یا یک ماژول <code dir="ltr">Android</code> ساخته شوند.

نرم‌افزار همچنان مسئول نمایش مجوز به کاربر، ثبت رویدادهای مجوز و قطع اتصال، انتخاب دستگاه هنگام وجود چند رابط <code dir="ltr">CDC</code> و نمایش یا ذخیره داده <code dir="ltr">Sensor/QNH</code> است. روش پذیرش فیزیکی در <code dir="ltr">doc/فارسی/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.fa.md</code> آمده است.
