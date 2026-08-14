package com.shahbaz.androidusb

import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.os.SystemClock
import com.shahbaz.protocol.ShahbazLinkSession
import java.io.Closeable
import java.util.Timer
import java.util.TimerTask

/**
 * Application-facing Android client for shahbaz_interface_board.
 *
 * The Shahbaz app remains responsible for USB permission UX and for choosing the exact
 * device when more than one CDC device is attached. This class owns protocol session
 * establishment, heartbeat/time-sync maintenance, telemetry start, and QNH altitude.
 */
class ShahbazInterfaceBoardClient(
    private val context: Context,
    private val listener: Listener,
    qnhHpa: Double = 1013.25,
) : Closeable, ShahbazUsbCdcTransport.Listener {
    interface Listener {
        fun onSessionReady()
        fun onSht30(temperatureC: Double, relativeHumidityPercent: Double)
        fun onMs5611(pressurePa: Int, temperatureC: Double, barometricAltitudeMeters: Double)
        fun onDisconnected()
        fun onError(message: String, cause: Throwable? = null)
    }

    private val session = ShahbazLinkSession(
        monotonicUs = { SystemClock.elapsedRealtimeNanos().toULong() / 1000uL },
        initialQnhHpa = qnhHpa,
    )
    private val transport = ShahbazUsbCdcTransport(context, this)
    private val sessionLock = Any()
    private var maintenanceTimer: Timer? = null

    fun matchingDevices(): List<UsbDevice> = transport.matchingDevices()

    fun hasPermission(device: UsbDevice): Boolean = transport.hasPermission(device)

    fun requestPermission(device: UsbDevice) {
        ShahbazUsbPermission.request(context, device)
    }

    /** Pass the app's USB-permission broadcast here; opens only when permission was granted. */
    fun handlePermissionResult(intent: Intent): Boolean {
        val device = ShahbazUsbPermission.grantedDevice(context, intent) ?: return false
        return open(device)
    }

    /** Call after UsbManager permission is granted. */
    fun open(device: UsbDevice): Boolean {
        close()
        synchronized(sessionLock) { session.onUsbAttached() }
        if (!transport.open(device)) {
            synchronized(sessionLock) { session.onUsbDetached() }
            return false
        }
        val timeSync = synchronized(sessionLock) { session.buildTimeSync() }
        if (!transport.write(timeSync)) {
            close()
            return false
        }
        return true
    }

    fun setQnhHpa(qnhHpa: Double) {
        synchronized(sessionLock) { session.qnhHpa = qnhHpa }
    }

    /** Call from ACTION_USB_DEVICE_DETACHED. */
    fun handleDetached(device: UsbDevice) {
        transport.handleDetached(device)
    }

    override fun onUsbBytes(bytes: ByteArray) {
        val events = synchronized(sessionLock) { session.feedUsbBytes(bytes) }
        for (event in events) {
            when (event) {
                is ShahbazLinkSession.Event.SessionReady -> {
                    listener.onSessionReady()
                    val startupFrames = synchronized(sessionLock) {
                        listOf(
                            session.buildDeviceInfoRequest(),
                            session.buildStartTelemetry(),
                            session.buildHeartbeat(),
                        )
                    }
                    if (startupFrames.any { !transport.write(it) }) {
                        listener.onError("failed to start Shahbaz telemetry session")
                        close()
                        return
                    }
                    startMaintenance()
                }
                is ShahbazLinkSession.Event.Sht30 -> listener.onSht30(
                    event.reading.temperatureMilliCelsius / 1000.0,
                    event.reading.relativeHumidityMilliPercent.toDouble() / 1000.0,
                )
                is ShahbazLinkSession.Event.Ms5611 -> listener.onMs5611(
                    event.reading.pressurePa,
                    event.reading.temperatureMilliCelsius / 1000.0,
                    event.barometricAltitudeMeters,
                )
                is ShahbazLinkSession.Event.ProtocolRejected -> listener.onError(event.reason)
                is ShahbazLinkSession.Event.FrameReceived -> Unit
            }
        }
    }

    override fun onUsbDisconnected() {
        maintenanceTimer?.cancel()
        maintenanceTimer = null
        synchronized(sessionLock) { session.onUsbDetached() }
        listener.onDisconnected()
    }

    override fun onUsbError(message: String, cause: Throwable?) {
        listener.onError(message, cause)
    }

    override fun close() {
        maintenanceTimer?.cancel()
        maintenanceTimer = null
        try {
            val shutdownFrames = synchronized(sessionLock) {
                if (session.connected && session.sessionToken != null) {
                    listOf(session.buildStopTelemetry(), session.buildDisarm())
                } else {
                    emptyList()
                }
            }
            for (frame in shutdownFrames) transport.write(frame)
        } catch (_: RuntimeException) { }
        transport.close()
        synchronized(sessionLock) { session.onUsbDetached() }
    }

    private fun startMaintenance() {
        maintenanceTimer?.cancel()
        maintenanceTimer = Timer("shahbaz-link-maintenance", true).also { timer ->
            timer.scheduleAtFixedRate(object : TimerTask() {
                override fun run() {
                    try {
                        val frames = synchronized(sessionLock) {
                            if (!session.connected || session.sessionToken == null) return
                            buildList {
                                if (session.timeSyncRefreshDue()) add(session.buildTimeSync())
                                add(session.buildHeartbeat())
                            }
                        }
                        if (frames.any { !transport.write(it) }) {
                            listener.onError("Shahbaz link maintenance write failed")
                            close()
                        }
                    } catch (error: RuntimeException) {
                        listener.onError("Shahbaz link maintenance failed", error)
                        close()
                    }
                }
            }, 350L, 350L)
        }
    }
}
