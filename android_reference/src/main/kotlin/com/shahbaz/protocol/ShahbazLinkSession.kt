package com.shahbaz.protocol

/**
 * Stateful host-side session coordinator for the operational Shahbaz Android link.
 *
 * This class has no Android framework dependency so its session/reset/freshness-facing
 * behavior can be unit-tested on the host. One instance represents one physical USB
 * attachment. Call [onUsbAttached] and [onUsbDetached] at the real USB lifecycle edges.
 */
class ShahbazLinkSession(
    private val monotonicUs: () -> ULong,
    initialQnhHpa: Double = 1013.25,
) {
    companion object {
        const val TIME_SYNC_REFRESH_US: ULong = 30_000_000uL
    }

    sealed class Event {
        data class SessionReady(val token: ULong) : Event()
        data class FrameReceived(val frame: DecodedFrame) : Event()
        data class Sht30(val reading: Sht30Reading) : Event()
        data class Ms5611(
            val reading: Ms5611Reading,
            val barometricAltitudeMeters: Double,
        ) : Event()
        data class ProtocolRejected(val reason: String) : Event()
    }

    private val accumulator = FrameAccumulator()
    private var nextSequence: UInt = 1u
    private var pendingTimeSyncClientUs: ULong? = null
    private var lastSuccessfulTimeSyncHostUs: ULong? = null

    var connected: Boolean = false
        private set

    var sessionToken: ULong? = null
        private set

    var qnhHpa: Double = initialQnhHpa
        set(value) {
            if (!value.isFinite() || value !in 800.0..1100.0) {
                throw ProtocolException("QNH must be in 800..1100 hPa")
            }
            field = value
        }

    init {
        qnhHpa = initialQnhHpa
    }

    fun onUsbAttached() {
        resetSessionState()
        connected = true
    }

    fun onUsbDetached() {
        resetSessionState()
        connected = false
    }

    fun buildTimeSync(): ByteArray {
        requireConnected()
        val now = monotonicUs()
        pendingTimeSyncClientUs = now
        return encode(SafeRequests.timeSync(now), sessionBound = false)
    }

    fun buildDeviceInfoRequest(): ByteArray =
        encodeConnected(SafeRequests.deviceInfo(), sessionBound = false)

    fun buildDeviceStatusRequest(): ByteArray =
        encodeConnected(SafeRequests.deviceStatus(), sessionBound = false)

    fun buildHeartbeat(): ByteArray =
        encodeConnected(SafeRequests.heartbeat(), sessionBound = true)

    fun buildStartTelemetry(): ByteArray =
        encodeConnected(SafeRequests.startTelemetry(), sessionBound = true)

    fun buildStopTelemetry(): ByteArray =
        encodeConnected(SafeRequests.stopTelemetry(), sessionBound = true)

    fun buildSetSensorRate(sensorId: Int, instanceId: Int, intervalUs: UInt): ByteArray =
        encodeConnected(
            SafeRequests.setSensorRate(sensorId, instanceId, intervalUs),
            sessionBound = true,
        )

    fun buildDisarm(): ByteArray =
        encodeConnected(SafeRequests.disarm(), sessionBound = false)

    fun buildEmergencyStop(): ByteArray =
        encodeConnected(SafeRequests.emergencyStop(), sessionBound = false)

    fun timeSyncRefreshDue(nowUs: ULong = monotonicUs()): Boolean {
        val last = lastSuccessfulTimeSyncHostUs ?: return true
        return nowUs >= last && nowUs - last >= TIME_SYNC_REFRESH_US
    }

    fun feedUsbBytes(bytes: ByteArray): List<Event> {
        if (!connected) return emptyList()
        val output = mutableListOf<Event>()
        for (streamEvent in accumulator.feed(bytes)) {
            when (streamEvent.kind) {
                StreamEventKind.FRAME_READY -> {
                    val frame = streamEvent.frame ?: continue
                    val sync = decodeTimeSyncResponse(frame)
                    if (sync != null) {
                        val pending = pendingTimeSyncClientUs
                        if (pending == null || sync.clientSendUs != pending || sync.sessionToken == 0uL) {
                            output += Event.ProtocolRejected("invalid or unsolicited TimeSyncResponse")
                            continue
                        }
                        sessionToken = sync.sessionToken
                        pendingTimeSyncClientUs = null
                        lastSuccessfulTimeSyncHostUs = monotonicUs()
                        output += Event.SessionReady(sync.sessionToken)
                        output += Event.FrameReceived(frame)
                        continue
                    }

                    val sample = decodeSensorSample(frame)
                    if (sample != null) {
                        try {
                            when (sample.sensorId) {
                                SensorId.SHT30 -> output += Event.Sht30(sample.asSht30Reading())
                                SensorId.MS5611 -> {
                                    val reading = sample.asMs5611Reading()
                                    val altitude = BarometricAltitude.metersFromPressure(
                                        reading.pressurePa,
                                        qnhHpa * 100.0,
                                    )
                                    output += Event.Ms5611(reading, altitude)
                                }
                            }
                        } catch (error: ProtocolException) {
                            output += Event.ProtocolRejected(error.message ?: "invalid sensor sample")
                        }
                    }
                    output += Event.FrameReceived(frame)
                }
                StreamEventKind.FRAME_REJECTED,
                StreamEventKind.OVERSIZE_DISCARDED ->
                    output += Event.ProtocolRejected(streamEvent.error ?: "invalid USB frame")
                StreamEventKind.EMPTY_DELIMITER -> Unit
            }
        }
        return output
    }

    private fun encodeConnected(request: OutboundRequest, sessionBound: Boolean): ByteArray {
        requireConnected()
        return encode(request, sessionBound)
    }

    private fun encode(request: OutboundRequest, sessionBound: Boolean): ByteArray {
        val sequence = nextSequence
        nextSequence += 1u
        return SafeOutboundCodec.encode(
            request = request,
            sequence = sequence,
            senderMonotonicUs = monotonicUs(),
            sessionToken = if (sessionBound) requireSessionToken() else null,
        )
    }

    private fun requireConnected() {
        if (!connected) throw OutboundPolicyException("USB is not attached")
    }

    private fun requireSessionToken(): ULong =
        sessionToken ?: throw OutboundPolicyException("TimeSync/session token is not established")

    private fun resetSessionState() {
        accumulator.reset()
        nextSequence = 1u
        pendingTimeSyncClientUs = null
        lastSuccessfulTimeSyncHostUs = null
        sessionToken = null
    }
}
