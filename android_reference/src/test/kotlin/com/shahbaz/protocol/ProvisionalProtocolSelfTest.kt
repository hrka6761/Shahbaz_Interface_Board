package com.shahbaz.protocol

/** Standalone stdlib-only checks; no Android or JUnit runtime is required. */
object ProvisionalProtocolSelfTest {
    @JvmStatic
    fun main(args: Array<String>) {
        check(args.isEmpty()) { "self-test does not accept arguments" }
        crcAndCobsVectors()
        sharedGoldenFrame()
        fragmentedAndOversizeStreams()
        requestPayloads()
        controlMessagesAreTypedAndEncodable()
        deviceToHostMessagesCannotBeEncoded()
        sensorTelemetryAndAltitude()
        operationalAndroidSessionLifecycle()
        println("Shahbaz Kotlin protocol v2 self-test passed")
    }

    private fun crcAndCobsVectors() {
        check(Crc32c.calculate("123456789".encodeToByteArray()).toUInt() == 0xE3069283u)
        check(Crc32c.calculate(byteArrayOf()) == 0)
        val cases = listOf(
            byteArrayOf(),
            byteArrayOf(0),
            byteArrayOf(0x11, 0, 0x22, 0, 0, 0x33),
            ByteArray(256) { it.toByte() },
        )
        for (case in cases) {
            check(Cobs.decode(Cobs.encode(case)).contentEquals(case))
        }
    }

    private fun sharedGoldenFrame() {
        val expected = hex(
            "04 02 16 42 01 01 02 01 0e 04 03 02 01 08 07 06 " +
                "05 04 03 02 01 08 02 aa 0b 55 01 02 03 04 05 6b 13 ee 0b 00",
        )
        val actual = SafeOutboundCodec.encode(
            OutboundRequest(
                MessageType.PING,
                MessagePriority.HIGH,
                byteArrayOf(0xAA.toByte(), 0, 0x55, 1, 2, 3, 4, 5),
            ),
            sequence = 0x01020304u,
            senderMonotonicUs = 0x0102030405060708uL,
        )
        check(actual.contentEquals(expected))
        val decoded = FrameDecoder.decodeDelimited(expected)
        check(decoded.header.messageType == MessageType.PING)
        check(decoded.header.sequence == 0x01020304u)
        check(decoded.header.senderMonotonicUs == 0x0102030405060708uL)
        check(decoded.payload.contentEquals(byteArrayOf(0xAA.toByte(), 0, 0x55, 1, 2, 3, 4, 5)))
    }

    private fun fragmentedAndOversizeStreams() {
        val frame = SafeOutboundCodec.encode(SafeRequests.deviceStatus(), 3u, 10uL)
        val accumulator = FrameAccumulator()
        check(accumulator.feed(frame.copyOfRange(0, 5)).isEmpty())
        val events = accumulator.feed(frame.copyOfRange(5, frame.size) + frame)
        check(
            events.map { it.kind } ==
                listOf(StreamEventKind.FRAME_READY, StreamEventKind.FRAME_READY),
        )

        accumulator.feed(
            ByteArray(ProvisionalWire.MAX_COBS_ENCODED_FRAME_LENGTH + 1) {
                0x11.toByte()
            },
        )
        check(accumulator.discardingOversizeFrame)
        val recovered = accumulator.feed(byteArrayOf(0) + frame)
        check(
            recovered.map { it.kind } ==
                listOf(StreamEventKind.OVERSIZE_DISCARDED, StreamEventKind.FRAME_READY),
        )
    }

    private fun requestPayloads() {
        check(SafeRequests.deviceInfo().payload.isEmpty())
        check(SafeRequests.deviceStatus().payload.isEmpty())
        check(SafeRequests.startTelemetry().payload.isEmpty())
        check(SafeRequests.stopTelemetry().payload.isEmpty())
        check(SafeRequests.heartbeat().payload.isEmpty())
        check(SafeRequests.disarm().payload.isEmpty())
        check(SafeRequests.emergencyStop().payload.isEmpty())
        check(
            SafeRequests.ping(0x0102030405060708uL).payload.contentEquals(
                hex("08 07 06 05 04 03 02 01"),
            ),
        )
        check(SafeRequests.timeSync(9uL).payload.contentEquals(hex("09 00 00 00 00 00 00 00")))
        check(
            SafeRequests.setSensorRate(2, 3, 40_000u).payload.contentEquals(
                hex("02 03 40 9c 00 00"),
            ),
        )
    }

    private fun controlMessagesAreTypedAndEncodable() {
        val requests = listOf(
            SafeRequests.arm(),
            SafeRequests.motor(0, 900),
            SafeRequests.servo(1, 1500),
            SafeRequests.actuator(1, 3, 2100),
            SafeRequests.controlMode(),
        )
        for ((index, request) in requests.withIndex()) {
            check(outboundPolicy(request.messageType) == MessagePolicy.SUPPORTED_CURRENT_PHASE)
            val sessionToken = 0x1122334455667788uL
            val wire = SafeOutboundCodec.encode(
                request, (100 + index).toUInt(), 500uL, sessionToken,
            )
            val payload = FrameDecoder.decodeDelimited(wire).payload
            check(payload.copyOfRange(0, 8).contentEquals(u64(sessionToken)))
            check(payload.copyOfRange(8, payload.size).contentEquals(request.payload))
        }
        var missingTokenRejected = false
        try {
            SafeOutboundCodec.encode(SafeRequests.arm(), 99u, 500uL)
        } catch (_: OutboundPolicyException) {
            missingTokenRejected = true
        }
        check(missingTokenRejected) { "session-bound control encoded without token" }
        check(SafeRequests.motor(0, 900).payload.contentEquals(hex("00 84 03")))
        check(SafeRequests.servo(1, 1500).payload.contentEquals(hex("01 dc 05")))
        check(SafeRequests.actuator(2, 1, 2500).payload.contentEquals(hex("02 01 c4 09")))
    }

    private fun deviceToHostMessagesCannotBeEncoded() {
        val receiveOnly = listOf(
            MessageType.DEVICE_INFO_RESPONSE, MessageType.SENSOR_SAMPLE,
            MessageType.DEVICE_STATUS_RESPONSE, MessageType.TIME_SYNC_RESPONSE,
            MessageType.COMMAND_ACK, MessageType.COMMAND_NACK,
        )
        for (type in receiveOnly) {
            check(outboundPolicy(type) == MessagePolicy.RECEIVE_ONLY)
            var rejected = false
            try {
                SafeOutboundCodec.encode(OutboundRequest(type, MessagePriority.NORMAL, byteArrayOf()), 0u, 0uL)
            } catch (_: OutboundPolicyException) {
                rejected = true
            }
            check(rejected) { "$type unexpectedly passed outbound policy" }
        }
    }

    private fun sensorTelemetryAndAltitude() {
        val shtPayload =
            byteArrayOf(1, 0) +
                u32(7u) + u64(99uL) + u32(0x1Bu) + u32(1u) + u32(0u) + byteArrayOf(2) +
                fieldSigned(1, -1250) + fieldUnsigned(2, 50_123u)
        val shtFrame = decodedSensorFrame(shtPayload)
        val sht = decodeSensorSample(shtFrame)!!.asSht30Reading()
        check(sht.temperatureMilliCelsius == -1250)
        check(sht.relativeHumidityMilliPercent == 50_123u)

        val msPayload =
            byteArrayOf(2, 0) +
                u32(8u) + u64(101uL) + u32(0x1Fu) + u32(1u) + u32(0u) + byteArrayOf(2) +
                fieldSigned(3, 101_325) + fieldSigned(4, 22_340)
        val ms = decodeSensorSample(decodedSensorFrame(msPayload))!!.asMs5611Reading()
        check(ms.pressurePa == 101_325)
        check(ms.temperatureMilliCelsius == 22_340)
        check(kotlin.math.abs(BarometricAltitude.metersFromPressure(101_325)) < 0.001)
        val approximatelyOneKm = BarometricAltitude.metersFromPressure(89_875)
        check(approximatelyOneKm in 995.0..1005.0) { "unexpected altitude $approximatelyOneKm" }

        var rejected = false
        try {
            val duplicate =
                byteArrayOf(1, 0) + u32(1u) + u64(1uL) + u32(0x1Bu) + u32(1u) + u32(0u) +
                    byteArrayOf(2) + fieldSigned(1, 1) + fieldSigned(1, 2)
            decodeSensorSample(decodedSensorFrame(duplicate))
        } catch (_: ProtocolException) {
            rejected = true
        }
        check(rejected) { "duplicate telemetry fields were accepted" }
    }


    private fun operationalAndroidSessionLifecycle() {
        var now = 1_000_000uL
        val session = ShahbazLinkSession(monotonicUs = { now })
        session.onUsbAttached()
        val syncRequest = session.buildTimeSync()
        check(FrameDecoder.decodeDelimited(syncRequest).header.messageType == MessageType.TIME_SYNC_REQUEST)

        var blocked = false
        try {
            session.buildStartTelemetry()
        } catch (_: OutboundPolicyException) {
            blocked = true
        }
        check(blocked) { "session-bound telemetry started before TimeSync" }

        val token = 0x8877665544332211uL
        val syncResponsePayload = u64(1_000_000uL) + u64(1_000_010uL) + u64(1_000_020uL) + u64(token)
        val response = deviceFrame(MessageType.TIME_SYNC_RESPONSE, 9u, 1_000_020uL, syncResponsePayload)
        val events = session.feedUsbBytes(response)
        check(events.any { it is ShahbazLinkSession.Event.SessionReady && it.token == token })
        check(session.sessionToken == token)
        check(FrameDecoder.decodeDelimited(session.buildStartTelemetry()).payload.copyOfRange(0, 8).contentEquals(u64(token)))

        session.qnhHpa = 1000.0
        now += ShahbazLinkSession.TIME_SYNC_REFRESH_US
        check(session.timeSyncRefreshDue(now))

        session.onUsbDetached()
        check(!session.connected)
        check(session.sessionToken == null)
        blocked = false
        try {
            session.buildHeartbeat()
        } catch (_: OutboundPolicyException) {
            blocked = true
        }
        check(blocked) { "detached Android session could still transmit heartbeat" }
    }

    private fun deviceFrame(
        type: MessageType,
        sequence: UInt,
        senderUs: ULong,
        payload: ByteArray,
    ): ByteArray {
        val body = ByteArray(ProvisionalWire.HEADER_LENGTH + payload.size)
        body[0] = ProvisionalWire.VERSION.toByte()
        body[1] = ProvisionalWire.HEADER_LENGTH.toByte()
        body[2] = (type.wireValue and 0xFF).toByte()
        body[3] = ((type.wireValue ushr 8) and 0xFF).toByte()
        body[4] = 0
        body[5] = 0
        body[6] = MessagePriority.HIGH.wireValue.toByte()
        body[7] = 0
        u32(sequence).copyInto(body, 8)
        u64(senderUs).copyInto(body, 12)
        body[20] = (payload.size and 0xFF).toByte()
        body[21] = ((payload.size ushr 8) and 0xFF).toByte()
        payload.copyInto(body, ProvisionalWire.HEADER_LENGTH)
        val decoded = body + u32(Crc32c.calculate(body).toUInt())
        return Cobs.encode(decoded) + byteArrayOf(ProvisionalWire.DELIMITER)
    }

    private fun decodedSensorFrame(payload: ByteArray): DecodedFrame =
        DecodedFrame(
            FrameHeader(MessageType.SENSOR_SAMPLE, MessagePriority.NORMAL, 1u, 1uL, payload.size),
            payload,
        )

    private fun u32(value: UInt): ByteArray = ByteArray(4) { i -> ((value shr (8 * i)) and 0xFFu).toByte() }
    private fun u64(value: ULong): ByteArray = ByteArray(8) { i -> ((value shr (8 * i)) and 0xFFuL).toByte() }
    private fun fieldSigned(id: Int, value: Int): ByteArray = byteArrayOf(id.toByte(), 1) + u32(value.toUInt())
    private fun fieldUnsigned(id: Int, value: UInt): ByteArray = byteArrayOf(id.toByte(), 2) + u32(value)

    private fun hex(value: String): ByteArray =
        value.trim()
            .split(Regex("\\s+"))
            .filter { it.isNotEmpty() }
            .map { it.toInt(16).toByte() }
            .toByteArray()
}
