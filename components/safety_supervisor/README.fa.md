# ناظر ایمنی (<code dir="ltr">Safety Supervisor</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> <code dir="ltr">State Machine</code> اصلی حالت ایمن برای لینک ارتباطی، <code dir="ltr">Heartbeat</code>، <code dir="ltr">Arming</code>، خطا و وضعیت <code dir="ltr">Actuator</code> را کنترل می‌کند. تا زمانی که شرایط ایمنی لازم برقرار نباشند، فرمان حرکتی اجازه فعال‌کردن خروجی‌ها را ندارد.

## فایل‌های اصلی

- <code dir="ltr">safety_types.hpp</code>: وضعیت‌ها، خطاها و واژگان ایمنی قابل مشاهده در <code dir="ltr">Protocol</code>.
- <code dir="ltr">actuator_controller.hpp</code>: <code dir="ltr">Interface</code> محدود <code dir="ltr">Actuator</code> که توسط منطق ایمنی کنترل می‌شود.
- <code dir="ltr">safety_supervisor_interface.hpp</code>: قرارداد حداقلی ناظر برای سایر <code dir="ltr">Component</code>‌ها.
- <code dir="ltr">safety_supervisor.hpp</code> / <code dir="ltr">src/safety_supervisor.cpp</code>: مدیریت لینک، <code dir="ltr">Session</code> و <code dir="ltr">Heartbeat</code>، <code dir="ltr">arm/disarm</code>، خطا و گذارهای حالت ایمن.
- <code dir="ltr">test/test_safety_supervisor.cpp</code>: راه‌اندازی اولیه، <code dir="ltr">Arming</code> موفق و ردشده، <code dir="ltr">Timeout</code> <code dir="ltr">Heartbeat</code>، قطع/وصل <code dir="ltr">USB</code>، خطا، ناهنجاری زمان و اجبار به حالت امن.
- <code dir="ltr">Kconfig</code>: تنظیمات <code dir="ltr">Scheduler</code>ی و ایمنی.

در عملکرد عادی، حالت <code dir="ltr">Armed</code> فقط وقتی قابل دسترسی است که پیش‌شرط‌های آن معتبر باشند. قطع لینک، قدیمی شدن <code dir="ltr">Heartbeat</code>، توقف اضطراری، خرابی <code dir="ltr">Actuator</code>، خرابی <code dir="ltr">Liveness</code> سرویس بحرانی یا خطای ثبت/<code dir="ltr">Feed</code> کردن <code dir="ltr">Watchdog</code> سیستم را به حالت امن یا <code dir="ltr">Fault</code> قفل‌شده می‌برد. بررسی زمان فرستنده و <code dir="ltr">Session Token v2</code> پیش از تازه‌شدن <code dir="ltr">heartbeat/control freshness</code> انجام می‌شود تا فرمان قدیمی یا بازپخش‌شده نتواند لینک را سالم نشان دهد.
