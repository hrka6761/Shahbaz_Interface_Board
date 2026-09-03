# چهار <code dir="ltr">GY-530 / VL53L0X Rangefinder Module</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

مالک پروژه چهار برد متصل را <code dir="ltr">GY-530 Module</code> دارای حسگر <code dir="ltr">VL53L0X</code> معرفی کرده است. در حال حاضر عکس واضح جلو و پشت، نام سازنده، شماتیک یا رکورد اندازه‌گیری الکتریکی همان بردها در این پوشه وجود ندارد. بنابراین مستندات شرکت <code dir="ltr">STMicroelectronics</code> فقط برای خود <code dir="ltr">chip VL53L0X</code> مرجع قطعی است و <code dir="ltr">regulator</code>، <code dir="ltr">level shifting</code>، مقاومت‌های <code dir="ltr">pull-up</code>، ترتیب <code dir="ltr">Pin</code>ها و محدوده مجاز <code dir="ltr">VCC</code> روی <code dir="ltr">breakout</code> هنوز تأیید نشده‌اند.

## سیم‌بندی موردنیاز پروژه

هر چهار <code dir="ltr">Module</code> از خط مشترک و سازگار با منطق 3.3 ولت استفاده می‌کنند:

```text
all SDA -> ESP32-S3 GPIO8
all SCL -> ESP32-S3 GPIO9
all GND -> common ground
```

قبل از اتصال <code dir="ltr">VCC</code>، ورودی تغذیه همان <code dir="ltr">Module</code> واقعی را بررسی کنید. ادعای یک فروشنده درباره <code dir="ltr">GY-530</code> عمومی را به این بردها تعمیم ندهید و هرگز منطق 5 ولت را به <code dir="ltr">ESP32-S3 GPIO</code> وارد نکنید.

هر <code dir="ltr">Module</code> باید سیگنال فعال‌پایین <code dir="ltr">XSHUT</code> مربوط به <code dir="ltr">VL53L0X</code> را در دسترس قرار دهد و به یک <code dir="ltr">GPIO</code> جداگانه و بازبینی‌شده متصل شود:

| <code dir="ltr">instance</code> ثابت | نقش مونتاژ | <code dir="ltr">XSHUT GPIO</code> پیشنهادی | آدرس زمان اجرا |
|---:|---|---:|---:|
| <code dir="ltr">0</code> | پایین/زمین | <code dir="ltr">GPIO12</code> | <code dir="ltr">0x30</code> |
| <code dir="ltr">1</code> | بالا | <code dir="ltr">GPIO13</code> | <code dir="ltr">0x31</code> |
| <code dir="ltr">2</code> | جلوی چپ | <code dir="ltr">GPIO14</code> | <code dir="ltr">0x32</code> |
| <code dir="ltr">3</code> | جلوی راست | <code dir="ltr">GPIO15</code> | <code dir="ltr">0x33</code> |

مقدارهای <code dir="ltr">GPIO12..15</code> فقط پیشنهاد <code dir="ltr">Firmware</code> هستند و اتصال یا ایمنی مسیر واقعی روی پرنده را اثبات نمی‌کنند. پیش از فعال‌سازی باید پیوستگی، قطبیت، اتصال کوتاه، تداخل و نگاشت دقیق حسگر فیزیکی به نقش ثبت شود. کد فعلی پایان اندازه‌گیری را با خواندن دوره‌ای بررسی می‌کند؛ بنابراین خروجی وقفه یا <code dir="ltr">GPIO1</code> روی <code dir="ltr">breakout</code> در پیاده‌سازی فعلی لازم نیست.

## علت نیاز به چهار خط <code dir="ltr">XSHUT</code>

هر چهار <code dir="ltr">VL53L0X</code> با آدرس هفت‌بیتی <code dir="ltr">0x29</code> روشن می‌شوند و قطعات موازی در این آدرس را نمی‌توان جداگانه خطاب کرد. <code dir="ltr">Firmware</code> ابتدا همه خطوط <code dir="ltr">XSHUT</code> را پایین نگه می‌دارد و سپس هر <code dir="ltr">Module</code> را جداگانه آزاد می‌کند تا یک آدرس موقت یکتا به آن بدهد. این آدرس‌ها پس از بازنشانی از بین می‌روند و در هر روشن‌شدن دوباره ساخته می‌شوند.

استفاده از <code dir="ltr">I2C multiplexer</code> از نظر معماری سخت‌افزار ممکن است، اما این مخزن اکنون چنین مسیری را پیاده‌سازی نکرده است. بدون ساخت و تست بخش سازگار، کد فعلی مجموعه را برای سخت‌افزار دارای <code dir="ltr">multiplexer</code> فعال نکنید.

## محدودیت‌های مونتاژ و استفاده در پرواز

- قطعه پایین باید مسیر نوری بدون مانع در کنار بدنه و پایه فرود داشته باشد. فاصله گزارش‌شده در امتداد محور نوری است و نرم‌افزار <code dir="ltr">Android</code> پیش از استفاده نزدیک زمین، تصویر زاویه و سایر بررسی‌های اعتبار را اعمال می‌کند.
- قطعه بالا و دو قطعه جلو در حال حاضر فقط منبع <code dir="ltr">Telemetry</code> هستند. <code dir="ltr">Firmware</code> از آن‌ها اجتناب از مانع انجام نمی‌دهد.
- پروژه فقط بازه <code dir="ltr">30..2000 mm</code> را برای کنترل مجاز می‌داند. این سیاست نرم‌افزار تضمین نمی‌کند که هر سطح، نور، پوشش یا هندسه مونتاژ در فاصله دو متر نتیجه معتبر بدهد.
- چهار قطعه به‌صورت همکاری‌کننده و غیرهم‌زمان نمونه‌برداری می‌شوند، اما تداخل نوری، نور محیط، بازتاب هدف، میدان دید، لرزش، آلودگی و اثر پنجره محافظ همچنان به تست روی پرنده نیاز دارد.
- یک نمونه فاصله به‌تنهایی نباید تماس با زمین یا خلع سلاح را آغاز کند. کمک‌فرود <code dir="ltr">Android</code> باید نشانه مستقل فرود را نیز الزام کند.

## شواهد موردنیاز باقی‌مانده

- عکس واضح جلو و پشت و نوشته‌های خوانا برای هر چهار برد <code dir="ltr">GY-530</code> واقعی.
- نگاشت تأییدشده <code dir="ltr">VCC</code>، <code dir="ltr">GND</code>، <code dir="ltr">SDA</code>، <code dir="ltr">SCL</code>، <code dir="ltr">XSHUT</code> و در صورت وجود <code dir="ltr">GPIO1</code> در همان نسخه برد.
- اندازه‌گیری ولتاژ منطق در حالت بیکار، مقاومت مؤثر <code dir="ltr">pull-up</code>، زمان خیز و عملکرد خط با هر شش <code dir="ltr">I2C Module</code> متصل.
- رکورد پیوستگی هر خط <code dir="ltr">XSHUT</code> با <code dir="ltr">GPIO12/13/14/15</code> و نقش فیزیکی موردنظر.
- شاهد روشن‌شدن و بازنشانی که تخصیص دقیق <code dir="ltr">0x30</code>، <code dir="ltr">0x31</code>، <code dir="ltr">0x32</code> و <code dir="ltr">0x33</code> را بدون باقی‌ماندن قطعه در <code dir="ltr">0x29</code> نشان دهد.
- تست هدف ثابت، جنس سطح، نور خورشید، زاویه، لرزش، انسداد، تداخل نوری، داده کهنه، قطع اتصال و خرابی یک قطعه پیش از هر تست پیشرانش.

تا زمان تکمیل این شواهد، مقدار <code dir="ltr">CONFIG_SHAHBAZ_VL53L0X_ENABLE=n</code> را حفظ کنید.

## منابع اصلی

- [برگه مشخصات رسمی شرکت سازنده](https://www.st.com/resource/en/datasheet/vl53l0x.pdf)
- [یادداشت رسمی استفاده از چند قطعه در یک طراحی](https://www.st.com/resource/en/application_note/an4846-using-multiple-vl53l0x-in-a-single-design-stmicroelectronics.pdf)
- [راهنمای رسمی رابط برنامه‌نویسی](https://www.st.com/resource/en/user_manual/um2039-world-smallest-timeofflight-ranging-and-gesture-detection-sensor-application-programming-interface-stmicroelectronics.pdf)
- [راهنمای یکپارچه‌سازی فاصله‌سنج در پروژه مرجع](https://docs.px4.io/main/en/sensor/rangefinders)

این منابع خود حسگر و اصول یکپارچه‌سازی را توضیح می‌دهند و هویت یا صحت یک <code dir="ltr">GY-530 breakout</code> عمومی را اثبات نمی‌کنند.
