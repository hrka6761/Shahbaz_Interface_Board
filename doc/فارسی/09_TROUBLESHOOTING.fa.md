# 09 — عیب‌یابی

**فارسی** | [نسخه انگلیسی](../English/09_TROUBLESHOOTING.en.md)

عیب‌یابی بر اساس معماری محصول انجام شود: **<code dir="ltr">Android + Shahbaz</code> مسیر عملیاتی است و <code dir="ltr">Windows HIL</code> ابزار جداسازی خطا است.**

## نرم‌افزار شهباز برد را پیدا نمی‌کند

- گوشی باید قابلیت <code dir="ltr">USB Host/OTG</code> داشته باشد.
- از رابط <code dir="ltr">Native USB</code> برد استفاده شود، نه رابط <code dir="ltr">USB-UART</code> توسعه.
- کابل یا مبدل باید انتقال داده را پشتیبانی کند.
- نرم‌افزار شهباز باید با <code dir="ltr">UsbManager</code> رابط <code dir="ltr">CDC</code> دارای <code dir="ltr">bulk IN/OUT</code> را ببیند.
- اگر <code dir="ltr">Android</code> مجوز <code dir="ltr">USB</code> می‌خواهد، قبل از <code dir="ltr">openDevice</code> باید مجوز دریافت شود.

اگر مسیر <code dir="ltr">Android</code> شکست خورد، تست سند 06 را اجرا کنید. اگر <code dir="ltr">Windows HIL</code> نیز شکست خورد، ابتدا برد و <code dir="ltr">Firmware</code> بررسی شوند. اگر <code dir="ltr">Windows HIL</code> موفق بود، تمرکز روی مجوز <code dir="ltr">USB</code>، انتخاب رابط، چرخه عمر و یکپارچه‌سازی نرم‌افزار شهباز باشد.

## مجوز <code dir="ltr">USB</code> رد می‌شود

نرم‌افزار باید در حالت قطع باقی بماند و انتقال دارای مجوز را در حلقه سریع تکرار نکند. رد مجوز خطای <code dir="ltr">Protocol</code> نیست.

## <code dir="ltr">USB</code> باز است اما <code dir="ltr">Telemetry</code> دیده نمی‌شود

به‌ترتیب بررسی کنید که <code dir="ltr">TimeSyncResponse</code> دریافت شده، زمان درخواست را بازتاب داده، <code dir="ltr">Session Token</code> غیرصفر است، <code dir="ltr">StartTelemetry</code> با همان <code dir="ltr">Token</code> ارسال شده و <code dir="ltr">Heartbeat</code> نیز از <code dir="ltr">Token</code> فعلی استفاده می‌کند.

## خطای <code dir="ltr">SessionMismatch</code>

<code dir="ltr">Token</code> اتصال قبلی هرگز دوباره استفاده نشود. هنگام قطع، وضعیت <code dir="ltr">Parser/Session</code> پاک شود و پس از اتصال مجدد <code dir="ltr">TimeSync</code> تازه انجام شود.

## خطای <code dir="ltr">StaleOrExpired</code>

برای زمان فرستنده از <code dir="ltr">SystemClock.elapsedRealtimeNanos</code> استفاده شود. <code dir="ltr">TimeSync</code> تازه با زمان فعلی انجام و در اتصال‌های طولانی دوره‌ای تکرار شود.

## اتصال مجدد بازیابی نمی‌شود

رویداد <code dir="ltr">ACTION_USB_DEVICE_DETACHED</code> باید مرز قطعی فیزیکی <code dir="ltr">Session</code> باشد. پس از در اختیار گرفتن رابط‌ها، نرم‌افزار باید <code dir="ltr">CDC DTR</code> را صریحاً غیرفعال و سپس فعال کند؛ هر بار بازشدن دوباره <code dir="ltr">DTR</code> حتی با کابل متصل یک <code dir="ltr">Session</code> منطقی تازه است. <code dir="ltr">UsbDeviceConnection</code> بسته شود، رابط‌ها آزاد شوند و <code dir="ltr">Accumulator</code>، شماره توالی، <code dir="ltr">Session Token</code> و وضعیت <code dir="ltr">TimeSync</code> پاک شوند. پس از اتصال جدید، مجوز لازم دوباره دریافت و <code dir="ltr">Session</code> تازه ساخته شود.

## فشار درست است اما ارتفاع اشتباه است

ابتدا فشار خام <code dir="ltr">MS5611</code> بررسی شود، سپس مقدار <code dir="ltr">QNH</code> **داخل نرم‌افزار شهباز** کنترل شود. مقدار <code dir="ltr">1013.25 hPa</code> فقط مرجع استاندارد است. تغییر <code dir="ltr">QNH</code> باید ارتفاع را تغییر دهد اما فشار خام را نه.

## <code dir="ltr">Windows HIL</code> شکست می‌خورد

از این ابزار فقط برای جداکردن لایه پایین استفاده کنید: <code dir="ltr">COM_USB</code> باید رابط <code dir="ltr">Native USB CDC</code> باشد، برنامه دیگری آن را باز نکرده باشد، تنظیم <code dir="ltr">Heartbeat Timeout</code> درست باشد و مسیر <code dir="ltr">GPIO19/20</code> و سیم‌کشی <code dir="ltr">Sensor</code>ها بررسی شود.

## برد <code dir="ltr">Reset</code> می‌شود یا <code dir="ltr">Sensor</code> قطع می‌شود

تغذیه و کابل <code dir="ltr">USB</code>، مسیر تأییدنشده تغذیه هم‌زمان، <code dir="ltr">3.3V</code>، زمین مشترک، <code dir="ltr">GPIO8 SDA</code>، <code dir="ltr">GPIO9 SCL</code>، آدرس <code dir="ltr">SHT30 0x44</code> و آدرس پروژه <code dir="ltr">MS5611 0x77</code> بررسی شوند.

برای محدودیت‌های الکتریکی به سندهای 10 و 14 مراجعه کنید.
