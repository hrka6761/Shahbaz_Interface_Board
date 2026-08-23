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
        sensorValidityBitsMatchFirmware()
        sensorTelemetryAndAltitude()
        deviceInfoValidationPolicy()
        boundedTimeSyncRetryCorrelation()
        operationalAndroidSessionLifecycle()
        startupAcknowledgementsFailClosed()
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

    private fun sensorValidityBitsMatchFirmware() {
        check(SensorValidity.TRANSPORT_VALID == 0x01u)
        check(SensorValidity.CRC_VALID == 0x02u)
        check(SensorValidity.CALIBRATION_VALID == 0x04u)
        check(SensorValidity.TIMING_VALID == 0x08u)
        check(SensorValidity.PLAUSIBILITY_VALID == 0x10u)
        val common = SensorValidity.TRANSPORT_VALID or
            SensorValidity.CRC_VALID or
            SensorValidity.TIMING_VALID or
            SensorValidity.PLAUSIBILITY_VALID
        check(SensorValidity.COMMON_REQUIRED == common)
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

    private fun deviceInfoValidationPolicy() {
        check(BoardValidationIssues.FATAL_MASK == 0x400Fu)
        check(BoardValidationIssues.ADVISORY_MASK == 0xBFF0u)
        check(BoardValidationIssues.KNOWN_MASK == 0xFFFFu)
        val advisoryMask = BoardValidationIssues.ADVISORY_MASK
        val decoded = decodeDeviceInfoResponse(
            FrameDecoder.decodeDelimited(
                deviceFrame(
                    MessageType.DEVICE_INFO_RESPONSE,
                    1u,
                    1uL,
                    deviceInfoPayload(issueMask = advisoryMask),
                ),
            ),
        ) ?: error("DeviceInfoResponse was not decoded")
        val validated = decoded.validateSensorOnlyProfile()
        check(validated.deviceInfo.boardValidationIssueMask == advisoryMask)
        check(validated.advisoryIssueMask == advisoryMask)
        check(validated.deviceInfo.target == DeviceTarget.ESP32_S3)

        fun rejected(info: DeviceInfo): Boolean = try {
            info.validateSensorOnlyProfile()
            false
        } catch (_: ProtocolException) {
            true
        }

        check(rejected(decoded.copy(protocolVersion = 1)))
        check(rejected(decoded.copy(boardValidationIssueMask = 0x1_0000u)))
        check(rejected(decoded.copy(actuatorAvailable = true)))
        check(rejected(decoded.copy(actuatorsEnabledByConfiguration = true)))
        check(rejected(decoded.copy(activeMotorChannels = 1)))
        check(rejected(decoded.copy(activeServoChannels = 1)))
        repeat(16) { bit ->
            val issue = 1u shl bit
            if ((issue and BoardValidationIssues.FATAL_MASK) != 0u) {
                check(rejected(decoded.copy(boardValidationIssueMask = issue)))
            }
        }

        var malformedRejected = false
        try {
            val invalidTarget = deviceInfoPayload().also { it[1] = 2 }
            decodeDeviceInfoResponse(
                FrameDecoder.decodeDelimited(
                    deviceFrame(MessageType.DEVICE_INFO_RESPONSE, 2u, 2uL, invalidTarget),
                ),
            )
        } catch (_: ProtocolException) {
            malformedRejected = true
        }
        check(malformedRejected) { "unknown DeviceInfo target was accepted" }
    }

    private fun boundedTimeSyncRetryCorrelation() {
        var now = 10_000uL
        val session = ShahbazLinkSession(monotonicUs = { now })
        session.onUsbAttached()
        val sentAt = mutableListOf<ULong>()
        repeat(ShahbazLinkSession.MAX_OUTSTANDING_TIME_SYNCS + 1) {
            sentAt += now
            session.buildTimeSync()
            now += 1_000uL
        }

        val token = 0x0102030405060708uL
        fun responseFor(clientSendUs: ULong, sequence: UInt): ByteArray = deviceFrame(
            MessageType.TIME_SYNC_RESPONSE,
            sequence,
            now,
            u64(clientSendUs) + u64(now - 2uL) + u64(now - 1uL) + u64(token),
        )

        val evictedEvents = session.feedUsbBytes(responseFor(sentAt.first(), 1u))
        check(evictedEvents.any { it is ShahbazLinkSession.Event.ProtocolRejected }) {
            "oldest TimeSync timestamp was not evicted from the bounded correlation window"
        }
        check(session.sessionToken == null)

        val retainedEvents = session.feedUsbBytes(responseFor(sentAt[1], 2u))
        check(retainedEvents.any { it is ShahbazLinkSession.Event.TimeSynchronized }) {
            "a retained delayed TimeSync response did not establish the session"
        }

        val clearedEvents = session.feedUsbBytes(responseFor(sentAt.last(), 3u))
        check(clearedEvents.any { it is ShahbazLinkSession.Event.ProtocolRejected }) {
            "accepted TimeSync response did not clear all outstanding timestamps"
        }
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

        // A bounded client retry retains earlier outstanding timestamps. A delayed response for
        // any retained attempt may establish the session, then clears every outstanding request.
        now += 500_000uL
        val retryRequest = session.buildTimeSync()
        check(FrameDecoder.decodeDelimited(retryRequest).header.messageType == MessageType.TIME_SYNC_REQUEST)
        val token = 0x8877665544332211uL
        val syncResponsePayload = u64(1_000_000uL) + u64(1_000_010uL) + u64(1_000_020uL) + u64(token)
        val lateResponse = deviceFrame(
            MessageType.TIME_SYNC_RESPONSE,
            9u,
            1_000_020uL,
            syncResponsePayload,
        )
        val delayedEvents = session.feedUsbBytes(lateResponse)
        check(
            delayedEvents.any {
                it is ShahbazLinkSession.Event.TimeSynchronized && it.token == token
            },
        )
        check(session.sessionToken == token)

        val currentResponsePayload =
            u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(token)
        val response = deviceFrame(
            MessageType.TIME_SYNC_RESPONSE,
            10u,
            now + 20uL,
            currentResponsePayload,
        )
        val events = session.feedUsbBytes(response)
        check(events.any { it is ShahbazLinkSession.Event.ProtocolRejected })
        check(events.none { it is ShahbazLinkSession.Event.TimeSynchronized })
        check(session.sessionToken == token)
        val deviceInfoRequest = FrameDecoder.decodeDelimited(session.buildDeviceInfoRequest())
        val heartbeat = FrameDecoder.decodeDelimited(session.buildHeartbeat())
        check(deviceInfoRequest.header.messageType == MessageType.DEVICE_INFO_REQUEST)
        check(heartbeat.header.messageType == MessageType.HEARTBEAT)
        blocked = false
        try {
            session.buildStartTelemetry()
        } catch (_: OutboundPolicyException) {
            blocked = true
        }
        check(blocked) { "StartTelemetry was allowed before startup HeartbeatAck" }

        val heartbeatEvents = session.feedUsbBytes(
            deviceFrame(
                MessageType.HEARTBEAT_ACK,
                11u,
                now + 30uL,
                byteArrayOf(),
                MessagePriority.CRITICAL,
            ),
        )
        check(heartbeatEvents.any { it === ShahbazLinkSession.Event.HeartbeatAcknowledged })
        val startTelemetry = FrameDecoder.decodeDelimited(session.buildStartTelemetry())
        check(startTelemetry.header.messageType == MessageType.START_TELEMETRY)
        check(startTelemetry.payload.copyOfRange(0, 8).contentEquals(u64(token)))
        val earlySensorEvents = session.feedUsbBytes(
            deviceFrame(
                MessageType.SENSOR_SAMPLE,
                12u,
                now + 35uL,
                sht30Payload(sequence = 1u, deviceTimestampUs = now + 35uL),
            ),
        )
        check(earlySensorEvents.any { it is ShahbazLinkSession.Event.ProtocolRejected })
        check(earlySensorEvents.none { it is ShahbazLinkSession.Event.Sht30 }) {
            "a SensorSample was emitted before StartTelemetry acknowledgement/readiness"
        }
        val startAckPayload =
            u32(startTelemetry.header.sequence) +
                byteArrayOf(ApplicationAction.START_TELEMETRY.wireValue.toByte())
        val startAckEvents = session.feedUsbBytes(
            deviceFrame(MessageType.COMMAND_ACK, 13u, now + 40uL, startAckPayload),
        )
        check(startAckEvents.any { it === ShahbazLinkSession.Event.StartTelemetryAcknowledged })
        check(startAckEvents.none { it is ShahbazLinkSession.Event.SessionReady }) {
            "session became ready before DeviceInfo validation"
        }
        val readyEvents = session.feedUsbBytes(
            deviceFrame(
                MessageType.DEVICE_INFO_RESPONSE,
                14u,
                now + 50uL,
                deviceInfoPayload(issueMask = 0x2FF0u),
            ),
        )
        check(readyEvents.any { it is ShahbazLinkSession.Event.DeviceInfoValidated })
        check(
            readyEvents.count {
                it is ShahbazLinkSession.Event.SessionReady &&
                    it.token == token &&
                    it.deviceInfo.advisoryIssueMask == 0x2FF0u
            } == 1,
        )
        check(session.deviceInfo?.deviceInfo?.boardValidationIssueMask == 0x2FF0u)

        session.qnhHpa = 1000.0
        now += ShahbazLinkSession.TIME_SYNC_REFRESH_US
        check(session.timeSyncRefreshDue(now))
        session.buildTimeSync()
        check(!session.timeSyncRefreshDue(now)) { "pending periodic TimeSync was duplicated" }
        val refreshPayload = u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(token)
        val refreshEvents = session.feedUsbBytes(
            deviceFrame(MessageType.TIME_SYNC_RESPONSE, 11u, now + 20uL, refreshPayload),
        )
        check(refreshEvents.none { it is ShahbazLinkSession.Event.SessionReady }) {
            "periodic TimeSync restarted session startup"
        }
        check(session.sessionToken == token)

        now += ShahbazLinkSession.TIME_SYNC_REFRESH_US
        session.buildTimeSync()
        val changedToken = token xor 0x55uL
        val changedPayload =
            u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(changedToken)
        val changedEvents = session.feedUsbBytes(
            deviceFrame(MessageType.TIME_SYNC_RESPONSE, 12u, now + 20uL, changedPayload),
        )
        check(
            changedEvents.any {
                it is ShahbazLinkSession.Event.SessionRejected &&
                    it.reason.contains("without USB detach")
            },
        )
        check(changedEvents.none { it is ShahbazLinkSession.Event.SessionReady })
        check(session.sessionToken == token) { "token changed inside one physical attachment" }

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

        // A real detach resets the token invariant and permits one readiness edge for the new link.
        now += 1uL
        session.onUsbAttached()
        session.buildTimeSync()
        val replacementToken = 0x1020304050607080uL
        val replacementPayload =
            u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(replacementToken)
        val replacementEvents = session.feedUsbBytes(
            deviceFrame(MessageType.TIME_SYNC_RESPONSE, 1u, now + 20uL, replacementPayload),
        )
        check(
            replacementEvents.count {
                it is ShahbazLinkSession.Event.TimeSynchronized && it.token == replacementToken
            } == 1,
        )
        check(replacementEvents.none { it is ShahbazLinkSession.Event.SessionReady })
        session.buildDeviceInfoRequest()
        session.buildHeartbeat()
        val replacementInfoEvents = session.feedUsbBytes(
            deviceFrame(
                MessageType.DEVICE_INFO_RESPONSE,
                2u,
                now + 25uL,
                deviceInfoPayload(),
            ),
        )
        check(replacementInfoEvents.any { it is ShahbazLinkSession.Event.DeviceInfoValidated })
        check(replacementInfoEvents.none { it is ShahbazLinkSession.Event.SessionReady })
        session.feedUsbBytes(
            deviceFrame(
                MessageType.HEARTBEAT_ACK,
                3u,
                now + 30uL,
                byteArrayOf(),
                MessagePriority.CRITICAL,
            ),
        )
        val replacementStart = FrameDecoder.decodeDelimited(session.buildStartTelemetry())
        val replacementReady = session.feedUsbBytes(
            deviceFrame(
                MessageType.COMMAND_ACK,
                4u,
                now + 40uL,
                u32(replacementStart.header.sequence) +
                    byteArrayOf(ApplicationAction.START_TELEMETRY.wireValue.toByte()),
            ),
        )
        check(
            replacementReady.count {
                it is ShahbazLinkSession.Event.SessionReady && it.token == replacementToken
            } == 1,
        )
    }

    private fun startupAcknowledgementsFailClosed() {
        var now = 5_000_000uL
        val session = ShahbazLinkSession(monotonicUs = { now })
        session.onUsbAttached()
        session.buildTimeSync()
        val token = 0x1234567890ABCDEFuL
        session.feedUsbBytes(
            deviceFrame(
                MessageType.TIME_SYNC_RESPONSE,
                1u,
                now + 20uL,
                u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(token),
            ),
        )
        session.buildDeviceInfoRequest()
        val heartbeat = FrameDecoder.decodeDelimited(session.buildHeartbeat())
        val nackPayload = u32(heartbeat.header.sequence) + u16(0x0008) + u16(0x0006)
        val nackEvents = session.feedUsbBytes(
            deviceFrame(MessageType.COMMAND_NACK, 2u, now + 30uL, nackPayload),
        )
        check(nackEvents.any { it is ShahbazLinkSession.Event.SessionRejected })
        var blocked = false
        try {
            session.buildHeartbeat()
        } catch (_: OutboundPolicyException) {
            blocked = true
        }
        check(blocked) { "failed startup session continued sending heartbeat" }

        now += 1_000uL
        val wrongAckSession = ShahbazLinkSession(monotonicUs = { now })
        wrongAckSession.onUsbAttached()
        wrongAckSession.buildTimeSync()
        wrongAckSession.feedUsbBytes(
            deviceFrame(
                MessageType.TIME_SYNC_RESPONSE,
                1u,
                now + 20uL,
                u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(token),
            ),
        )
        wrongAckSession.buildHeartbeat()
        wrongAckSession.feedUsbBytes(
            deviceFrame(
                MessageType.HEARTBEAT_ACK,
                2u,
                now + 30uL,
                byteArrayOf(),
                MessagePriority.CRITICAL,
            ),
        )
        val start = FrameDecoder.decodeDelimited(wrongAckSession.buildStartTelemetry())
        val wrongAck = u32(start.header.sequence + 1u) +
            byteArrayOf(ApplicationAction.START_TELEMETRY.wireValue.toByte())
        val wrongAckEvents = wrongAckSession.feedUsbBytes(
            deviceFrame(MessageType.COMMAND_ACK, 3u, now + 40uL, wrongAck),
        )
        check(wrongAckEvents.any { it is ShahbazLinkSession.Event.SessionRejected })
        check(wrongAckEvents.none { it is ShahbazLinkSession.Event.SessionReady })

        now += 1_000uL
        val invalidInfoSession = ShahbazLinkSession(monotonicUs = { now })
        invalidInfoSession.onUsbAttached()
        invalidInfoSession.buildTimeSync()
        invalidInfoSession.feedUsbBytes(
            deviceFrame(
                MessageType.TIME_SYNC_RESPONSE,
                1u,
                now + 20uL,
                u64(now) + u64(now + 10uL) + u64(now + 20uL) + u64(token),
            ),
        )
        invalidInfoSession.buildDeviceInfoRequest()
        invalidInfoSession.buildHeartbeat()
        val invalidInfoEvents = invalidInfoSession.feedUsbBytes(
            deviceFrame(
                MessageType.DEVICE_INFO_RESPONSE,
                2u,
                now + 30uL,
                deviceInfoPayload(issueMask = BoardValidationIssues.FATAL_MASK),
            ),
        )
        check(invalidInfoEvents.any { it is ShahbazLinkSession.Event.SessionRejected })
        check(invalidInfoEvents.none { it is ShahbazLinkSession.Event.DeviceInfoValidated })
    }

    private fun deviceFrame(
        type: MessageType,
        sequence: UInt,
        senderUs: ULong,
        payload: ByteArray,
        priority: MessagePriority = MessagePriority.HIGH,
    ): ByteArray {
        val body = ByteArray(ProvisionalWire.HEADER_LENGTH + payload.size)
        body[0] = ProvisionalWire.VERSION.toByte()
        body[1] = ProvisionalWire.HEADER_LENGTH.toByte()
        body[2] = (type.wireValue and 0xFF).toByte()
        body[3] = ((type.wireValue ushr 8) and 0xFF).toByte()
        body[4] = 0
        body[5] = 0
        body[6] = priority.wireValue.toByte()
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

    private fun deviceInfoPayload(
        protocolVersion: Int = ProvisionalWire.VERSION,
        issueMask: UInt = 0u,
        activeMotorChannels: Int = 0,
        activeServoChannels: Int = 0,
        actuatorAvailable: Boolean = false,
        actuatorsEnabled: Boolean = false,
    ): ByteArray =
        byteArrayOf(
            protocolVersion.toByte(),
            DeviceTarget.ESP32_S3.wireValue.toByte(),
            4,
            2,
        ) +
            u32(16u * 1024u * 1024u) +
            u32(8u * 1024u * 1024u) +
            u32(issueMask) +
            byteArrayOf(
                activeMotorChannels.toByte(),
                activeServoChannels.toByte(),
                (if (actuatorAvailable) 1 else 0).toByte(),
                (if (actuatorsEnabled) 1 else 0).toByte(),
            )

    private fun sht30Payload(sequence: UInt, deviceTimestampUs: ULong): ByteArray =
        byteArrayOf(SensorId.SHT30.wireValue.toByte(), 0) +
            u32(sequence) +
            u64(deviceTimestampUs) +
            u32(SensorValidity.COMMON_REQUIRED) +
            u32(1u) +
            u32(0u) +
            byteArrayOf(2) +
            fieldSigned(1, 21_500) +
            fieldUnsigned(2, 45_000u)

    private fun u16(value: Int): ByteArray = ByteArray(2) { i -> ((value shr (8 * i)) and 0xFF).toByte() }
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
