package com.shahbaz.androidusb

import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.os.SystemClock
import com.shahbaz.protocol.ShahbazLinkSession
import com.shahbaz.protocol.ValidatedDeviceInfo
import java.io.Closeable
import java.util.Timer
import java.util.TimerTask

/**
 * Application-facing Android client for shahbaz_interface_board.
 *
 * The Shahbaz app remains responsible for USB permission UX and for choosing the exact
 * device when more than one matching board is attached. This class owns one serialized,
 * generation-bound protocol session, validated DeviceInfo, heartbeat/telemetry startup,
 * maintenance, disconnect, and QNH altitude.
 */
class ShahbazInterfaceBoardClient(
    private val context: Context,
    private val listener: Listener,
    qnhHpa: Double = 1013.25,
) : Closeable, ShahbazUsbCdcTransport.Listener {
    companion object {
        private const val INITIAL_TIME_SYNC_MAX_ATTEMPTS = 4
        private const val INITIAL_TIME_SYNC_RETRY_MS = 500L
        private const val STARTUP_RESPONSE_TIMEOUT_MS = 2_000L
        private const val MAINTENANCE_INTERVAL_MS = 350L
    }

    interface Listener {
        /** Called once DeviceInfo has passed the sensor-only compatibility policy. */
        fun onDeviceInfoValidated(info: ValidatedDeviceInfo) { }

        /** Called only after DeviceInfo, HeartbeatAck, and StartTelemetry CommandAck all pass. */
        fun onSessionReady()
        fun onSht30(temperatureC: Double, relativeHumidityPercent: Double)
        fun onMs5611(pressurePa: Int, temperatureC: Double, barometricAltitudeMeters: Double)
        fun onDisconnected()
        fun onError(message: String, cause: Throwable? = null)
    }

    private data class GenerationTimer(
        val generation: Long,
        val timer: Timer,
    )

    private val session = ShahbazLinkSession(
        monotonicUs = { SystemClock.elapsedRealtimeNanos().toULong() / 1000uL },
        initialQnhHpa = qnhHpa,
    )
    private val transport = ShahbazUsbCdcTransport(context, this)
    private val lifecycleLock = Any()
    private val sessionLock = Any()

    private var nextGeneration: Long = 0L
    private var activeGeneration: Long? = null
    private var requestedPermissionDevice: UsbDevice? = null
    private var currentDeviceInfo: ValidatedDeviceInfo? = null
    private var initialTimeSyncTimer: GenerationTimer? = null
    private var startupResponseTimer: GenerationTimer? = null
    private var maintenanceTimer: GenerationTimer? = null
    private var initialTimeSyncAttempts: Int = 0

    /** The validated board facts, including retained advisory bits, for the current link. */
    val validatedDeviceInfo: ValidatedDeviceInfo?
        get() = synchronized(sessionLock) { currentDeviceInfo }

    /** Monotonically increasing identifier of the current logical DTR session, if open. */
    val sessionGeneration: Long?
        get() = synchronized(sessionLock) { activeGeneration }

    fun matchingDevices(): List<UsbDevice> = transport.matchingDevices()

    fun hasPermission(device: UsbDevice): Boolean = transport.hasPermission(device)

    fun requestPermission(device: UsbDevice) {
        synchronized(sessionLock) { requestedPermissionDevice = device }
        ShahbazUsbPermission.request(context, device)
    }

    /** Pass the app's USB-permission broadcast here; opens only when permission is current. */
    fun handlePermissionResult(intent: Intent): Boolean {
        val requested = synchronized(sessionLock) {
            requestedPermissionDevice
        } ?: return false
        if (!ShahbazUsbPermission.isResultFor(context, intent, requested)) return false
        val device = ShahbazUsbPermission.grantedDevice(context, intent, requested)
        synchronized(sessionLock) {
            if (requestedPermissionDevice == requested) requestedPermissionDevice = null
        }
        return device?.let(::open) ?: false
    }

    /** Call after UsbManager permission is granted. */
    fun open(device: UsbDevice): Boolean = synchronized(lifecycleLock) {
        closeActiveGenerationLocked(expectedGeneration = null, sendShutdown = true)

        val generation = synchronized(sessionLock) {
            if (nextGeneration == Long.MAX_VALUE) {
                throw IllegalStateException("Shahbaz session generation exhausted")
            }
            nextGeneration += 1L
            activeGeneration = nextGeneration
            currentDeviceInfo = null
            session.onUsbAttached()
            nextGeneration
        }

        if (!transport.open(device, generation)) {
            closeActiveGenerationLocked(generation, sendShutdown = false)
            return@synchronized false
        }

        val started = synchronized(sessionLock) {
            activeGeneration == generation && startInitialTimeSyncLocked(generation)
        }
        if (!started) {
            closeActiveGenerationLocked(generation, sendShutdown = false)
            return@synchronized false
        }
        true
    }

    fun setQnhHpa(qnhHpa: Double) {
        synchronized(sessionLock) { session.qnhHpa = qnhHpa }
    }

    /** Call from ACTION_USB_DEVICE_DETACHED. */
    fun handleDetached(device: UsbDevice) {
        synchronized(lifecycleLock) { transport.handleDetached(device) }
    }

    override fun onUsbBytes(generation: Long, bytes: ByteArray) {
        var fatalMessage: String? = null
        var fatalCause: Throwable? = null
        synchronized(sessionLock) {
            if (activeGeneration != generation) return
            val events = try {
                session.feedUsbBytes(bytes)
            } catch (error: RuntimeException) {
                fatalMessage = "Shahbaz protocol session failed while decoding USB data"
                fatalCause = error
                emptyList()
            }

            for (event in events) {
                if (activeGeneration != generation || fatalMessage != null) break
                when (event) {
                    is ShahbazLinkSession.Event.TimeSynchronized -> {
                        cancelInitialTimeSyncLocked()
                        val frames = listOf(
                            session.buildDeviceInfoRequest(),
                            session.buildHeartbeat(),
                        )
                        if (frames.any { !transport.write(generation, it) }) {
                            fatalMessage = "failed to send Shahbaz DeviceInfo/initial Heartbeat"
                        } else {
                            startStartupResponseTimeoutLocked(generation, "HeartbeatAck")
                        }
                    }
                    is ShahbazLinkSession.Event.DeviceInfoValidated -> {
                        currentDeviceInfo = event.info
                        listener.onDeviceInfoValidated(event.info)
                    }
                    ShahbazLinkSession.Event.HeartbeatAcknowledged -> {
                        val frame = session.buildStartTelemetry()
                        if (!transport.write(generation, frame)) {
                            fatalMessage = "failed to send Shahbaz StartTelemetry"
                        } else {
                            startStartupResponseTimeoutLocked(
                                generation,
                                "StartTelemetry CommandAck",
                            )
                        }
                    }
                    ShahbazLinkSession.Event.StartTelemetryAcknowledged -> {
                        // If DeviceInfo was delayed, keep the link gated with a fresh bounded wait.
                        startStartupResponseTimeoutLocked(generation, "DeviceInfoResponse")
                    }
                    is ShahbazLinkSession.Event.SessionReady -> {
                        currentDeviceInfo = event.deviceInfo
                        cancelStartupResponseTimeoutLocked()
                        startMaintenanceLocked(generation)
                        listener.onSessionReady()
                    }
                    is ShahbazLinkSession.Event.SessionRejected -> {
                        fatalMessage = event.reason
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

        fatalMessage?.let { failGeneration(generation, it, fatalCause) }
    }

    override fun onUsbDisconnected(generation: Long) {
        synchronized(lifecycleLock) {
            val accepted = synchronized(sessionLock) {
                if (activeGeneration != generation) {
                    false
                } else {
                    invalidateSessionLocked()
                    requestedPermissionDevice = null
                    true
                }
            }
            if (accepted) listener.onDisconnected()
        }
    }

    override fun onUsbError(generation: Long, message: String, cause: Throwable?) {
        synchronized(sessionLock) {
            if (activeGeneration == generation) listener.onError(message, cause)
        }
    }

    override fun close() {
        synchronized(lifecycleLock) {
            synchronized(sessionLock) { requestedPermissionDevice = null }
            closeActiveGenerationLocked(expectedGeneration = null, sendShutdown = true)
        }
    }

    /** lifecycleLock must be held. */
    private fun closeActiveGenerationLocked(
        expectedGeneration: Long?,
        sendShutdown: Boolean,
    ): Boolean {
        val closure = synchronized(sessionLock) {
            val generation = activeGeneration
            if (generation == null || (expectedGeneration != null && generation != expectedGeneration)) {
                null
            } else {
                val frames = if (sendShutdown && session.connected && session.sessionToken != null) {
                    try {
                        listOf(session.buildStopTelemetry(), session.buildDisarm())
                    } catch (_: RuntimeException) {
                        emptyList()
                    }
                } else {
                    emptyList()
                }
                invalidateSessionLocked()
                generation to frames
            }
        }

        if (closure == null) {
            if (expectedGeneration == null) transport.close()
            return false
        }
        val (generation, frames) = closure
        for (frame in frames) transport.write(generation, frame)
        transport.close()
        return true
    }

    /** sessionLock must be held. */
    private fun invalidateSessionLocked() {
        cancelInitialTimeSyncLocked()
        cancelStartupResponseTimeoutLocked()
        cancelMaintenanceLocked()
        activeGeneration = null
        currentDeviceInfo = null
        initialTimeSyncAttempts = 0
        session.onUsbDetached()
    }

    /** sessionLock must be held. */
    private fun startInitialTimeSyncLocked(generation: Long): Boolean {
        if (activeGeneration != generation) return false
        initialTimeSyncAttempts = 1
        val firstFrame = session.buildTimeSync()
        if (!transport.write(generation, firstFrame)) return false

        val timer = Timer("shahbaz-initial-time-sync-$generation", true)
        val handle = GenerationTimer(generation, timer)
        initialTimeSyncTimer?.timer?.cancel()
        initialTimeSyncTimer = handle
        timer.scheduleAtFixedRate(object : TimerTask() {
            override fun run() {
                var failure: String? = null
                var failureCause: Throwable? = null
                synchronized(sessionLock) {
                    if (
                        activeGeneration != generation ||
                        initialTimeSyncTimer !== handle ||
                        session.sessionToken != null
                    ) {
                        timer.cancel()
                        return
                    }
                    try {
                        if (initialTimeSyncAttempts >= INITIAL_TIME_SYNC_MAX_ATTEMPTS) {
                            initialTimeSyncTimer = null
                            timer.cancel()
                            failure =
                                "initial Shahbaz TimeSync timed out after " +
                                    "$INITIAL_TIME_SYNC_MAX_ATTEMPTS attempts"
                        } else {
                            initialTimeSyncAttempts += 1
                            if (!transport.write(generation, session.buildTimeSync())) {
                                failure = "initial Shahbaz TimeSync retry write failed"
                            }
                        }
                    } catch (error: RuntimeException) {
                        failure = "initial Shahbaz TimeSync retry failed"
                        failureCause = error
                    }
                }
                failure?.let { failGeneration(generation, it, failureCause) }
            }
        }, INITIAL_TIME_SYNC_RETRY_MS, INITIAL_TIME_SYNC_RETRY_MS)
        return true
    }

    /** sessionLock must be held. */
    private fun startStartupResponseTimeoutLocked(generation: Long, expected: String) {
        cancelStartupResponseTimeoutLocked()
        val timer = Timer("shahbaz-startup-response-$generation", true)
        val handle = GenerationTimer(generation, timer)
        startupResponseTimer = handle
        timer.schedule(object : TimerTask() {
            override fun run() {
                val expired = synchronized(sessionLock) {
                    if (activeGeneration == generation && startupResponseTimer === handle) {
                        startupResponseTimer = null
                        true
                    } else {
                        false
                    }
                }
                if (expired) {
                    failGeneration(generation, "Shahbaz startup timed out waiting for $expected")
                }
            }
        }, STARTUP_RESPONSE_TIMEOUT_MS)
    }

    /** sessionLock must be held. */
    private fun startMaintenanceLocked(generation: Long) {
        cancelMaintenanceLocked()
        val timer = Timer("shahbaz-link-maintenance-$generation", true)
        val handle = GenerationTimer(generation, timer)
        maintenanceTimer = handle
        timer.scheduleAtFixedRate(object : TimerTask() {
            override fun run() {
                var failure: String? = null
                var failureCause: Throwable? = null
                synchronized(sessionLock) {
                    if (
                        activeGeneration != generation ||
                        maintenanceTimer !== handle ||
                        !session.connected ||
                        session.sessionToken == null
                    ) {
                        timer.cancel()
                        return
                    }
                    try {
                        val frames = buildList {
                            if (session.timeSyncRefreshDue()) add(session.buildTimeSync())
                            add(session.buildHeartbeat())
                        }
                        if (frames.any { !transport.write(generation, it) }) {
                            failure = "Shahbaz link maintenance write failed"
                        }
                    } catch (error: RuntimeException) {
                        failure = "Shahbaz link maintenance failed"
                        failureCause = error
                    }
                }
                failure?.let { failGeneration(generation, it, failureCause) }
            }
        }, MAINTENANCE_INTERVAL_MS, MAINTENANCE_INTERVAL_MS)
    }

    private fun failGeneration(generation: Long, message: String, cause: Throwable? = null) {
        synchronized(lifecycleLock) {
            val current = synchronized(sessionLock) { activeGeneration == generation }
            if (!current) return
            listener.onError(message, cause)
            closeActiveGenerationLocked(generation, sendShutdown = true)
        }
    }

    /** sessionLock must be held. */
    private fun cancelInitialTimeSyncLocked() {
        initialTimeSyncTimer?.timer?.cancel()
        initialTimeSyncTimer = null
        initialTimeSyncAttempts = 0
    }

    /** sessionLock must be held. */
    private fun cancelStartupResponseTimeoutLocked() {
        startupResponseTimer?.timer?.cancel()
        startupResponseTimer = null
    }

    /** sessionLock must be held. */
    private fun cancelMaintenanceLocked() {
        maintenanceTimer?.timer?.cancel()
        maintenanceTimer = null
    }
}
