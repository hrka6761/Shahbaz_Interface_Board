package com.shahbaz.androidusb

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build

/** Permission helper intentionally separated from the transport so the Shahbaz app owns UX/lifecycle. */
object ShahbazUsbPermission {
    private const val EXTRA_REQUESTED_DEVICE_ID =
        "com.shahbaz.androidusb.extra.REQUESTED_DEVICE_ID"
    private const val EXTRA_REQUESTED_DEVICE_NAME =
        "com.shahbaz.androidusb.extra.REQUESTED_DEVICE_NAME"

    fun action(context: Context): String = "${context.packageName}.SHAHBAZ_USB_PERMISSION"

    fun request(context: Context, device: UsbDevice) {
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val intent = Intent(action(context))
            .setPackage(context.packageName)
            .putExtra(EXTRA_REQUESTED_DEVICE_ID, device.deviceId)
            .putExtra(EXTRA_REQUESTED_DEVICE_NAME, device.deviceName)
        // UsbManager supplies EXTRA_DEVICE and EXTRA_PERMISSION_GRANTED with a fill-in Intent.
        // Android 12+ therefore requires this narrowly package-scoped PendingIntent to be mutable.
        val mutabilityFlag = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            PendingIntent.FLAG_MUTABLE
        } else {
            0
        }
        val pending = PendingIntent.getBroadcast(
            context,
            device.deviceId,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or mutabilityFlag,
        )
        manager.requestPermission(device, pending)
    }

    /**
     * Returns the requested device only when the callback belongs to that request and Android's
     * current UsbManager state confirms permission. Some Android builds omit one or both of the
     * standard result extras, so [requestedDevice] and [UsbManager.hasPermission] are the
     * authoritative fallback rather than leaving the application stuck waiting for extras.
     */
    fun grantedDevice(
        context: Context,
        intent: Intent,
        requestedDevice: UsbDevice,
    ): UsbDevice? {
        if (!isResultFor(context, intent, requestedDevice)) return null
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        // Current attachment/permission state is authoritative. In particular, an OEM-mangled
        // or stale EXTRA_PERMISSION_GRANTED=false must not override an actual current grant.
        val attachedDevice = manager.deviceList.values.firstOrNull {
            sameDevice(it, requestedDevice)
        } ?: return null
        return attachedDevice.takeIf(manager::hasPermission)
    }

    /** True only for the package-scoped callback created for [requestedDevice]. */
    fun isResultFor(context: Context, intent: Intent, requestedDevice: UsbDevice): Boolean {
        if (intent.action != action(context)) return false
        if (
            intent.getIntExtra(EXTRA_REQUESTED_DEVICE_ID, Int.MIN_VALUE) !=
            requestedDevice.deviceId
        ) {
            return false
        }
        if (intent.getStringExtra(EXTRA_REQUESTED_DEVICE_NAME) != requestedDevice.deviceName) {
            return false
        }

        @Suppress("DEPRECATION")
        val callbackDevice = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE) as? UsbDevice
        return callbackDevice == null || sameDevice(callbackDevice, requestedDevice)
    }

    private fun sameDevice(first: UsbDevice, second: UsbDevice): Boolean =
        first.deviceId == second.deviceId &&
            first.deviceName == second.deviceName &&
            first.vendorId == second.vendorId &&
            first.productId == second.productId
}
