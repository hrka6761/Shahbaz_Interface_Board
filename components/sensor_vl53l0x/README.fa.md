# بخش چهار <code dir="ltr">VL53L0X</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این بخش شامل کد مستقل از سخت‌افزار و بدون تخصیص پویای حافظه برای چهار حسگر فاصله <code dir="ltr">VL53L0X</code> است. این کد با <code dir="ltr">SharedSensorScheduler</code> یکپارچه شده، اما تا زمان بازبینی و ثبت سیم‌بندی واقعی حسگرها و خطوط <code dir="ltr">XSHUT</code>، در ترکیب عملیاتی غیرفعال می‌ماند.

## قرارداد ثابت نقش و <code dir="ltr">instance</code>

| <code dir="ltr">instance</code> | نقش | آدرس هفت‌بیتی زمان اجرا | کاربرد موردنظر |
|---:|---|---:|---|
| <code dir="ltr">0</code> | پایین/زمین | <code dir="ltr">0x30</code> | فاصله از زمین؛ ورودی مجاز برای کمک به فرود در نرم‌افزار <code dir="ltr">Android</code> در فاصله کمتر از حدود دو متر |
| <code dir="ltr">1</code> | بالا | <code dir="ltr">0x31</code> | فاصله مانع بالای پرنده؛ در حال حاضر فقط <code dir="ltr">Telemetry</code> |
| <code dir="ltr">2</code> | جلوی چپ | <code dir="ltr">0x32</code> | فاصله مانع جلوی چپ؛ در حال حاضر فقط <code dir="ltr">Telemetry</code> |
| <code dir="ltr">3</code> | جلوی راست | <code dir="ltr">0x33</code> | فاصله مانع جلوی راست؛ در حال حاضر فقط <code dir="ltr">Telemetry</code> |

این شناسه‌ها قرارداد <code dir="ltr">Protocol</code> هستند و بر اساس ترتیب کشف تعیین نمی‌شوند. تغییر آن‌ها به تغییر هماهنگ <code dir="ltr">Firmware</code>، کد رمزگشایی <code dir="ltr">Android</code>، داشبورد و تست‌ها نیاز دارد.

هر <code dir="ltr">VL53L0X</code> با آدرس هفت‌بیتی <code dir="ltr">0x29</code> روشن می‌شود. بنابراین چهار قطعه به چهار خط مستقل و فعال‌پایین <code dir="ltr">XSHUT</code> یا یک <code dir="ltr">I2C multiplexer</code> جداگانه نیاز دارند. پیاده‌سازی فعلی روش <code dir="ltr">XSHUT</code> را پشتیبانی می‌کند: ابتدا هر چهار قطعه در خاموشی نگه داشته می‌شوند، سپس هر قطعه جداگانه آزاد می‌شود، شناسه مدل <code dir="ltr">0xEEAA</code> بررسی می‌شود، یکی از آدرس‌های <code dir="ltr">0x30</code> تا <code dir="ltr">0x33</code> به آن داده و دوباره بررسی می‌شود و بعد پیکربندی انجام می‌گیرد. اتصال فقط <code dir="ltr">SDA</code> و <code dir="ltr">SCL</code> به هر چهار قطعه کافی نیست.

خط مشترک پروژه <code dir="ltr">SDA=GPIO8</code> و <code dir="ltr">SCL=GPIO9</code> است. نگاشت پیشنهادی و هنوز تأییدنشده <code dir="ltr">XSHUT</code> عبارت است از پایین <code dir="ltr">GPIO12</code>، بالا <code dir="ltr">GPIO13</code>، جلوی چپ <code dir="ltr">GPIO14</code> و جلوی راست <code dir="ltr">GPIO15</code>. این خطوط باید یکتا باشند و با خطوط <code dir="ltr">I2C</code>، <code dir="ltr">USB</code>، <code dir="ltr">Actuator</code>، حافظه، عیب‌یابی، خطوط حساس به <code dir="ltr">strapping</code> یا منابع رزروشده برد هم‌پوشانی نداشته باشند.

## فعال‌سازی بسته در حالت خطا

مقدار پیش‌فرض <code dir="ltr">CONFIG_SHAHBAZ_VL53L0X_ENABLE=n</code> است. فعال‌سازی مجموعه همچنین به موارد زیر نیاز دارد:

- <code dir="ltr">CONFIG_SHAHBAZ_SENSOR_HARDWARE_VERIFIED=y</code> و مقدار غیرخالی <code dir="ltr">CONFIG_SHAHBAZ_SENSOR_EVIDENCE_RECORD_ID</code>؛
- <code dir="ltr">CONFIG_SHAHBAZ_VL53L0X_XSHUT_PINS_PHYSICALLY_REVIEWED=y</code> و مقدار غیرخالی <code dir="ltr">CONFIG_SHAHBAZ_VL53L0X_XSHUT_EVIDENCE_RECORD_ID</code>؛
- چهار <code dir="ltr">GPIO</code> معتبر و یکتای <code dir="ltr">XSHUT</code> بدون تداخل با <code dir="ltr">Actuator</code> فعال؛
- راه‌اندازی موفق بخش <code dir="ltr">GPIO XSHUT</code> با رفتار بسته در حالت خطا.

اگر هر شرط برقرار نباشد، هر چهار فاصله‌سنج غیرقابل‌استفاده می‌مانند و نمونه ساختگی تولید نمی‌شود. پاسخ توسعه‌یافتهٔ <code dir="ltr">DeviceStatusResponse</code> وضعیت هر نقش ثابت را به‌صورت غیرفعال/نامعلوم، در حال راه‌اندازی، زنده یا خراب گزارش می‌کند؛ بااین‌حال <code dir="ltr">Client</code> باید <code dir="ltr">Telemetry</code> غایب یا کهنهٔ هر <code dir="ltr">instance</code> را برای کنترل غیرقابل‌استفاده بداند. <code dir="ltr">Protocol</code> شمارنده‌های داخلی <code dir="ltr">DriverError</code> را ارسال نمی‌کند.

## رفتار <code dir="ltr">Scheduler</code>

کد به‌صورت همکاری‌کننده اجرا می‌شود و هر فراخوانی <code dir="ltr">step()</code> حداکثر یک انتقال محدود <code dir="ltr">I2C</code>، یک اقدام محدود بازیابی یا یک تغییر <code dir="ltr">XSHUT</code> انجام می‌دهد. این مسیر توقف انتظاری، چرخش بی‌پایان یا تخصیص پویای حافظه ندارد. هر قطعه وضعیت مستقل توالی، سلامت، تلاش دوباره و بازیابی دارد و نمونه‌برداری <code dir="ltr">round-robin</code> مانع محروم‌شدن قطعات سالم توسط یک قطعه خراب می‌شود. بازیابی خط مشترک همه استفاده‌کنندگان <code dir="ltr">I2C</code> را نامعتبر می‌کند تا پس از آرام‌شدن خط دوباره بررسی شوند.

فاصله پیش‌فرض هر نمونه برای هر قطعه <code dir="ltr">100000 us</code> است. فرمان <code dir="ltr">SetSensorRate</code> در <code dir="ltr">Protocol v2</code> شناسه حسگر <code dir="ltr">3</code>، مقدارهای <code dir="ltr">instance=0..3</code> و فاصله <code dir="ltr">60000..10000000 us</code> را می‌پذیرد. هر چهار قطعه عمداً یک آهنگ مشترک دارند؛ <code dir="ltr">instance</code> هدف درخواست‌کننده را مشخص می‌کند، اما <code dir="ltr">Scheduler</code> نامتقارن ایجاد نمی‌کند.

## قرارداد <code dir="ltr">SensorSample</code>

نمونه‌های <code dir="ltr">VL53L0X</code> از شناسه حسگر <code dir="ltr">3</code> و چهار فیلد بدون علامت 32 بیتی استفاده می‌کنند:

| شناسه فیلد | معنا |
|---:|---|
| <code dir="ltr">5</code> | فاصله بر حسب میلی‌متر |
| <code dir="ltr">6</code> | وضعیت خام چهار بیتی از <code dir="ltr">RESULT_RANGE_STATUS[6:3]</code> |
| <code dir="ltr">7</code> | کیفیت مجازبودن برای کنترل در شهباز؛ در صورت پذیرش وضعیت و فاصله برابر <code dir="ltr">100</code> و در غیر این صورت <code dir="ltr">0</code> |
| <code dir="ltr">8</code> | عدم قطعیت زمان اندازه‌گیری به میکروثانیه؛ <code dir="ltr">UINT32_MAX</code> یعنی نامعلوم یا غیرقابل نمایش |

مُهر زمانی، میانهٔ بازهٔ فرمان شروع تبدیل تا پایان خواندن نتیجه است. فیلد 8 نیم‌عرض همین بازه با گردکردن رو به بالا است. تأخیر انتشار این مقادیر را تغییر نمی‌دهد ولی تأخیر خواندن، عدم قطعیت را افزایش می‌دهد. فاصلهٔ درخواستی از شروع تا شروع است؛ نرخ واقعی به اندازه‌گیری ترتیبی و بار مشترک بستگی دارد. [زمان و نمونه‌برداری](../sensor_scheduler/README.fa.md) را ببینید.

فیلد <code dir="ltr">7</code> در حال حاضر یک مقدار دودویی محافظه‌کارانه برای سیاست نرم‌افزار است و درصد اندازه‌گیری‌شده قدرت سیگنال نوری نیست.

وضعیت خام <code dir="ltr">0</code> و <code dir="ltr">11</code> پذیرفته می‌شود. رمزگشایی نام‌دار همچنین <code dir="ltr">1=sigma failure</code>، <code dir="ltr">2=signal failure</code>، <code dir="ltr">3=minimum-range failure</code>، <code dir="ltr">4=phase failure</code> و <code dir="ltr">5=hardware failure</code> را می‌شناسد و بقیه مقدارها ناشناخته‌اند. فاصله فقط در بازه بسته <code dir="ltr">30..2000 mm</code> و با وضعیت <code dir="ltr">0</code> یا <code dir="ltr">11</code> برای کنترل مجاز است.

برای هر نتیجه‌ای که با موفقیت خوانده شود، پرچم‌های <code dir="ltr">TransportValid</code>، <code dir="ltr">CalibrationValid</code> و <code dir="ltr">TimingValid</code> تنظیم می‌شوند. <code dir="ltr">PlausibilityValid</code> فقط برای نتیجه مجاز جهت کنترل افزوده می‌شود. هر نمونه جدید <code dir="ltr">Fresh</code> است و نخستین نمونه موفق بعد از بازیابی با <code dir="ltr">RecoveredAfterError</code> مشخص می‌شود. بیت صفر سلامت یعنی وضعیت فاصله نامعتبر و بیت یک یعنی فاصله بیرون از بازه کنترل <code dir="ltr">30..2000 mm</code>. فاصله و وضعیت خام برای عیب‌یابی همچنان ارسال می‌شوند، اما مصرف‌کننده نباید نمونه‌ای را که همه اعتبارهای لازم را ندارد در کنترل پرواز استفاده کند.

حسگر پایین فقط منبع مشاهده است. <code dir="ltr">Firmware</code> برد درباره تماس با زمین یا خلع سلاح تصمیم نمی‌گیرد. نرم‌افزار <code dir="ltr">Android</code> باید داده تازه و معتبر پایین را با جبران زاویه، بررسی پیوستگی، توافق تخمین‌گر، سرعت عمودی و نشانه مستقل فرود ترکیب کند.

## راستی‌آزمایی

تست‌های مستقل از سخت‌افزار، رمزگشایی <code dir="ltr">Domain</code>، همه وضعیت‌های خام، مرزهای فاصله، انتخاب <code dir="ltr">SPAD</code>، اعتبار آدرس، تخصیص آدرس چهار قطعه، خطاهای <code dir="ltr">XSHUT</code>، پایان زمان راه‌اندازی و اندازه‌گیری، بازیابی <code dir="ltr">I2C</code>، فاصله افزایشی تلاش دوباره، پس‌فشار انتشار، نامعتبرشدن خط مشترک و عدالت <code dir="ltr">round-robin</code> را پوشش می‌دهند. این تست‌ها عملکرد نوری، جهت فیزیکی حسگرها، صحت سیم‌بندی، کیفیت سیگنال <code dir="ltr">I2C</code>، میدان دید/انسداد یا ایمنی فرود روی سخت‌افزار واقعی را اثبات نمی‌کنند.

منابع اصلی:

- [برگه مشخصات رسمی شرکت سازنده](https://www.st.com/resource/en/datasheet/vl53l0x.pdf)
- [یادداشت رسمی استفاده از چند قطعه در یک طراحی](https://www.st.com/resource/en/application_note/an4846-using-multiple-vl53l0x-in-a-single-design-stmicroelectronics.pdf)
- [راهنمای رسمی رابط برنامه‌نویسی](https://www.st.com/resource/en/user_manual/um2039-world-smallest-timeofflight-ranging-and-gesture-detection-sensor-application-programming-interface-stmicroelectronics.pdf)
- [راهنمای یکپارچه‌سازی فاصله‌سنج در پروژه مرجع](https://docs.px4.io/main/en/sensor/rangefinders)

دنباله ثابت تنظیم رجیستر از کد <code dir="ltr">VL53L0X</code> دارای مجوز <code dir="ltr">BSD-3-Clause</code> در <code dir="ltr">PX4</code> برگرفته شده و متن انتساب در <code dir="ltr">LICENSE-PX4-BSD-3-Clause.txt</code> قرار دارد.
