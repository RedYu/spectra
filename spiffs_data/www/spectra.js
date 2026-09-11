/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 * Shared web entry point. Only the current page is initialized.
 */
(() => {
    // ===== index.html =====
    function init_overview() {

        const REFRESH_INTERVAL_MS = 2000;

        function setText(id, value) {
            const element =
                document.getElementById(id);

            if (element === null) {
                return;
            }

            element.textContent =
                value ?? "-";

            element.classList.remove(
                "loading"
            );
        }

        function setStateValue(
            id,
            enabled,
            enabledText = "Enabled",
            disabledText = "Disabled"
        ) {
            const element =
                document.getElementById(id);

            if (element === null) {
                return;
            }

            element.textContent =
                enabled
                    ? enabledText
                    : disabledText;

            element.classList.toggle(
                "value-success",
                enabled
            );

            element.classList.toggle(
                "value-danger",
                !enabled
            );
        }

        function setBadge(
            id,
            text,
            state = ""
        ) {
            const badge =
                document.getElementById(id);

            if (badge === null) {
                return;
            }

            badge.textContent = text;
            badge.className =
                `card-badge ${state}`.trim();
        }

        function formatBytes(bytes) {
            if (!Number.isFinite(bytes) ||
                bytes < 0) {

                return "-";
            }

            if (bytes < 1024) {
                return `${bytes} B`;
            }

            if (bytes < 1024 * 1024) {
                return `${(
                    bytes / 1024
                ).toFixed(1)} KB`;
            }

            if (bytes < 1024 * 1024 * 1024) {
                return `${(
                    bytes /
                    (1024 * 1024)
                ).toFixed(1)} MB`;
            }

            return `${(
                bytes /
                (1024 * 1024 * 1024)
            ).toFixed(1)} GB`;
        }

        function formatUptime(seconds) {
            if (!Number.isFinite(seconds) ||
                seconds < 0) {

                return "-";
            }

            const days =
                Math.floor(seconds / 86400);

            const hours =
                Math.floor(
                    (seconds % 86400) / 3600
                );

            const minutes =
                Math.floor(
                    (seconds % 3600) / 60
                );

            const remainingSeconds =
                Math.floor(seconds % 60);

            const time =
                [
                    hours,
                    minutes,
                    remainingSeconds
                ]
                    .map(value =>
                        String(value).padStart(2, "0")
                    )
                    .join(":");

            return days > 0
                ? `${days}d ${time}`
                : time;
        }

        function formatVoltage(
            enabled,
            voltage
        ) {
            if (!enabled) {
                return "Disabled";
            }

            if (Number.isFinite(voltage) &&
                voltage > 0) {

                return `Enabled · ${voltage} mV`;
            }

            return "Enabled";
        }

        function getPowerOutput(
            power,
            name
        ) {
            const outputs =
                power.outputs || {};

            const voltages =
                power.configured_voltages_mv ||
                power.voltages_mv ||
                {};

            const nested =
                outputs[name];

            if ((typeof nested === "object") &&
                (nested !== null)) {

                return {
                    enabled:
                        nested.enabled === true,

                    voltage:
                        nested.configured_voltage_mv ??
                        nested.voltage_mv ??
                        0
                };
            }

            return {
                enabled:
                    nested === true ||
                    power[`${name}_enabled`] === true,

                voltage:
                    voltages[name] ??
                    power[
                        `${name}_configured_voltage_mv`
                    ] ??
                    0
            };
        }

        async function fetchJson(path) {
            const response =
                await fetch(path, {
                    cache: "no-store"
                });

            let result;

            try {
                result =
                    await response.json();
            } catch {
                throw new Error(
                    `${path}: invalid response`
                );
            }

            if (!response.ok) {
                throw new Error(
                    result.message ||
                    `${path}: HTTP ${response.status}`
                );
            }

            return result;
        }

        function updateDeviceTime(time) {
            const value =
                document.getElementById(
                    "system-local-time"
                );

            const indicator =
                document.getElementById(
                    "system-time-sync"
                );

            if (!value || !indicator) {
                return;
            }

            const valid =
                time &&
                time.valid === true &&
                typeof time.local === "string" &&
                time.local.length >= 19;

            value.textContent =
                valid
                    ? time.local
                        .slice(0, 19)
                        .replace("T", " ")
                    : "Unavailable";

            indicator.classList.remove(
                "waiting",
                "synchronized"
            );

            let stateText =
                "Time synchronization unavailable";

            if (time?.synchronized === true) {
                indicator.classList.add(
                    "synchronized"
                );

                stateText =
                    "Time synchronized via SNTP";

            } else if (time?.service_running === true) {
                indicator.classList.add(
                    "waiting"
                );

                stateText =
                    "Waiting for SNTP synchronization";
            }

            indicator.title = stateText;

            indicator.setAttribute(
                "aria-label",
                stateText
            );
        }

        function updateSystem(system) {
            const title =
                system.device_name
                    ? `${system.device_id}`
                    : system.device_id || "spectra";

            setText(
                "device-title",
                title
            );

            setText(
                "device-subtitle",
                system.firmware_version
                    ? `${system.device_name}`
                    : "CAN Analyzer"
            );

            setText(
                "summary-cpu",
                `${system.cpu_usage ?? 0}%`
            );

            setText(
                "summary-frequency",
                `${system.cpu_frequency_mhz ?? 0} MHz`
            );

            setText(
                "summary-heap",
                formatBytes(system.free_heap)
            );

            setText(
                "summary-minimum-heap",
                `Minimum ${formatBytes(
                    system.minimum_free_heap
                )}`
            );

            setText(
                "summary-temperature",
                system.chip_temperature_valid
                    ? `${Number(
                        system.chip_temperature_celsius
                    ).toFixed(1)} °C`
                    : "Unavailable"
            );

            setText(
                "summary-uptime",
                formatUptime(system.uptime_sec)
            );

            setText(
                "summary-reset",
                `Reset: ${
                    system.reset_reason_name ||
                    "unknown"
                }`
            );

            setText(
                "system-device-id",
                system.device_id
            );

            setText(
                "system-device-name",
                system.device_name
            );

            setText(
                "system-firmware",
                system.firmware_version
            );

            setText(
                "system-hardware",
                system.hardware_version
            );

            const processor =
                system.chip_model
                    ? `${system.chip_model}, ${
                        system.chip_cores
                    } cores`
                    : "-";

            setText(
                "system-processor",
                processor
            );

            setText(
                "system-flash",
                formatBytes(system.flash_size)
            );

            setText(
                "system-psram",
                formatBytes(system.psram_size)
            );

            setStateValue(
                "system-storage",
                system.storage_ready,
                "Ready",
                "Unavailable"
            );

            const sdMounted =
                system.sd_card_mounted === true;

            const sdInfoAvailable =
                sdMounted &&
                system.sd_card_info_available === true;

            const sdStatus =
                sdMounted
                    ? system.sd_card_filesystem
                        ? `Mounted · ${
                            system.sd_card_filesystem
                        }`
                        : "Mounted"
                    : "Not mounted";

            setStateValue(
                "system-sd-card",
                sdMounted,
                sdStatus,
                "Not mounted"
            );

            setText(
                "system-sd-capacity",
                sdInfoAvailable
                    ? `${
                        formatBytes(
                            system.sd_card_free_bytes
                        )
                    } free / ${
                        formatBytes(
                            system.sd_card_total_bytes
                        )
                    }`
                    : "-"
            );

            setBadge(
                "system-badge",
                "Online",
                "success"
            );

            updateConnectionStatus(
                system.internet_available
            );

            updateDeviceTime(
                system.time
            );
        }

        function updateConnectionStatus(
            available
        ) {
            const container =
                document.getElementById(
                    "connection-status"
                );

            const text =
                document.getElementById(
                    "connection-status-text"
                );

            container.classList.remove(
                "online",
                "offline"
            );

            if (available) {
                container.classList.add(
                    "online"
                );

                text.textContent =
                    "Backend online";
            } else {
                container.classList.add(
                    "offline"
                );

                text.textContent =
                    "Backend unavailable";
            }
        }

        function updateNetwork(network) {
            const wifi =
                network.wifi || {};

            const softap =
                wifi.softap || {};

            const station =
                wifi.station || {};

            const rndis =
                network.usb_rndis || {};

            const dns =
                network.dns || {};

            const mdns =
                network.mdns || {};

            setText(
                "network-mode",
                wifi.mode || "disabled"
            );

            setText(
                "network-ap",
                softap.enabled
                    ? softap.ssid || "Enabled"
                    : "Disabled"
            );

            setText(
                "network-ap-address",
                softap.ipv4?.address || "-"
            );

            setText(
                "network-clients",
                String(
                    softap.client_count ?? 0
                )
            );

            setText(
                "network-station",
                station.connected
                    ? station.ssid || "Connected"
                    : station.enabled
                        ? "Connecting"
                        : "Disabled"
            );

            setText(
                "network-sta-address",
                station.ipv4?.address || "-"
            );

            setText(
                "network-rndis",
                rndis.host_connected
                    ? "Host connected"
                    : rndis.started
                        ? "Waiting for host"
                        : "Disabled"
            );

            setText(
                "network-rndis-address",
                rndis.ipv4?.address || "-"
            );

            setText(
                "network-dns",
                dns.started
                    ? dns.local_name
                    : "Stopped"
            );

            setText(
                "network-mdns",
                mdns.started
                    ? mdns.address
                    : "Stopped"
            );

            const active =
                station.connected ||
                rndis.host_connected ||
                softap.client_count > 0;

            setBadge(
                "network-badge",
                active ? "Active" : "Idle",
                active ? "success" : ""
            );
        }

        function updateBattery(battery) {
            setText(
                "power-charger",
                battery.charger || "ETA6003"
            );

            const element =
                document.getElementById(
                    "power-battery"
                );

            if (element === null) {
                return {
                    low: false,
                    critical: false
                };
            }

            element.classList.remove(
                "value-success",
                "value-warning",
                "value-danger"
            );

            if (battery.service_running !== true) {
                setText(
                    "power-battery",
                    "Service unavailable"
                );

                return {
                    low: false,
                    critical: false
                };
            }

            if (battery.measurement_valid !== true) {
                setText(
                    "power-battery",
                    "Measuring"
                );

                return {
                    low: false,
                    critical: false
                };
            }

            if (battery.present !== true) {
                setText(
                    "power-battery",
                    "Not installed"
                );

                return {
                    low: false,
                    critical: false
                };
            }

            const voltageMv =
                Number(battery.voltage_mv);

            const level =
                Math.max(
                    0,
                    Math.min(
                        100,
                        Number(
                            battery.level_percent
                        ) || 0
                    )
                );

            const voltageText =
                Number.isFinite(voltageMv)
                    ? `${(
                        voltageMv / 1000
                    ).toFixed(2)} V`
                    : "-";

            setText(
                "power-battery",
                `${level}% · ${voltageText}`
            );

            const critical =
                level <= 5;

            const low =
                level <= 20;

            element.classList.toggle(
                "value-success",
                !low
            );

            element.classList.toggle(
                "value-warning",
                low && !critical
            );

            element.classList.toggle(
                "value-danger",
                critical
            );

            return {
                low,
                critical
            };
        }

        function updatePower(power) {
            const batteryState =
                updateBattery(
                    power.battery || {}
                );

            const dcdc1 =
                getPowerOutput(power, "dcdc1");

            const dcdc2 =
                getPowerOutput(power, "dcdc2");

            const dcdc3 =
                getPowerOutput(power, "dcdc3");

            const aldo1 =
                getPowerOutput(power, "aldo1");

            const dldo1 =
                getPowerOutput(power, "dldo1");

            setText(
                "power-controller",
                power.controller || "AXP313A"
            );

            setText(
                "power-dcdc1",
                formatVoltage(
                    dcdc1.enabled,
                    dcdc1.voltage
                )
            );

            setText(
                "power-dcdc2",
                formatVoltage(
                    dcdc2.enabled,
                    dcdc2.voltage
                )
            );

            setText(
                "power-dcdc3",
                formatVoltage(
                    dcdc3.enabled,
                    dcdc3.voltage
                )
            );

            setText(
                "power-aldo1",
                formatVoltage(
                    aldo1.enabled,
                    aldo1.voltage
                )
            );

            setText(
                "power-dldo1",
                formatVoltage(
                    dldo1.enabled,
                    dldo1.voltage
                )
            );

            setText(
                "power-source",
                power.power_on_source_name ??
                power.power_on_source ??
                "-"
            );

            const interrupts =
                power.interrupts || {};

            const temperatureIrq =
                interrupts.overtemperature
                    ?.active === true;

            const dcdc2UndervoltageIrq =
                interrupts.dcdc2_undervoltage
                    ?.active === true;

            const dcdc3UndervoltageIrq =
                interrupts.dcdc3_undervoltage
                    ?.active === true;

            const powerWarning =
                temperatureIrq ||
                dcdc2UndervoltageIrq ||
                dcdc3UndervoltageIrq ||
                batteryState.low;

            setText(
                "power-temperature",
                temperatureIrq
                    ? "Triggered"
                    : "Normal"
            );

            const irqStatus =
                power.irq_status ??
                interrupts.status_raw;

            setText(
                "power-irq",
                Number.isFinite(irqStatus)
                    ? `0x${Number(
                        irqStatus
                    )
                        .toString(16)
                        .toUpperCase()
                        .padStart(2, "0")}`
                    : "-"
            );

            setBadge(
                "power-badge",
                batteryState.critical
                    ? "Battery critical"
                    : powerWarning
                        ? "Warning"
                        : "Normal",
                batteryState.critical
                    ? "danger"
                    : powerWarning
                        ? "warning"
                        : "success"
            );
        }

        async function refreshDashboard() {
            const status =
                document.getElementById(
                    "refresh-status"
                );

            try {
                const [
                    system,
                    network,
                    power
                ] = await Promise.all([
                    fetchJson("/api/system"),
                    fetchJson("/api/network"),
                    fetchJson("/api/power")
                ]);

                updateSystem(system);
                updateNetwork(network);
                updatePower(power);

                status.textContent =
                    `Updated ${new Date()
                        .toLocaleTimeString()}`;

            } catch (error) {
                status.textContent =
                    `Update failed: ${error.message}`;

                console.error(
                    "Failed to refresh dashboard:",
                    error
                );
            }
        }

        refreshDashboard();

        window.setInterval(
            refreshDashboard,
            REFRESH_INTERVAL_MS
        );
    
    }

    // ===== settings.html =====
    function init_settings() {

        const elements = {
            deviceTitle:
                document.getElementById("device-title"),

            deviceSubtitle:
                document.getElementById("device-subtitle"),

            deviceTarget:
                document.getElementById("device-target"),

            deviceName:
                document.getElementById("device-name"),

            schemaVersion:
                document.getElementById("schema-version"),

            restartNotice:
                document.getElementById("restart-notice"),

            brightness:
                document.getElementById("brightness"),

            brightnessValue:
                document.getElementById("brightness-value"),

            animations:
                document.getElementById("animations"),

            theme:
                document.getElementById("theme"),

            sdLogging:
                document.getElementById("sd-logging"),

            logWarningTags:
                document.getElementById(
                    "log-warning-tags"
                ),

            logInfoTags:
                document.getElementById(
                    "log-info-tags"
                ),

            logDebugTags:
                document.getElementById(
                    "log-debug-tags"
                ),

            logDisabledTags:
                document.getElementById(
                    "log-disabled-tags"
                ),

            wifiApEnabled:
                document.getElementById("wifi-ap-enabled"),

            wifiApSsid:
                document.getElementById("wifi-ap-ssid"),

            wifiApPassword:
                document.getElementById("wifi-ap-password"),

            wifiApOpen:
                document.getElementById("wifi-ap-open"),

            wifiStaEnabled:
                document.getElementById("wifi-sta-enabled"),

            wifiStaSsid:
                document.getElementById("wifi-sta-ssid"),

            wifiStaPassword:
                document.getElementById("wifi-sta-password"),

            wifiStaOpen:
                document.getElementById("wifi-sta-open"),

            scanWifi:
                document.getElementById("scan-wifi"),

            wifiScanStatus:
                document.getElementById(
                    "wifi-scan-status"
                ),

            wifiNetworkList:
                document.getElementById(
                    "wifi-network-list"
                ),

            wifiStaCredentialStatus:
                document.getElementById(
                    "wifi-sta-credential-status"
                ),

            clearStaCredentials:
                document.getElementById(
                    "clear-sta-credentials"
                ),

            usbRndisEnabled:
                document.getElementById("usb-rndis-enabled"),

            canPrimaryEnabled:
                document.getElementById(
                    "can-primary-enabled"
                ),

            canPrimaryBitrate:
                document.getElementById(
                    "can-primary-bitrate"
                ),

            canPrimaryListenOnly:
                document.getElementById(
                    "can-primary-listen-only"
                ),

            canSecondaryEnabled:
                document.getElementById("can-secondary-enabled"),

            canSecondaryNominalBitrate:
                document.getElementById(
                    "can-secondary-nominal-bitrate"
                ),

            canSecondaryDataBitrate:
                document.getElementById(
                    "can-secondary-data-bitrate"
                ),

            canSecondaryFdEnabled:
                document.getElementById(
                    "can-secondary-fd-enabled"
                ),

            canSecondaryBrsEnabled:
                document.getElementById(
                    "can-secondary-brs-enabled"
                ),

            canSecondaryListenOnly:
                document.getElementById(
                    "can-secondary-listen-only"
                ),

            restart:
                document.getElementById("restart"),

            reload:
                document.getElementById("reload"),

            save:
                document.getElementById("save"),

            status:
                document.getElementById("status")
        };

        let credentialsConfigured = false;
        let loadedStaSsid = "";

        let operationBusy = false;
        let scanRunning = false;
        let scanPollTimer = null;

        function updateActionStates() {
            elements.restart.disabled =
                operationBusy;

            elements.reload.disabled =
                operationBusy;

            elements.save.disabled =
                operationBusy;

            elements.animations.disabled =
                operationBusy;
            
            elements.theme.disabled =
                operationBusy;

            elements.clearStaCredentials.disabled =
                operationBusy ||
                !credentialsConfigured;

            elements.scanWifi.disabled =
                operationBusy ||
                scanRunning;

            elements.canPrimaryEnabled.disabled =
                operationBusy;

            updateCanControls();
        }

        function setBusy(busy) {
            operationBusy = busy;

            updateActionStates();
        }

        function setStatus(
            message,
            type = ""
        ) {
            elements.status.textContent =
                message;

            elements.status.className =
                `status ${type}`.trim();
        }

        function updateBrightness() {
            elements.brightnessValue.textContent =
                `${elements.brightness.value}%`;
        }

        function updateApControls() {
            const enabled =
                elements.wifiApEnabled.checked;

            elements.wifiApSsid.disabled =
                !enabled;

            elements.wifiApOpen.disabled =
                !enabled;

            elements.wifiApPassword.disabled =
                !enabled ||
                elements.wifiApOpen.checked;

            document
                .getElementById("show-ap-password")
                .disabled =
                    elements.wifiApPassword.disabled;
        }

        const CAN_FD_DATA_BITRATES = [
            1000000,
            2000000,
            4000000,
            5000000
        ];

        function selectValidSecondaryDataBitrate() {
            const nominalBitrate =
                Number(elements.canSecondaryNominalBitrate.value);

            const currentBitrate =
                Number(elements.canSecondaryDataBitrate.value);

            if (CAN_FD_DATA_BITRATES.includes(currentBitrate) &&
                currentBitrate > nominalBitrate) {

                return;
            }

            const nextBitrate =
                CAN_FD_DATA_BITRATES.find(
                    bitrate => bitrate > nominalBitrate
                );

            if (nextBitrate !== undefined) {
                elements.canSecondaryDataBitrate.value =
                    String(nextBitrate);
            }
        }

        function updateCanControls() {
            elements.canPrimaryEnabled.disabled =
                operationBusy;

            const primaryDisabled =
                operationBusy ||
                !elements.canPrimaryEnabled.checked;

            elements.canPrimaryBitrate.disabled =
                primaryDisabled;

            elements.canPrimaryListenOnly.disabled =
                primaryDisabled;

            elements.canSecondaryEnabled.disabled =
                operationBusy;

            const secondaryDisabled =
                operationBusy ||
                !elements.canSecondaryEnabled.checked;

            elements.canSecondaryNominalBitrate.disabled =
                secondaryDisabled;

            elements.canSecondaryFdEnabled.disabled =
                secondaryDisabled;

            elements.canSecondaryListenOnly.disabled =
                secondaryDisabled;

            if (!elements.canSecondaryFdEnabled.checked) {
                elements.canSecondaryBrsEnabled.checked = false;
            }

            elements.canSecondaryBrsEnabled.disabled =
                secondaryDisabled ||
                !elements.canSecondaryFdEnabled.checked;

            if (elements.canSecondaryBrsEnabled.checked) {
                selectValidSecondaryDataBitrate();
            }

            elements.canSecondaryDataBitrate.disabled =
                secondaryDisabled ||
                !elements.canSecondaryFdEnabled.checked ||
                !elements.canSecondaryBrsEnabled.checked;
        }

        function updateStaControls() {
            const enabled =
                elements.wifiStaEnabled.checked;

            elements.wifiStaSsid.disabled =
                !enabled;

            elements.wifiStaOpen.disabled =
                !enabled;

            elements.wifiStaPassword.disabled =
                !enabled ||
                elements.wifiStaOpen.checked;

            document
                .getElementById("show-sta-password")
                .disabled =
                    elements.wifiStaPassword.disabled;
        }

        function togglePassword(
            input,
            button
        ) {
            const visible =
                input.type === "text";

            input.type =
                visible
                    ? "password"
                    : "text";

            button.textContent =
                visible
                    ? "Show"
                    : "Hide";
        }

        function formatSignal(rssi) {
            if (!Number.isFinite(rssi)) {
                return "Unknown signal";
            }

            if (rssi >= -50) {
                return `${rssi} dBm - Excellent`;
            }

            if (rssi >= -65) {
                return `${rssi} dBm - Good`;
            }

            if (rssi >= -75) {
                return `${rssi} dBm - Fair`;
            }

            return `${rssi} dBm - Weak`;
        }

        function selectWifiNetwork(network) {
            elements.wifiStaEnabled.checked = true;

            elements.wifiStaSsid.value =
                network.ssid;

            elements.wifiStaPassword.value = "";

            elements.wifiStaOpen.checked =
                network.password_required !== true;

            updateStaControls();

            for (const item of
                 elements.wifiNetworkList.children) {

                item.classList.toggle(
                    "selected",
                    item.dataset.bssid ===
                        network.bssid
                );
            }

            if (network.password_required) {
                elements.wifiStaPassword.focus();

                setStatus(
                    `Selected ${network.ssid}. Enter its password.`
                );

            } else {
                setStatus(
                    `Selected open network ${network.ssid}.`
                );
            }
        }

        function renderWifiNetworks(networks) {
            elements.wifiNetworkList.replaceChildren();

            if (!Array.isArray(networks) ||
                networks.length === 0) {

                const message =
                    document.createElement("div");

                message.className =
                    "network-list-message";

                message.textContent =
                    "No Wi-Fi networks found";

                elements.wifiNetworkList.appendChild(
                    message
                );

                return;
            }

            for (const network of networks) {
                if ((typeof network !== "object") ||
                    (network === null) ||
                    (typeof network.ssid !== "string") ||
                    (network.ssid.length === 0)) {

                    continue;
                }

                const button =
                    document.createElement("button");

                button.type = "button";
                button.className = "network-item";

                button.dataset.bssid =
                    network.bssid || "";

                const information =
                    document.createElement("div");

                const name =
                    document.createElement("div");

                name.className = "network-name";
                name.textContent = network.ssid;

                const details =
                    document.createElement("div");

                details.className =
                    "network-details";

                const security =
                    network.password_required
                        ? "Secured"
                        : "Open";

                details.textContent =
                    `${security} - Channel ${
                        network.channel ?? "-"
                    } - ${network.bssid || "Unknown BSSID"}`;

                const signal =
                    document.createElement("div");

                signal.className =
                    "network-signal";

                signal.textContent =
                    formatSignal(network.rssi);

                information.append(
                    name,
                    details
                );

                button.append(
                    information,
                    signal
                );

                button.addEventListener(
                    "click",
                    () => {
                        selectWifiNetwork(
                            network
                        );
                    }
                );

                elements.wifiNetworkList.appendChild(
                    button
                );
            }

            if (elements.wifiNetworkList.children.length === 0) {
                const message =
                    document.createElement("div");

                message.className =
                    "network-list-message";

                message.textContent =
                    "No visible Wi-Fi networks";

                elements.wifiNetworkList.appendChild(
                    message
                );
            }
        }

        async function loadWifiScanStatus() {
            try {
                const network =
                    await fetchJson(
                        "/api/network",
                        {
                            cache: "no-store"
                        }
                    );

                const scan =
                    network.wifi?.scan;

                if (scan === undefined) {
                    throw new Error(
                        "Scan information is unavailable"
                    );
                }

                if (scan.state === "running") {
                    scanRunning = true;

                    updateActionStates();

                    elements.wifiScanStatus.textContent =
                        "Scanning...";

                    scheduleScanPoll();

                    return;
                }

                scanRunning = false;

                updateActionStates();

                if (scan.state === "complete") {
                    const networks =
                        Array.isArray(scan.networks)
                            ? scan.networks
                            : [];

                    renderWifiNetworks(
                        networks
                    );

                    elements.wifiScanStatus.textContent =
                        scan.truncated
                            ? `${networks.length} networks, result truncated`
                            : `${networks.length} networks found`;

                    return;
                }

                if (scan.state === "error") {
                    elements.wifiScanStatus.textContent =
                        `Scan failed: ${
                            scan.last_error_name ||
                            scan.last_error ||
                            "unknown error"
                        }`;

                    return;
                }

                elements.wifiScanStatus.textContent =
                    "Not scanned";

            } catch (error) {
                scanRunning = false;

                updateActionStates();

                elements.wifiScanStatus.textContent =
                    `Scan status unavailable: ${error.message}`;
            }
        }

        function scheduleScanPoll() {
            if (scanPollTimer !== null) {
                window.clearTimeout(
                    scanPollTimer
                );
            }

            scanPollTimer =
                window.setTimeout(
                    async () => {
                        scanPollTimer = null;

                        await loadWifiScanStatus();
                    },
                    750
                );
        }

        async function startWifiScan() {
            if (scanRunning ||
                operationBusy) {

                return;
            }

            scanRunning = true;

            updateActionStates();

            elements.wifiScanStatus.textContent =
                "Starting scan...";

            elements.wifiNetworkList.replaceChildren();

            try {
                await fetchJson(
                    "/api/network/wifi/scan",
                    {
                        method: "POST"
                    }
                );

                elements.wifiScanStatus.textContent =
                    "Scanning...";

                scheduleScanPoll();

            } catch (error) {
                scanRunning = false;

                updateActionStates();

                elements.wifiScanStatus.textContent =
                    `Scan failed: ${error.message}`;

                setStatus(
                    `Failed to start Wi-Fi scan: ${error.message}`,
                    "error"
                );
            }
        }

        async function fetchJson(
            path,
            options = {}
        ) {
            const response =
                await fetch(
                    path,
                    options
                );

            let result;

            try {
                result =
                    await response.json();
            } catch {
                throw new Error(
                    `Invalid server response: HTTP ${response.status}`
                );
            }

            if (!response.ok) {
                throw new Error(
                    result.message ||
                    `HTTP ${response.status}`
                );
            }

            return result;
        }

        function populateSettings(settings) {
            const device =
                settings.device || {};

            const display =
                settings.display || {};

            const logging =
                settings.logging || {};

            const tagLevels =
                logging.tag_levels || {};

            const ui =
                settings.ui || {};

            const network =
                settings.network || {};

            const can =
                settings.can || {};

            const canPrimary =
                can.primary || {};

            const canSecondary =
                can.secondary || {};

            const wifiAp =
                network.wifi_ap || {};

            const wifiSta =
                network.wifi_sta || {};

            const usbRndis =
                network.usb_rndis || {};

            elements.deviceTarget.textContent =
                device.target || "-";

            elements.deviceName.textContent =
                device.name || "-";

            elements.schemaVersion.textContent =
                settings.schema_version ?? "-";

            elements.deviceTitle.textContent =
                device.target || "Spectra";

            elements.deviceSubtitle.textContent =
                device.name || "Device settings";

            elements.brightness.value =
                display.brightness ?? 80;

            elements.animations.checked =
                ui.animations_enabled === true;

            elements.theme.value =
                ui.theme === "dark"
                    ? "dark"
                    : "light";

            elements.sdLogging.checked =
                logging.sd_enabled === true;

            elements.logWarningTags.value =
                tagLevels.warning ?? "";

            elements.logInfoTags.value =
                tagLevels.info ?? "";

            elements.logDebugTags.value =
                tagLevels.debug ?? "";

            elements.logDisabledTags.value =
                tagLevels.disabled ?? "";

            elements.wifiApEnabled.checked =
                wifiAp.enabled === true;

            elements.wifiApSsid.value =
                wifiAp.ssid || "";

            elements.wifiApPassword.value =
                wifiAp.password || "";

            elements.wifiApOpen.checked =
                wifiAp.password === "";

            elements.wifiStaEnabled.checked =
                wifiSta.enabled === true;

            elements.wifiStaSsid.value =
                wifiSta.ssid || "";

            elements.wifiStaPassword.value = "";
            elements.wifiStaOpen.checked = false;

            loadedStaSsid =
                wifiSta.ssid || "";

            credentialsConfigured =
                wifiSta.credentials_configured === true;

            elements.wifiStaCredentialStatus.textContent =
                credentialsConfigured
                    ? `Credentials stored${
                        wifiSta.credential_id
                            ? ` - ${wifiSta.credential_id}`
                            : ""
                    }`
                    : "No stored credentials";

            elements.wifiStaCredentialStatus
                .classList.toggle(
                    "configured",
                    credentialsConfigured
                );

            elements.usbRndisEnabled.checked =
                usbRndis.enabled === true;

            elements.canPrimaryEnabled.checked =
                canPrimary.enabled === true;

            elements.canPrimaryBitrate.value =
                String(
                    canPrimary.bitrate ?? 500000
                );

            elements.canPrimaryListenOnly.checked =
                canPrimary.listen_only !== false;

            elements.canSecondaryEnabled.checked =
                canSecondary.enabled === true;

            elements.canSecondaryNominalBitrate.value =
                String(
                    canSecondary.nominal_bitrate ?? 500000
                );

            elements.canSecondaryFdEnabled.checked =
                canSecondary.fd_enabled === true;

            elements.canSecondaryBrsEnabled.checked =
                canSecondary.fd_enabled === true &&
                canSecondary.brs_enabled === true;

            elements.canSecondaryListenOnly.checked =
                canSecondary.listen_only !== false;

            const secondaryDataBitrate =
                Number(canSecondary.data_bitrate ?? 2000000);

            if (CAN_FD_DATA_BITRATES.includes(secondaryDataBitrate)) {
                elements.canSecondaryDataBitrate.value =
                    String(secondaryDataBitrate);
            }

            selectValidSecondaryDataBitrate();
            updateCanControls();

            elements.restartNotice.classList.toggle(
                "visible",
                settings.restart_required === true
            );

            updateBrightness();
            updateApControls();
            updateStaControls();

            elements.clearStaCredentials.disabled =
                !credentialsConfigured;
        }

        function validateSettings() {
            if (elements.wifiApEnabled.checked) {
                const ssid =
                    elements.wifiApSsid.value.trim();

                const password =
                    elements.wifiApPassword.value;

                if (ssid.length === 0) {
                    throw new Error(
                        "SoftAP SSID cannot be empty"
                    );
                }

                if (!elements.wifiApOpen.checked &&
                    ((password.length < 8) ||
                     (password.length > 63))) {

                    throw new Error(
                        "SoftAP password must contain 8 to 63 characters"
                    );
                }
            }

            if (elements.wifiStaEnabled.checked) {
                const ssid =
                    elements.wifiStaSsid.value.trim();

                const password =
                    elements.wifiStaPassword.value;

                if (ssid.length === 0) {
                    throw new Error(
                        "Station SSID cannot be empty"
                    );
                }

                const networkChanged =
                    ssid !== loadedStaSsid;

                const needsCredentials =
                    !credentialsConfigured ||
                    networkChanged;

                if (!elements.wifiStaOpen.checked &&
                    needsCredentials &&
                    password.length === 0) {

                    throw new Error(
                        "Enter the Station password for the selected network"
                    );
                }

                if (!elements.wifiStaOpen.checked &&
                    password.length > 0 &&
                    ((password.length < 8) ||
                     (password.length > 63))) {

                    throw new Error(
                        "Station password must contain 8 to 63 characters"
                    );
                }
            }
        }

        function createSettingsPayload() {
            const wifiSta = {
                enabled:
                    elements.wifiStaEnabled.checked
            };

            const wifiStaSsid =
                elements.wifiStaSsid.value.trim();

            const wifiStaPassword =
                elements.wifiStaPassword.value;

            if (elements.wifiStaOpen.checked) {
                /*
                 * An open network still requires both fields in the API request.
                 */
                wifiSta.ssid =
                    wifiStaSsid;

                wifiSta.password = "";

            } else if (wifiStaPassword.length > 0) {
                /*
                 * Replace stored credentials only when a new password was entered.
                 */
                wifiSta.ssid =
                    wifiStaSsid;

                wifiSta.password =
                    wifiStaPassword;
            }

            const secondaryNominalBitrate =
                Number(
                    elements.canSecondaryNominalBitrate.value
                );

            const secondaryFdEnabled =
                elements.canSecondaryFdEnabled.checked;

            const secondaryBrsEnabled =
                secondaryFdEnabled &&
                elements.canSecondaryBrsEnabled.checked;

            const secondaryDataBitrate =
                secondaryBrsEnabled
                    ? Number(elements.canSecondaryDataBitrate.value)
                    : secondaryNominalBitrate;

            return {
                display: {
                    brightness:
                        Number(elements.brightness.value)
                },

                logging: {
                    sd_enabled:
                        elements.sdLogging.checked,

                    tag_levels: {
                        warning:
                            elements.logWarningTags
                                .value.trim(),

                        info:
                            elements.logInfoTags
                                .value.trim(),

                        debug:
                            elements.logDebugTags
                                .value.trim(),

                        disabled:
                            elements.logDisabledTags
                                .value.trim()
                    }
                },

                ui: {
                    animations_enabled:
                        elements.animations.checked,

                    theme:
                        elements.theme.value
                },

                network: {
                    wifi_ap: {
                        enabled:
                            elements.wifiApEnabled.checked,

                        ssid:
                            elements.wifiApSsid.value.trim(),

                        password:
                            elements.wifiApOpen.checked
                                ? ""
                                : elements.wifiApPassword.value
                    },

                    wifi_sta:
                        wifiSta,

                    usb_rndis: {
                        enabled:
                            elements.usbRndisEnabled.checked
                    }
                },

                can: {
                    primary: {
                        enabled:
                            elements.canPrimaryEnabled.checked,

                        bitrate:
                            Number(elements.canPrimaryBitrate.value),

                        listen_only:
                            elements.canPrimaryListenOnly.checked
                    },

                    secondary: {
                        enabled:
                            elements.canSecondaryEnabled.checked,

                        nominal_bitrate:
                            secondaryNominalBitrate,

                        data_bitrate:
                            secondaryDataBitrate,

                        fd_enabled:
                            secondaryFdEnabled,

                        brs_enabled:
                            secondaryBrsEnabled,

                        listen_only:
                            elements.canSecondaryListenOnly.checked
                    }
                }
            };
        }

        async function loadSettings() {
            setBusy(true);

            setStatus(
                "Loading settings..."
            );

            try {
                const settings =
                    await fetchJson(
                        "/api/settings",
                        {
                            cache: "no-store"
                        }
                    );

                populateSettings(settings);

                setStatus(
                    "Settings loaded",
                    "success"
                );

            } catch (error) {
                setStatus(
                    `Failed to load settings: ${error.message}`,
                    "error"
                );

            } finally {
                setBusy(false);
            }
        }

        async function saveSettings() {
            setBusy(true);

            setStatus(
                "Applying settings..."
            );

            try {
                validateSettings();

                await fetchJson(
                    "/api/settings",
                    {
                        method: "PUT",

                        headers: {
                            "Content-Type":
                                "application/json"
                        },

                        body: JSON.stringify(
                            createSettingsPayload()
                        )
                    }
                );

                setStatus(
                    "Saving settings..."
                );

                await fetchJson(
                    "/api/settings/save",
                    {
                        method: "POST"
                    }
                );

                await loadSettings();

                setStatus(
                    "Settings saved",
                    "success"
                );

            } catch (error) {
                setStatus(
                    `Failed to save settings: ${error.message}`,
                    "error"
                );

            } finally {
                setBusy(false);
            }
        }

        async function reloadSettings() {
            setBusy(true);

            setStatus(
                "Reloading saved settings..."
            );

            try {
                await fetchJson(
                    "/api/settings/reload",
                    {
                        method: "POST"
                    }
                );

                await loadSettings();

                setStatus(
                    "Saved settings reloaded",
                    "success"
                );

            } catch (error) {
                setStatus(
                    `Failed to reload settings: ${error.message}`,
                    "error"
                );

            } finally {
                setBusy(false);
            }
        }

        async function restartDevice() {
            if (operationBusy) {
                return;
            }
        
            const confirmed =
                window.confirm(
                    "Restart the device now?\n\n" +
                    "Network connections will be temporarily unavailable."
                );
        
            if (!confirmed) {
                return;
            }
        
            setBusy(true);
        
            if (scanPollTimer !== null) {
                window.clearTimeout(
                    scanPollTimer
                );

                scanPollTimer = null;
            }

            scanRunning = false;
        
            elements.restart.textContent =
                "Restarting...";
        
            setStatus(
                "Scheduling graceful device restart..."
            );
        
            try {
                await fetchJson(
                    "/api/system/restart",
                    {
                        method: "POST"
                    }
                );
        
                setStatus(
                    "Device is restarting. Waiting for it to return...",
                    "success"
                );
        
                /*
                 * Give the device time to stop its services, restart and
                 * initialize the web server again.
                 */
                window.setTimeout(
                    () => {
                        window.location.href = "/";
                    },
                    8000
                );
        
            } catch (error) {
                elements.restart.textContent =
                    "Restart device";
        
                setBusy(false);
        
                setStatus(
                    `Failed to restart device: ${error.message}`,
                    "error"
                );
            }
        }

        async function clearStaCredentials() {
            const confirmed =
                window.confirm(
                    "Clear the stored Wi-Fi Station credentials?"
                );

            if (!confirmed) {
                return;
            }

            setBusy(true);

            setStatus(
                "Clearing Station credentials..."
            );

            try {
                await fetchJson(
                    "/api/settings/wifi/sta/credentials",
                    {
                        method: "DELETE"
                    }
                );

                credentialsConfigured = false;
                loadedStaSsid = "";

                elements.wifiStaPassword.value = "";

                elements.wifiStaCredentialStatus.textContent =
                    "No stored credentials";

                elements.wifiStaCredentialStatus
                    .classList.remove(
                        "configured"
                    );

                setStatus(
                    "Station credentials cleared",
                    "success"
                );

            } catch (error) {
                setStatus(
                    `Failed to clear credentials: ${error.message}`,
                    "error"
                );

            } finally {
                setBusy(false);
            }
        }

        elements.brightness.addEventListener(
            "input",
            updateBrightness
        );

        elements.wifiApEnabled.addEventListener(
            "change",
            updateApControls
        );

        elements.wifiApOpen.addEventListener(
            "change",
            () => {
                if (elements.wifiApOpen.checked) {
                    elements.wifiApPassword.value = "";
                }

                updateApControls();
            }
        );

        elements.wifiStaEnabled.addEventListener(
            "change",
            updateStaControls
        );

        elements.wifiStaOpen.addEventListener(
            "change",
            () => {
                if (elements.wifiStaOpen.checked) {
                    elements.wifiStaPassword.value = "";
                }

                updateStaControls();
            }
        );

        elements.canPrimaryEnabled.addEventListener(
            "change",
            updateCanControls
        );

        elements.canSecondaryEnabled.addEventListener(
            "change",
            updateCanControls
        );

        elements.canSecondaryNominalBitrate.addEventListener(
            "change",
            updateCanControls
        );

        elements.canSecondaryFdEnabled.addEventListener(
            "change",
            updateCanControls
        );

        elements.canSecondaryBrsEnabled.addEventListener(
            "change",
            updateCanControls
        );

        document
            .getElementById("show-ap-password")
            .addEventListener(
                "click",
                event => {
                    togglePassword(
                        elements.wifiApPassword,
                        event.currentTarget
                    );
                }
            );

        document
            .getElementById("show-sta-password")
            .addEventListener(
                "click",
                event => {
                    togglePassword(
                        elements.wifiStaPassword,
                        event.currentTarget
                    );
                }
            );

        elements.clearStaCredentials.addEventListener(
            "click",
            clearStaCredentials
        );

        elements.restart.addEventListener("click", restartDevice);

        elements.reload.addEventListener(
            "click",
            reloadSettings
        );

        elements.save.addEventListener(
            "click",
            saveSettings
        );

        elements.scanWifi.addEventListener(
            "click",
            startWifiScan
        );

        window.addEventListener(
            "beforeunload",
            () => {
                if (scanPollTimer !== null) {
                    window.clearTimeout(
                        scanPollTimer
                    );

                    scanPollTimer = null;
                }
            }
        );

        loadSettings();
        loadWifiScanStatus();
    
    }

    // ===== files.html =====
    function init_files() {
        const FILE_LIST_LIMIT = 16;

        const volumeSelector = document.getElementById("volume");

        const currentPathElement = document.getElementById("current-path");

        const fileList = document.getElementById("file-list");

        const statusElement = document.getElementById("status");

        const backButton = document.getElementById("back");

        const previousPageButton = document.getElementById("previous-page");

        const nextPageButton = document.getElementById("next-page");

        const pageStatusElement = document.getElementById("page-status");

        let currentPath = "/";
        let currentOffset = 0;
        let hasMore = false;
        let activeRequest = null;

        function setStatus(message, error = false) {
            statusElement.textContent = message;

            statusElement.classList.toggle("error", error);
        }

        function joinPath(base, name) {
            if (base === "/") {
                return `/${name}`;
            }

            return `${base}/${name}`;
        }

        function getParentPath(path) {
            if (path === "/") {
                return "/";
            }

            const parts = path.split("/").filter(Boolean);

            parts.pop();

            return parts.length === 0 ? "/" : `/${parts.join("/")}`;
        }

        function formatSize(size) {
            if (!Number.isFinite(size) || size < 0) {

                return "-";
            }

            if (size < 1024) {
                return `${size} B`;
            }

            if (size < 1024 * 1024) {
                return `${(size / 1024).toFixed(1)} KB`;
            }

            if (size < 1024 * 1024 * 1024) {
                return `${(size / (1024 * 1024)).toFixed(1)} MB`;
            }

            return `${(size / (1024 * 1024 * 1024)).toFixed(1)} GB`;
        }

        function updateNavigation() {
            backButton.disabled = currentPath === "/";

            previousPageButton.disabled = currentOffset === 0;

            nextPageButton.disabled = !hasMore;

            const entryCount = fileList.children.length;

            if (entryCount === 0) {
                pageStatusElement.textContent = "No entries";

                return;
            }

            const firstEntry = currentOffset + 1;

            const lastEntry = currentOffset + entryCount;

            pageStatusElement.textContent = `${firstEntry}-${lastEntry}`;
        }

        function createFileRow(volume, entry) {
            const row = document.createElement("tr");

            const nameCell = document.createElement("td");

            const typeCell = document.createElement("td");

            const sizeCell = document.createElement("td");

            const actionCell = document.createElement("td");

            nameCell.textContent = entry.name;

            nameCell.title = entry.name;

            typeCell.textContent = entry.type;

            sizeCell.textContent = entry.type === "directory" ? "-" : formatSize(entry.size);

            const button = document.createElement("button");

            button.type = "button";

            if (entry.type === "directory") {
                button.textContent = "Open";

                button.addEventListener("click", () => {
                    currentPath = joinPath(currentPath, entry.name);

                    currentOffset = 0;
                    hasMore = false;

                    loadFiles();
                });

            } else {
                button.textContent = "Download";

                button.addEventListener("click", () => {
                    const filePath = joinPath(currentPath, entry.name);

                    const query = new URLSearchParams({volume, path : filePath});

                    window.location.href = `/api/files/download?` + query.toString();
                });
            }

            actionCell.appendChild(button);

            row.append(nameCell, typeCell, sizeCell, actionCell);

            return row;
        }

        async function loadFiles() {
            if (activeRequest !== null) {
                activeRequest.abort();
            }

            const request = new AbortController();

            activeRequest = request;

            const volume = volumeSelector.value;

            setStatus("Loading...");

            backButton.disabled = true;
            previousPageButton.disabled = true;
            nextPageButton.disabled = true;

            try {
                const query = new URLSearchParams({
                    volume,
                    path : currentPath,
                    offset : String(currentOffset),
                    limit : String(FILE_LIST_LIMIT)
                });

                const response = await fetch(`/api/files?${query.toString()}`,
                                             {cache : "no-store", signal : request.signal});

                let result;

                try {
                    result = await response.json();

                } catch {
                    throw new Error(`Invalid server response: HTTP ${response.status}`);
                }

                if (!response.ok) {
                    throw new Error(result.message || `HTTP ${response.status}`);
                }

                if ((typeof result.path !== "string") || !Array.isArray(result.entries)) {

                    throw new Error("Invalid file-list response");
                }

                /*
                 * Ignore a response belonging to an older request.
                 */
                if (activeRequest !== request) {
                    return;
                }

                fileList.replaceChildren();

                currentPath = result.path;

                currentPathElement.textContent = currentPath;

                currentPathElement.title = currentPath;

                hasMore = result.has_more === true;

                for (const entry of result.entries) {
                    if ((typeof entry !== "object") || (entry === null) ||
                        (typeof entry.name !== "string") ||
                        ((entry.type !== "file") && (entry.type !== "directory"))) {

                        continue;
                    }

                    const row = createFileRow(volume, entry);

                    fileList.appendChild(row);
                }

                const count = fileList.children.length;

                setStatus(count === 1 ? "1 entry" : `${count} entries`);

                updateNavigation();

            } catch (error) {
                if (error.name === "AbortError") {
                    return;
                }

                if (activeRequest === request) {
                    fileList.replaceChildren();

                    hasMore = false;

                    const message = error instanceof Error ? error.message : String(error);

                    setStatus(`Failed to load files: ${message}`, true);

                    updateNavigation();
                }

            } finally {
                if (activeRequest === request) {
                    activeRequest = null;
                }
            }
        }

        backButton.addEventListener("click", () => {
            if (currentPath === "/") {
                return;
            }

            currentPath = getParentPath(currentPath);

            currentOffset = 0;
            hasMore = false;

            loadFiles();
        });

        previousPageButton.addEventListener("click", () => {
            if (currentOffset === 0) {
                return;
            }

            currentOffset = currentOffset >= FILE_LIST_LIMIT ? currentOffset - FILE_LIST_LIMIT : 0;

            hasMore = false;

            loadFiles();
        });

        nextPageButton.addEventListener("click", () => {
            if (!hasMore) {
                return;
            }

            currentOffset += FILE_LIST_LIMIT;

            hasMore = false;

            loadFiles();
        });

        volumeSelector.addEventListener("change", () => {
            currentPath = "/";
            currentOffset = 0;
            hasMore = false;

            currentPathElement.textContent = currentPath;

            currentPathElement.title = currentPath;

            loadFiles();
        });

        loadFiles();


        async function loadDeviceTitle() {
            const title =
                document.getElementById(
                    "device-title"
                );

            try {
                const response =
                    await fetch(
                        "/api/system",
                        {
                            cache: "no-store"
                        }
                    );

                if (!response.ok) {
                    return;
                }

                const system =
                    await response.json();

                title.textContent =
                    system.device_id ||
                    "Spectra";

            } catch (error) {
                console.error(
                    "Failed to load device title:",
                    error
                );
            }
        }

        loadDeviceTitle();
    
    }

    // ===== can_test.html =====
    function init_test() {

        const BATCH_HEADER_SIZE = 8;
        const EVENT_HEADER_SIZE = 40;

        let socket = null;
        let lastSequence = null;

        let receivedBatches = 0;
        let receivedEvents = 0;

        const statisticsElements = {
            queued:
                document.getElementById("statistics-queued"),

            sent:
                document.getElementById("statistics-sent"),

            dropped:
                document.getElementById("statistics-dropped"),

            failures:
                document.getElementById("statistics-failures"),

            queue:
                document.getElementById("statistics-queue"),

            queuePeak:
                document.getElementById("statistics-queue-peak"),

            sentBatches:
                document.getElementById(
                    "statistics-sent-batches"
                ),

            binaryBytes:
                document.getElementById(
                    "statistics-binary-bytes"
                ),

            batchEventsTotal:
                document.getElementById(
                    "statistics-batch-events-total"
                ),

            batchAverage:
                document.getElementById(
                    "statistics-batch-average"
                ),

            batchPeak:
                document.getElementById(
                    "statistics-batch-peak"
                ),

            batches:
                document.getElementById("statistics-batches"),

            events:
                document.getElementById("statistics-events"),

            filtered:
                document.getElementById("statistics-filtered"),
        };

        function formatByteSize(value) {
            const bytes =
                Number(value ?? 0);

            if (!Number.isFinite(bytes) ||
                (bytes <= 0)) {

                return "0 B";
            }

            if (bytes < 1024) {
                return `${bytes} B`;
            }

            if (bytes < (1024 * 1024)) {
                return `${(bytes / 1024).toFixed(1)} KB`;
            }

            return (
                `${(bytes / (1024 * 1024)).toFixed(2)} MB`
            );
        }

        function updateStreamStatistics(statistics) {
            statisticsElements.queued.textContent =
                statistics.queued_events ?? 0;

            statisticsElements.sent.textContent =
                statistics.sent_events ?? 0;

            statisticsElements.dropped.textContent =
                statistics.dropped_events ?? 0;

            statisticsElements.failures.textContent =
                statistics.send_failures ?? 0;

            statisticsElements.queue.textContent =
                `${statistics.queue_current ?? 0} / ` +
                `${statistics.queue_capacity ?? 0}`;

            statisticsElements.queuePeak.textContent =
                statistics.queue_peak ?? 0;

            statisticsElements.sentBatches.textContent =
                statistics.sent_batches ?? 0;

            statisticsElements.binaryBytes.textContent =
                formatByteSize(
                    statistics.sent_binary_bytes
                );

            statisticsElements.batchEventsTotal.textContent =
                statistics.batch_events_total ?? 0;

            statisticsElements.batchAverage.textContent =
                statistics.batch_events_average ?? 0;

            statisticsElements.batchPeak.textContent =
                statistics.batch_events_peak ?? 0;

            statisticsElements.dropped.classList.toggle(
                "warning",
                Number(statistics.dropped_events) > 0
            );

            statisticsElements.failures.classList.toggle(
                "error",
                Number(statistics.send_failures) > 0
            );

            statisticsElements.filtered.textContent =
                statistics.filtered_events ?? 0;

            const batchAverage =
                Number(
                    statistics.batch_events_average ?? 0
                );

            statisticsElements.batchAverage.classList.toggle(
                "warning",
                (batchAverage > 0) &&
                (batchAverage < 32)
            );
        }

        const status =
            document.getElementById("status");

        const log =
            document.getElementById("log");

        const LOG_LINE_LIMIT = 1000;
        const LOG_FLUSH_INTERVAL_MS = 50;

        const logLines = [];
        const pendingLogLines = [];

        let logFlushTimer = null;

        function flushLog() {
            logFlushTimer = null;

            if (pendingLogLines.length === 0) {
                return;
            }

            for (const line of pendingLogLines) {
                logLines.push(
                    line
                );
            }

            pendingLogLines.length = 0;

            if (logLines.length >
                LOG_LINE_LIMIT) {

                logLines.splice(
                    0,
                    logLines.length -
                        LOG_LINE_LIMIT
                );
            }

            log.textContent =
                `${logLines.join("\n")}\n`;

            log.scrollTop =
                log.scrollHeight;

            statisticsElements.batches.textContent =
                receivedBatches;

            statisticsElements.events.textContent =
                receivedEvents;
        }

        function appendLogBatch(messages) {
            if ((messages === null) ||
                (messages.length === 0)) {

                return;
            }

            for (const message of messages) {
                pendingLogLines.push(
                    message
                );
            }

            if (logFlushTimer === null) {
                logFlushTimer =
                    setTimeout(
                        flushLog,
                        LOG_FLUSH_INTERVAL_MS
                    );
            }
        }

        function appendLog(message) {
            appendLogBatch(
                [message]
            );
        }

        function eventName(value) {
            return [
                "RX",
                "TX_QUEUED",
                "TX_COMPLETED",
                "TX_FAILED",
                "TX_ABORTED"
            ][value] ?? `UNKNOWN(${value})`;
        }

        function busName(value) {
            return value === 0
                ? "Primary"
                : value === 1
                    ? "Secondary"
                    : `Bus(${value})`;
        }

        function timestampSourceName(value) {
            return [
                "none",
                "software",
                "hardware"
            ][value] ?? `unknown(${value})`;
        }

        function hex(value, width) {
            return value
                .toString(16)
                .toUpperCase()
                .padStart(width, "0");
        }

        function parseEvent(
            view,
            offset,
            lines
        ) {
            if ((offset + EVENT_HEADER_SIZE) >
                view.byteLength) {

                throw new Error("Truncated event header");
            }

            const type =
                view.getUint8(offset + 0);

            const bus =
                view.getUint8(offset + 1);

            const direction =
                view.getUint8(offset + 2);

            const flags =
                view.getUint8(offset + 3);

            const dlc =
                view.getUint8(offset + 4);

            const dataLength =
                view.getUint8(offset + 5);

            const timestampSource =
                view.getUint8(offset + 6);

            const eventSequence =
                view.getUint32(
                    offset + 8,
                    true
                );

            const transactionId =
                view.getUint32(
                    offset + 12,
                    true
                );

            const nativeSequence =
                view.getUint32(
                    offset + 16,
                    true
                );

            const identifier =
                view.getUint32(
                    offset + 20,
                    true
                );

            const result =
                view.getInt32(
                    offset + 24,
                    true
                );

            const timestampUs =
                view.getBigUint64(
                    offset + 28,
                    true
                );

            if (dataLength > 64) {
                throw new Error(
                    `Invalid payload length ${dataLength}`
                );
            }

            const eventSize =
                EVENT_HEADER_SIZE +
                dataLength;

            if ((offset + eventSize) >
                view.byteLength) {

                throw new Error("Truncated event payload");
            }

            const data =
                new Uint8Array(
                    view.buffer,
                    view.byteOffset +
                        offset +
                        EVENT_HEADER_SIZE,
                    dataLength
                );

            const dataText =
                Array.from(data)
                    .map(byte => hex(byte, 2))
                    .join(" ");

            if (lastSequence !== null) {
                const expected =
                    (lastSequence + 1) >>> 0;

                if (eventSequence !== expected) {
                    lines.push(
                        `WARNING: sequence gap, expected ` +
                        `${expected}, received ${eventSequence}`
                    );
                }
            }

            lastSequence =
                eventSequence;

            const extended =
                (flags & (1 << 0)) !== 0;

            const remote =
                (flags & (1 << 1)) !== 0;

            const fd =
                (flags & (1 << 2)) !== 0;

            const brs =
                (flags & (1 << 3)) !== 0;

            const esi =
                (flags & (1 << 4)) !== 0;

            lines.push(
                `${eventSequence} ` +
                `${eventName(type)} ` +
                `${busName(bus)} ` +
                `${direction === 0 ? "RX" : "TX"} ` +
                `ID=${hex(identifier, extended ? 8 : 3)} ` +
                `DLC=${dlc} LEN=${dataLength} ` +
                `FD=${fd} BRS=${brs} RTR=${remote} ESI=${esi} ` +
                `TXID=${transactionId} NATIVE=${nativeSequence} ` +
                `RESULT=${result} ` +
                `TS=${timestampUs}(${timestampSourceName(timestampSource)}) ` +
                `[${dataText}]`
            );

            return eventSize;
        }

        function parseBatch(buffer) {
            const batchLines = [];

            const view =
                new DataView(buffer);

            if (view.byteLength <
                BATCH_HEADER_SIZE) {

                throw new Error("Truncated batch header");
            }

            const version =
                view.getUint8(0);

            const messageType =
                view.getUint8(1);

            const eventCount =
                view.getUint16(
                    2,
                    true
                );

            const payloadSize =
                view.getUint32(
                    4,
                    true
                );

            if (version !== 1) {
                throw new Error(
                    `Unsupported protocol version ${version}`
                );
            }

            if (messageType !== 1) {
                throw new Error(
                    `Unsupported binary message type ${messageType}`
                );
            }

            if ((BATCH_HEADER_SIZE + payloadSize) !==
                view.byteLength) {

                throw new Error(
                    "Batch payload size mismatch"
                );
            }

            let offset =
                BATCH_HEADER_SIZE;

            for (let index = 0;
                 index < eventCount;
                 ++index) {

                offset +=
                    parseEvent(
                        view,
                        offset,
                        batchLines
                    );
            }

            if (offset !== view.byteLength) {
                throw new Error(
                    "Unexpected trailing batch data"
                );
            }

            appendLogBatch(
                batchLines
            );

            receivedBatches += 1;
            receivedEvents += eventCount;

        }

        function connect() {
            if (socket !== null) {
                return;
            }

            status.textContent = "Connecting";
            status.classList.remove("paused");

            const scheme =
                location.protocol === "https:"
                    ? "wss:"
                    : "ws:";

            socket =
                new WebSocket(
                    `${scheme}//${location.host}/ws/can`
                );

            socket.binaryType =
                "arraybuffer";

            socket.onopen =
                () => {
                    status.textContent =
                        "Connected";

                    status.classList.remove("paused");

                    appendLog(
                        "WebSocket connected"
                    );

                    /*
                     * Activate server-side CAN streaming and verify the
                     * bidirectional WebSocket connection.
                     */
                    socket.send(
                        JSON.stringify({
                            command: "subscribe",
                            primary: true,
                            secondary: true,
                            rx: true,
                            tx: true,
                            paused: false
                        })
                    );
                };

            socket.onmessage =
                event => {
                    try {
                        if (typeof event.data === "string") {
                            const message =
                                JSON.parse(
                                    event.data
                                );

                            if (message.type ===
                                "stream_statistics") {

                                updateStreamStatistics(
                                    message
                                );

                            } else if (message.type ===
                                       "subscription") {

                                status.textContent =
                                    message.paused
                                        ? "Paused"
                                        : "Connected";

                                status.classList.toggle(
                                    "paused",
                                    message.paused
                                );

                                appendLog(
                                    `Subscription updated: ${event.data}`
                                );

                            } else {
                                appendLog(
                                    `JSON: ${event.data}`
                                );
                            }

                            return;
                        }

                        parseBatch(
                            event.data
                        );

                    } catch (error) {
                        appendLog(
                            `PARSE ERROR: ${error.message}`
                        );
                    }
                };

            socket.onerror =
                () => {
                    status.textContent = "Error";
                    status.classList.remove("paused");

                    appendLog(
                        "WebSocket error"
                    );
                };

            socket.onclose =
                event => {
                    status.textContent =
                        `Disconnected (${event.code})`;

                    status.classList.remove("paused");

                    appendLog(
                        `WebSocket closed: ${event.code}`
                    );

                    socket = null;
                    lastSequence = null;
                };
        }

        function disconnect() {
            if (socket !== null) {
                socket.close(
                    1000,
                    "Test completed"
                );
            }
        }

        document
            .getElementById("connect")
            .addEventListener(
                "click",
                connect
            );

        document
            .getElementById("disconnect")
            .addEventListener(
                "click",
                disconnect
            );

        document
            .getElementById("clear")
            .addEventListener(
                "click",
                () => {
                    if (logFlushTimer !== null) {
                        clearTimeout(
                            logFlushTimer
                        );

                        logFlushTimer = null;
                    }

                    pendingLogLines.length = 0;
                    logLines.length = 0;
                    log.textContent = "";

                    lastSequence = null;
                    receivedBatches = 0;
                    receivedEvents = 0;

                    statisticsElements.batches.textContent =
                        "0";

                    statisticsElements.events.textContent =
                        "0";
                }
            );
    
    }

    // ===== can_logger.html =====
    function init_logger() {

        "use strict";

        const element = id => document.getElementById(id);
        const eventNames = ["RX", "TX queued", "TX done", "TX failed", "TX aborted"];
        const channels = [0, 1].map(() => ({
            identifiers: new Map(), history: [], rx: 0, tx: 0,
            previousRx: 0, previousTx: 0, dirty: true
        }));
        let socket = null;
        let paused = false;
        let pending = false;
        let received = 0;
        let previousRateTime = performance.now();
        let subscriptionTimer = null;

        for (let bus = 0; bus < 2; bus++) {
            const section = document.createElement("section");
            section.className = "channel " + (bus ? "secondary" : "");
            section.innerHTML = '<div class="channel-title"><span class="tag">CH ' + (bus + 1) +
                '</span><h2>' + (bus ? "Secondary CAN" : "Primary CAN") +
                '</h2><span class="rate" id="rate' + bus + '">RX 0/s · TX 0/s</span></div>' +
                '<div class="tables"><div class="panel"><div class="panel-title">Identifiers<span id="ids' + bus +
                '">0 IDs</span></div><div class="scroll"><table><thead><tr><th>ID / type</th><th>Count</th><th>Data</th><th>Δ ms</th></tr></thead><tbody id="idrows' + bus +
                '"></tbody></table></div></div><div class="panel"><div class="panel-title">Event buffer<span id="buffer' + bus +
                '">0 events</span></div><div class="scroll"><table><thead><tr><th>No.</th><th>Time / source</th><th>Event</th><th>ID</th><th>Data</th></tr></thead><tbody id="history' + bus +
                '"></tbody></table></div></div></div>';
            element("channels").append(section);
        }

        function hex(value, width = 2) {
            return value.toString(16).toUpperCase().padStart(width, "0");
        }

        function identifier(event) {
            return hex(event.id, event.flags & 1 ? 8 : 3);
        }

        function timeText(event) {
            return event.source === 0 ? "—" :
                (event.timestamp / 1000000n).toString() + "." +
                (event.timestamp % 1000000n).toString().padStart(6, "0");
        }

        function parseBatch(buffer) {
            const view = new DataView(buffer);
            if (view.byteLength < 8 || view.getUint8(0) !== 1 ||
                view.getUint8(1) !== 1 || view.getUint32(4, true) !== view.byteLength - 8) {
                throw new Error("Invalid CAN batch header");
            }
            const events = [];
            let offset = 8;
            for (let i = 0; i < view.getUint16(2, true); i++) {
                if (offset + 40 > view.byteLength)
                    throw new Error("Truncated CAN event");
                const length = view.getUint8(offset + 5);
                const type = view.getUint8(offset);
                const bus = view.getUint8(offset + 1);
                const source = view.getUint8(offset + 6);
                if (length > 64 || bus > 1 || type > 4 || source > 2 ||
                    offset + 40 + length > view.byteLength)
                    throw new Error("Invalid CAN event");
                events.push({
                    type, bus, source, flags: view.getUint8(offset + 3),
                    sequence: view.getUint32(offset + 8, true),
                    id: view.getUint32(offset + 20, true),
                    timestamp: view.getBigUint64(offset + 28, true),
                    data: Array.from(new Uint8Array(buffer, offset + 40, length))
                });
                offset += 40 + length;
            }
            if (offset !== view.byteLength)
                throw new Error("Unexpected trailing CAN data");
            return events;
        }

        function acceptEvent(event) {
            const channel = channels[event.bus];
            received++;
            channel.history.push(event);
            if (channel.history.length > 500)
                channel.history.shift();
            // Count actual RX and completed TX, not every TX lifecycle event.
            if (event.type === 0 || event.type === 2) {
                if (event.type === 0)
                    channel.rx++;
                else
                    channel.tx++;
                const key = event.id + ":" + (event.flags & 7);
                let row = channel.identifiers.get(key);
                if (!row && channel.identifiers.size >= 256) {
                    channel.identifiers.delete(channel.identifiers.keys().next().value);
                }
                const delta = row && event.source !== 0 && event.source === row.event.source &&
                    event.timestamp >= row.event.timestamp ?
                    Number(event.timestamp - row.event.timestamp) / 1000 : null;
                const changed = event.data.map((value, i) => Boolean(row && row.event.data[i] !== value));
                channel.identifiers.set(key, {
                    event, count: (row ? row.count : 0) + 1,
                    delta, changed, changedAt: performance.now()
                });
            }
            channel.dirty = true;
        }

        function payload(event, row) {
            if (event.flags & 2)
                return "RTR";
            return event.data.map((byte, i) => '<span class="byte' +
                (row && row.changed[i] && performance.now() - row.changedAt < 600 ? " changed" : "") +
                '">' + hex(byte) + "</span>").join(" ");
        }

        function render() {
            const query = element("search").value.trim().toUpperCase().replace(/^0X/, "");
            channels.forEach((channel, bus) => {
                if (!channel.dirty && !Array.from(channel.identifiers.values()).some(row =>
                    performance.now() - row.changedAt < 1000)) return;
                channel.dirty = false;
                const rows = Array.from(channel.identifiers.values()).filter(row => identifier(row.event).includes(query));
                element("idrows" + bus).innerHTML = rows.map(row =>
                    "<tr><td>" + identifier(row.event) + (row.event.flags & 4 ? " FD" : "") +
                    "</td><td>" + row.count + "</td><td>" + payload(row.event, row) +
                    "</td><td>" + (row.delta === null ? "—" : row.delta.toFixed(1)) + "</td></tr>"
                ).join("") || '<tr><td class="empty" colspan="4">No matching frames received</td></tr>';
                const history = channel.history.filter(event => identifier(event).includes(query));
                element("history" + bus).innerHTML = history.slice().reverse().map(event =>
                    "<tr><td>" + event.sequence + "</td><td>" + timeText(event) +
                    (event.source === 1 ? " SW" : event.source === 2 ? " HW" : "") +
                    "</td><td>" + eventNames[event.type] + "</td><td>" + identifier(event) +
                    "</td><td>" + payload(event) + "</td></tr>"
                ).join("") || '<tr><td class="empty" colspan="5">Connect to start receiving CAN events</td></tr>';
                element("ids" + bus).textContent = channel.identifiers.size + " IDs";
                element("buffer" + bus).textContent = channel.history.length + " events";
            });
            element("received").textContent = received.toLocaleString();
        }

        function updateControls() {
            const connected = socket && socket.readyState === WebSocket.OPEN;
            element("connect").textContent = socket ? "Disconnect" : "Connect";
            element("apply").disabled = !connected || pending;
            element("pause").disabled = !connected || pending;
            element("pause").textContent = paused ? "Resume stream" : "Pause stream";
            element("status").textContent = pending ? "● Applying…" : connected ?
                (paused ? "● Paused" : "● Connected") : socket ? "● Connecting…" : "● Disconnected";
            element("status").classList.toggle("live", Boolean(connected && !paused && !pending));
        }

        function subscribe(nextPaused) {
            if (!socket || socket.readyState !== WebSocket.OPEN || pending)
                return;
            socket.send(JSON.stringify({
                command: "subscribe", primary: element("primary").checked,
                secondary: element("secondary").checked, rx: element("rx").checked,
                tx: element("tx").checked, paused: nextPaused
            }));
            pending = true;
            subscriptionTimer = setTimeout(() => {
                element("message").textContent = "Subscription confirmation timed out. Reconnect to retry.";
                if (socket) socket.close();
            }, 5000);
            updateControls();
        }

        element("connect").onclick = () => {
            if (socket) { socket.close(); return; }
            element("message").textContent = "";
            const connection = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws/can");
            socket = connection;
            connection.binaryType = "arraybuffer";
            updateControls();
            connection.onopen = () => { paused = false; subscribe(false); };
            connection.onmessage = event => {
                if (socket !== connection) return;
                try {
                    if (typeof event.data !== "string") {
                        parseBatch(event.data).forEach(acceptEvent);
                        return;
                    }
                    const message = JSON.parse(event.data);
                    if (message.type === "subscription") {
                        clearTimeout(subscriptionTimer);
                        pending = false;
                        paused = message.paused;
                        element("message").textContent = "";
                        updateControls();
                    } else if (message.type === "stream_statistics") {
                        element("dropped").textContent = message.dropped_events ?? 0;
                        element("queue").textContent = (message.queue_current ?? 0) + " / " + (message.queue_capacity ?? 0);
                    } else if (message.type === "error") {
                        clearTimeout(subscriptionTimer);
                        pending = false;
                        element("message").textContent = message.code || "Server rejected the command";
                        updateControls();
                    }
                } catch (error) {
                    element("message").textContent = error.message;
                }
            };
            connection.onerror = () => { element("message").textContent = "WebSocket connection error"; };
            connection.onclose = event => {
                if (socket !== connection) return;
                clearTimeout(subscriptionTimer);
                socket = null; pending = false; paused = false;
                updateControls();
                if (event.code !== 1000) element("message").textContent = "Connection closed (" + event.code + "). Click Connect to retry.";
            };
        };
        element("apply").onclick = () => subscribe(paused);
        element("pause").onclick = () => subscribe(!paused);
        element("search").oninput = () => channels.forEach(channel => { channel.dirty = true; });
        element("clear").onclick = () => {
            channels.forEach(channel => {
                channel.identifiers.clear(); channel.history.length = 0;
                channel.rx = channel.tx = channel.previousRx = channel.previousTx = 0;
                channel.dirty = true;
            });
            received = 0;
        };
        element("export").onclick = () => {
            const rows = ["bus,sequence,timestamp_us,timestamp_source,event,id,flags,data"];
            channels.forEach((channel, bus) => channel.history.forEach(event => rows.push([
                bus ? "Secondary" : "Primary", event.sequence, event.timestamp.toString(),
                event.source, eventNames[event.type], identifier(event), event.flags,
                event.data.map(byte => hex(byte)).join(" ")
            ].join(","))));
            const url = URL.createObjectURL(new Blob([rows.join("\r\n")], { type: "text/csv" }));
            const link = document.createElement("a");
            link.href = url; link.download = "spectra-can-buffer.csv"; link.click();
            setTimeout(() => URL.revokeObjectURL(url), 1000);
        };
        setInterval(() => { if (!document.hidden) render(); }, 250);
        setInterval(() => {
            const now = performance.now();
            const elapsed = (now - previousRateTime) / 1000;
            channels.forEach((channel, bus) => {
                element("rate" + bus).textContent = "RX " + Math.round((channel.rx - channel.previousRx) / elapsed) +
                    "/s · TX " + Math.round((channel.tx - channel.previousTx) / elapsed) + "/s";
                channel.previousRx = channel.rx; channel.previousTx = channel.tx;
            });
            previousRateTime = now;
        }, 1000);
        render();
    
    }

    // ===== can_analyzer.html =====
    function init_analyzer() {

        "use strict";
        const $ = id => document.getElementById(id);
        const LIMIT = 100000, PAGE = 100;
        const names = ["RX", "TX queued", "TX done", "TX failed", "TX aborted"];
        let records = [], selected = new Set(), groups = [], filtered = [];
        let socket = null, paused = false, pending = false, ackTimer = null;
        let page = 0, dirty = true, busy = false, source = "No source";
        const hex = (n, width = 2) => n.toString(16).toUpperCase().padStart(width, "0");
        const key = e => e.bus + ":" + e.id + ":" + (e.flags & 7);
        const idText = e => hex(e.id, e.flags & 1 ? 8 : 3);
        const timeText = us => (us / 1000000n).toString() + "." + (us % 1000000n).toString().padStart(6, "0");
        const dataText = e => e.flags & 2 ? "RTR" : e.data.map(b => hex(b)).join(" ");
        const formatText = e => (e.flags & 1 ? " EXT" : " STD") + (e.flags & 4 ? " FD" : "") + (e.flags & 2 ? " RTR" : "");
        function validate(e) {
            if (e.bus > 1 || e.type > 4 || e.source > 2 || e.data.length > 64 ||
                e.id > (e.flags & 1 ? 0x1fffffff : 0x7ff))
                throw new Error("Invalid CAN event fields");
            return e;
        }
        function parseLive(buffer) {
            const v = new DataView(buffer), result = [];
            if (v.byteLength < 8 || v.getUint8(0) !== 1 || v.getUint8(1) !== 1 ||
                v.getUint32(4, true) !== v.byteLength - 8)
                throw new Error("Invalid WebSocket batch");
            let o = 8;
            for (let i = 0; i < v.getUint16(2, true); i++) {
                if (o + 40 > v.byteLength)
                    throw new Error("Truncated event");
                const n = v.getUint8(o + 5);
                if (n > 64 || o + 40 + n > v.byteLength)
                    throw new Error("Truncated payload");
                result.push(validate({
                    type: v.getUint8(o), bus: v.getUint8(o + 1), flags: v.getUint8(o + 3),
                    source: v.getUint8(o + 6), id: v.getUint32(o + 20, true),
                    sequence: v.getUint32(o + 8, true), timestamp: v.getBigUint64(o + 28, true),
                    capture: null, data: Array.from(new Uint8Array(buffer, o + 40, n))
                }));
                o += 40 + n;
            }
            if (o !== v.byteLength)
                throw new Error("Trailing batch data");
            return result;
        }
        async function parseScl(buffer) {
            const v = new DataView(buffer), result = [];
            if (v.byteLength < 32 || v.getUint32(0, true) !== 0x314c4353 ||
                v.getUint16(4, true) !== 1 || v.getUint16(6, true) !== 32 ||
                v.getUint32(8, true) !== 0x12345678 || v.getUint32(12, true) !== 0)
                throw new Error("Unsupported SCL header");
            let o = 32;
            while (o < v.byteLength && result.length < LIMIT) {
                if (o + 56 > v.byteLength)
                    throw new Error("Truncated SCL record at byte " + o);
                const size = v.getUint16(o, true), n = v.getUint8(o + 8);
                if (v.getUint8(o + 2) !== 1 || n > 64 || size !== 56 + n ||
                    o + size > v.byteLength)
                    throw new Error("Invalid SCL record at byte " + o);
                result.push(validate({
                    type: v.getUint8(o + 3), bus: v.getUint8(o + 4), source: v.getUint8(o + 6),
                    flags: v.getUint32(o + 12, true), id: v.getUint32(o + 16, true),
                    sequence: v.getUint32(o + 20, true), timestamp: v.getBigUint64(o + 40, true),
                    capture: v.getBigUint64(o + 48, true),
                    data: Array.from(new Uint8Array(buffer, o + 56, n))
                }));
                o += size;
                if (result.length % 2000 === 0)
                    await new Promise(resolve => setTimeout(resolve, 0));
            }
            return { records: result, truncated: o < v.byteLength };
        }
        function closeLive() {
            clearTimeout(ackTimer);
            if (socket) {
                const old = socket;
                socket = null;
                old.close();
            }
            pending = false;
            paused = false;
            controls();
        }
        function controls() {
            $("connect").disabled = busy;
            $("file").disabled = busy;
            $("connect").textContent = socket ? "Disconnect" : "Connect live";
            $("pause").disabled = !socket || socket.readyState !== 1 || pending || records.length >= LIMIT;
            $("pause").textContent = paused ? "Resume stream" : "Pause stream";
            $("status").textContent = busy ? "Loading file…" : socket ?
                (socket.readyState !== 1 ? "Connecting…" : pending ? "Applying…" : paused ? "Paused" : "Live") : "Offline";
        }
        function subscribe(next) {
            if (!socket || socket.readyState !== 1 || pending)
                return;
            socket.send(JSON.stringify({ command: "subscribe", primary: true, secondary: true, rx: true, tx: true, paused: next }));
            pending = true;
            controls();
            ackTimer = setTimeout(() => { closeLive(); $("message").textContent = "Subscription timed out. Reconnect to retry."; }, 5000);
        }
        $("connect").onclick = () => {
            if (socket) { closeLive(); return; }
            records = []; selected.clear(); page = 0; dirty = true; source = "Live stream";
            $("frame").textContent = "Select an event row to inspect its bytes.";
            $("detail").textContent = "";
            $("message").textContent = "";
            const ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws/can");
            socket = ws; ws.binaryType = "arraybuffer"; controls();
            ws.onopen = () => { if (socket === ws) subscribe(false); };
            ws.onmessage = event => {
                if (socket !== ws) return;
                try {
                    if (typeof event.data === "string") {
                        const msg = JSON.parse(event.data);
                        if (msg.type === "subscription") {
                            clearTimeout(ackTimer); pending = false; paused = msg.paused; controls();
                            if (records.length >= LIMIT && !paused) subscribe(true);
                        } else if (msg.type === "error") {
                            closeLive(); $("message").textContent = msg.code || "Server command failed";
                        } else if (msg.type === "stream_statistics" && msg.dropped_events > 0) {
                            $("message").textContent = "Server reports dropped events: " + msg.dropped_events;
                        }
                        return;
                    }
                    const incoming = parseLive(event.data);
                    for (const e of incoming) { if (records.length >= LIMIT) break; records.push(e); }
                    dirty = true;
                    if (records.length >= LIMIT) {
                        subscribe(true);
                        $("message").textContent = "100,000-event limit reached. Capture paused; later events are not retained.";
                    }
                } catch (error) { $("message").textContent = error.message; }
            };
            ws.onclose = () => { if (socket === ws) closeLive(); };
            ws.onerror = () => { $("message").textContent = "WebSocket connection failed"; };
        };
        $("pause").onclick = () => subscribe(!paused);
        $("file").onchange = async () => {
            const file = $("file").files[0]; if (!file) return;
            closeLive();
            if (file.size > 64 * 1024 * 1024) { $("message").textContent = "File exceeds the 64 MiB limit."; return; }
            busy = true; controls();
            try {
                const parsed = await parseScl(await file.arrayBuffer());
                records = parsed.records; selected.clear(); page = 0; source = file.name; dirty = true;
                $("frame").textContent = "Select an event row to inspect its bytes.";
                $("detail").textContent = "";
                $("message").textContent = parsed.truncated ? "Only the first 100,000 events were imported; the remaining file was not analyzed." : "SCL file loaded.";
            } catch (error) { $("message").textContent = error.message + ". Previous dataset retained."; }
            finally { busy = false; controls(); $("file").value = ""; }
        };
        function matches(e, pattern) {
            if (pattern.length === 0)
                return true;
            return e.data.some((_, start) => start + pattern.length <= e.data.length &&
                pattern.every((byte, i) => byte === null || byte === e.data[start + i]));
        }
        function rebuild() {
            const query = $("id").value.trim().toUpperCase().replace(/^0X/, "");
            const tokens = $("bytes").value.trim().split(/\s+/).filter(Boolean);
            if (tokens.some(t => !/^(\?\?|[0-9a-fA-F]{2})$/.test(t))) {
                $("bytes").setCustomValidity("Use hex byte pairs or ?? separated by spaces");
                $("matches").textContent = "Invalid byte pattern";
                $("events").textContent = "";
                $("ids").textContent = "";
                groups = [];
                filtered = [];
                $("page").textContent = "0 / 0";
                $("prev").disabled = true;
                $("next").disabled = true;
                return;
            }
            $("bytes").setCustomValidity("");
            const pattern = tokens.map(t => t === "??" ? null : parseInt(t, 16));
            const busFilter = $("bus").value, eventFilter = $("event").value;
            const formatFilter = $("format").value, onlySelected = $("only").checked;
            const map = new Map();
            filtered = [];
            records.forEach((e, index) => {
                if (!idText(e).includes(query) || (busFilter !== "" && e.bus !== Number(busFilter)) ||
                    (eventFilter === "rx" && e.type !== 0) || (eventFilter === "tx" && e.type === 0) ||
                    (formatFilter === "fd" && !(e.flags & 4)) || (formatFilter === "classic" && (e.flags & 4)) ||
                    !matches(e, pattern)) return;
                const k = key(e);
                if (!map.has(k)) map.set(k, { key: k, e, count: 0, last: index, min: Infinity, max: 0, sum: 0, intervals: 0, previous: {} });
                const g = map.get(k); g.last = index;
                // Keep RX and completed TX intervals separate; never compare distinct clock sources.
                if (e.type === 0 || e.type === 2) {
                    g.count++;
                    const domain = e.type + ":" + (e.capture !== null ? "capture" : e.source);
                    const time = e.capture !== null ? e.capture : e.source !== 0 ? e.timestamp : null;
                    const previous = g.previous[domain];
                    if (time !== null && previous !== undefined && time >= previous) {
                        const delta = Number(time - previous) / 1000;
                        g.min = Math.min(g.min, delta); g.max = Math.max(g.max, delta); g.sum += delta; g.intervals++;
                    }
                    if (time !== null) g.previous[domain] = time;
                }
                if (!onlySelected || selected.has(k)) filtered.push(index);
            });
            groups = Array.from(map.values());
            const sort = $("sort").value;
            groups.sort((a, b) => sort === "count" ? b.count - a.count : sort === "last" ? b.last - a.last :
                sort === "interval" ? (a.intervals ? a.sum / a.intervals : Infinity) - (b.intervals ? b.sum / b.intervals : Infinity) :
                a.e.id - b.e.id || a.e.bus - b.e.bus || a.e.flags - b.e.flags);
            if ($("order").value === "new")
                filtered.reverse();
            // Bound rendered identifier rows even for files containing many unique IDs.
            $("ids").innerHTML = groups.slice(0, 1000).map((g, i) => '<tr><td><input type="checkbox" aria-label="Select ID ' +
                idText(g.e) + '" data-group="' + i + '"' + (selected.has(g.key) ? " checked" : "") +
                '></td><td>' + (g.e.bus ? "S " : "P ") + idText(g.e) + formatText(g.e) + '</td><td>' + g.count +
                '</td><td>' + (g.intervals ? [g.min, g.sum / g.intervals, g.max].map(n => n.toFixed(2)).join(" / ") : "—") + '</td></tr>').join("");
            const pages = Math.ceil(filtered.length / PAGE);
            page = Math.max(0, Math.min(page, pages - 1));
            $("events").innerHTML = filtered.slice(page * PAGE, (page + 1) * PAGE).map(index => {
                const e = records[index], time = e.capture !== null ? timeText(e.capture) : e.source ? timeText(e.timestamp) : "—";
                return '<tr data-index="' + index + '" tabindex="0"><td>' + (index + 1) + '</td><td>' + (e.bus ? "S" : "P") +
                    '</td><td>' + time + '</td><td>' + names[e.type] + '</td><td>' + idText(e) + '</td><td>' + dataText(e) + '</td></tr>';
            }).join("");
            $("matches").textContent = filtered.length.toLocaleString() + " events";
            $("page").textContent = (pages ? page + 1 : 0) + " / " + pages;
            $("prev").disabled = page === 0;
            $("next").disabled = page + 1 >= pages;
            $("summary").textContent = source + " · " + records.length.toLocaleString() + " retained events · " + groups.length +
                " matching IDs" + (groups.length > 1000 ? " (first 1,000 IDs displayed)" : "") +
                " · intervals calculated from filtered RX / completed TX frames";
        }
        function inspect(index) {
            const e = records[index];
            if (!e)
                return;
            let previous = null;
            for (let i = index - 1; i >= 0; i--) {
                if (key(records[i]) === key(e) && records[i].type === e.type) {
                    previous = records[i];
                    break;
                }
            }
            $("frame").textContent = (e.bus ? "Secondary" : "Primary") + " · " + idText(e) + formatText(e) +
                " · " + names[e.type] + " · sequence " + e.sequence +
                " · original timestamp " + e.timestamp + " µs (" + ["none", "software", "hardware"][e.source] + ")" +
                (e.capture !== null ? " · capture +" + timeText(e.capture) + " s" : "");
            $("detail").textContent = e.flags & 2 ? "Remote frame — no payload" :
                "Byte  HEX  DEC  BIN       ASCII  Changed\n" + e.data.map((b, i) =>
                    String(i).padStart(4) + "   " + hex(b) + "  " + String(b).padStart(3) + "  " +
                    b.toString(2).padStart(8, "0") + "  " + (b >= 32 && b <= 126 ? String.fromCharCode(b) : ".") +
                    "      " + (previous ? previous.data[i] === b ? "" : "*" : "—")).join("\n");
        }
        $("ids").onchange = event => {
            const i = event.target.dataset.group; if (i === undefined) return;
            const k = groups[Number(i)].key;
            if (event.target.checked) selected.add(k); else selected.delete(k);
            dirty = true;
        };
        $("events").onclick = event => { const row = event.target.closest("[data-index]"); if (row) inspect(Number(row.dataset.index)); };
        $("events").onkeydown = event => { if (event.key === "Enter") { const row = event.target.closest("[data-index]"); if (row) inspect(Number(row.dataset.index)); } };
        for (const id of ["id", "bytes", "bus", "event", "format", "only", "sort", "order"]) {
            $(id).addEventListener("input", () => { page = 0; dirty = true; });
        }
        $("all").onclick = () => { groups.slice(0, 1000).forEach(g => selected.add(g.key)); dirty = true; };
        $("none").onclick = () => { selected.clear(); dirty = true; };
        $("reset").onclick = () => {
            for (const id of ["id", "bytes", "bus", "event", "format"]) $(id).value = "";
            $("only").checked = false; selected.clear(); page = 0; dirty = true;
        };
        $("prev").onclick = () => { page--; dirty = true; };
        $("next").onclick = () => { page++; dirty = true; };
        setInterval(() => { if (dirty && !document.hidden && !busy) { dirty = false; rebuild(); } }, 500);
        rebuild();
    
    }

    function init_can_settings() {
        /*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

        (() => {
            "use strict";

            const host = document.getElementById("can-settings");
            if (!host)
                return;

            const nominalRates = [
                10000, 20000, 33333, 50000, 83333, 100000, 125000, 250000, 500000, 800000, 1000000
            ];
            const dataRates = [ 1000000, 2000000, 4000000, 5000000 ];
            const rateOptions = rates =>
                rates
                    .map(rate => `<option value="${rate}">${
                             rate >= 1000000 ? `${rate / 1000000} Mbit/s`
                                             : `${rate / 1000} kbit/s`}</option>`)
                    .join("");

            function channelMarkup(bus, title) {
                const secondary = bus === "secondary";
                return `<fieldset class="can-config-channel" data-bus="${bus}">
            <legend>${title}</legend>
            <label class="can-config-enable"><input data-field="enabled" type="checkbox">Enable interface</label>
            <div class="can-config-fields">
                <label>${secondary ? "Nominal speed" : "Bus speed"}
                    <select data-field="rate">${rateOptions(nominalRates)}</select></label>
                <label>Operating mode<select data-field="mode">
                    <option value="listen">Listen only</option><option value="normal">Normal</option>
                </select></label>
                ${
                    secondary ? `<label>Format<select data-field="format">
                    <option value="classic">Classical</option><option value="fd">CAN FD</option>
                    <option value="brs">CAN FD + BRS</option></select></label>
                    <label>Data speed<select data-field="data">${
                                    rateOptions(dataRates)}</select></label>`
                              : ""}
            </div>
            <div class="can-config-card-footer">
                <span title="No termination control is exposed by this firmware">120 Ω · Not supported</span>
                <button type="button" data-action="apply">Apply ${
                    secondary ? "Secondary" : "Primary"}</button>
            </div>
        </fieldset>`;
            }

            host.innerHTML = channelMarkup("primary", "Primary · TWAI") +
                             channelMarkup("secondary", "Secondary · MCP2518FD") +
                             `<div class="can-config-footer">
            <span class="can-config-status" role="status" aria-live="polite">Loading CAN settings…</span>
            <button type="button" data-action="reload">Reload</button>
            <button type="button" data-action="save">Save to device</button>
            <small>Apply restarts the selected CAN interface. Save stores all current device settings.
                120 Ω control is not exposed by the firmware.</small>
        </div>`;

            const status = host.querySelector(".can-config-status");
            const panels = Array.from(host.querySelectorAll("[data-bus]"));
            const field = (panel, name) => panel.querySelector(`[data-field="${name}"]`);
            let loaded = false;
            let busy = false;
            const dirty = new Set();

            function message(text, error = false) {
                status.textContent = text;
                status.classList.toggle("error", error);
            }

            function updateControls() {
                for (const panel of panels) {
                    panel.disabled = busy || !loaded;
                    if (panel.dataset.bus === "secondary") {
                        const nominal = Number(field(panel, "rate").value);
                        const data = field(panel, "data");
                        for (const option of data.options) {
                            option.disabled = Number(option.value) <= nominal;
                        }
                        if (Number(data.value) <= nominal) {
                            data.value = String(dataRates.find(rate => rate > nominal));
                        }
                        data.disabled = field(panel, "format").value !== "brs";
                    }
                }
                host.querySelector('[data-action="reload"]').disabled = busy;
                host.querySelector('[data-action="save"]').disabled =
                    busy || !loaded || dirty.size > 0;
            }

            async function request(url, options = {}) {
                const controller = new AbortController();
                const timer = setTimeout(() => controller.abort(), 30000);
                try {
                    const response = await fetch(
                        url, {...options, cache : "no-store", signal : controller.signal});
                    const body = await response.json().catch(() => ({}));
                    if (!response.ok)
                        throw new Error(body.message || body.error || `HTTP ${response.status}`);
                    return body;
                } finally {
                    clearTimeout(timer);
                }
            }

            function populate(panel, config) {
                const secondary = panel.dataset.bus === "secondary";
                if (!config || typeof config.enabled !== "boolean" ||
                    typeof config.listen_only !== "boolean" ||
                    !nominalRates.includes(secondary ? config.nominal_bitrate : config.bitrate) ||
                    (secondary && (typeof config.fd_enabled !== "boolean" ||
                                   typeof config.brs_enabled !== "boolean"))) {
                    throw new Error("Unsupported CAN settings response");
                }
                field(panel, "enabled").checked = config.enabled;
                field(panel, "mode").value = config.listen_only ? "listen" : "normal";
                field(panel, "rate").value =
                    String(secondary ? config.nominal_bitrate : config.bitrate);
                if (secondary) {
                    if (config.brs_enabled &&
                        (!config.fd_enabled || !dataRates.includes(config.data_bitrate) ||
                         config.data_bitrate <= config.nominal_bitrate)) {
                        throw new Error("Unsupported CAN FD settings response");
                    }
                    field(panel, "format").value =
                        config.fd_enabled ? (config.brs_enabled ? "brs" : "fd") : "classic";
                    field(panel, "data").value = String(
                        dataRates.includes(config.data_bitrate) ? config.data_bitrate : 2000000);
                }
            }

            async function reload() {
                if (busy || (dirty.size && !window.confirm("Discard unapplied CAN changes?")))
                    return;
                busy = true;
                updateControls();
                message("Loading CAN settings…");
                try {
                    const settings = await request("/api/settings");
                    for (const panel of panels)
                        populate(panel, settings.can?.[panel.dataset.bus]);
                    loaded = true;
                    dirty.clear();
                    message("CAN settings loaded. Changes are applied only with Apply.");
                } catch (error) {
                    loaded = false;
                    message(`CAN settings unavailable: ${error.message}`, true);
                } finally {
                    busy = false;
                    updateControls();
                }
            }

            async function apply(panel) {
                if (busy || !loaded)
                    return;
                const bus = panel.dataset.bus;
                if (!window.confirm(`Apply ${
                        bus} CAN settings? This may interrupt capture or recording on that bus.`))
                    return;
                const rate = Number(field(panel, "rate").value);
                const config = {
                    enabled : field(panel, "enabled").checked,
                    listen_only : field(panel, "mode").value === "listen"
                };
                if (bus === "primary") {
                    config.bitrate = rate;
                } else {
                    config.nominal_bitrate = rate;
                    config.fd_enabled = field(panel, "format").value !== "classic";
                    config.brs_enabled = field(panel, "format").value === "brs";
                    config.data_bitrate =
                        config.brs_enabled ? Number(field(panel, "data").value) : rate;
                }
                busy = true;
                updateControls();
                message(`Applying ${bus} CAN settings…`);
                try {
                    await request("/api/settings", {
                        method : "PUT",
                        headers : {"Content-Type" : "application/json"},
                        body : JSON.stringify({can : {[bus] : config}})
                    });
                    const settings = await request("/api/settings");
                    populate(panel, settings.can?.[bus]);
                    dirty.delete(bus);
                    message(`${
                        bus === "primary" ? "Primary"
                                          : "Secondary"} applied. Not saved to flash yet.`);
                } catch (error) {
                    loaded = false;
                    message(
                        `Apply/readback failed: ${error.message}. Reload to check device state.`,
                        true);
                } finally {
                    busy = false;
                    updateControls();
                }
            }

            for (const panel of panels) {
                panel.addEventListener("change", () => {
                    dirty.add(panel.dataset.bus);
                    message("Unapplied changes. Use Apply for each edited bus before saving.");
                    updateControls();
                });
                panel.querySelector('[data-action="apply"]')
                    .addEventListener("click", () => apply(panel));
            }
            host.querySelector('[data-action="reload"]').addEventListener("click", reload);
            host.querySelector('[data-action="save"]').addEventListener("click", async () => {
                if (busy || !loaded || dirty.size)
                    return;
                if (!window.confirm("Save all currently applied device settings to flash?"))
                    return;
                busy = true;
                updateControls();
                try {
                    await request("/api/settings/save", {method : "POST"});
                    message("Current device settings saved to flash.");
                } catch (error) {
                    message(`Save failed: ${error.message}`, true);
                } finally {
                    busy = false;
                    updateControls();
                }
            });
            reload();
        })();
    }

    function init_transmit() {
        const host = document.getElementById('can-transmit');
        if (!host)
            return;
        const lengths = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 ];
        host.innerHTML =
            `<details class="tx-panel"><summary>Transmit CAN frames <span>Single frame · cyclic jobs · counters</span></summary>
            <p class="tx-warning">Sends real frames to the selected bus. Use only on equipment you are authorized to test.
                Jobs continue after closing this page. Stop prevents new submissions; a queued frame may still transmit.</p>
            <form class="tx-form">
                <div class="tx-fields">
                    <label>Job<select name="slot">${
                    [0, 1, 2, 3]
                        .map(i => `<option value="${i}">Job ${i + 1}</option>`)
                        .join('')}</select></label>
                    <label>Channel<select name="bus"><option value="0">Primary · TWAI</option><option value="1">Secondary · MCP2518FD</option></select></label>
                    <label>ID (hex)<input name="id" value="123" required maxlength="10"></label>
                    <label>ID format<select name="extended"><option value="0">Standard · 11 bit</option><option value="1">Extended · 29 bit</option></select></label>
                    <label>Frame format<select name="format"><option value="classic">Classical CAN</option><option value="fd">CAN FD</option><option value="brs">CAN FD + BRS</option></select></label>
                    <label>DLC<select name="dlc">${
                lengths
                    .map((n, i) => `<option value="${i}" ${i === 8 ? 'selected' : ''}>${i} · ${
                             n} bytes</option>`)
                    .join('')}</select></label>
                    <label>Interval (ms)<input name="interval_ms" type="number" min="10" max="3600000" value="100" required></label>
                    <label>Attempts (0 = until Stop)<input name="count" type="number" min="0" max="1000000" value="1" required></label>
                </div>
                <label class="tx-data">DATA (hex bytes, matching DLC)<input name="data" value="00 00 00 00 00 00 00 00" maxlength="191" autocomplete="off"></label>
                <details class="tx-increments"><summary>Automatic increments</summary><div class="tx-fields">
                    <label>ID step (hex, 0 = off)<input name="id_step" value="0" required></label>
                    <label>ID end (hex)<input name="id_end" value="123" required></label>
                    <label>DLC increment<select name="increment_dlc"><option value="0">Off</option><option value="1">+1 after each queued frame</option></select></label>
                    <label>DLC end<input name="dlc_end" type="number" min="0" max="15" value="8" required></label>
                    <label>Counter offset (byte)<input name="data_offset" type="number" min="0" max="63" value="0" required></label>
                    <label>Counter width (0 = off)<input name="data_width" type="number" min="0" max="8" value="0" required></label>
                    <label>Counter step<input name="data_step" type="number" min="1" max="4294967295" value="1" required></label>
                    <label>Counter byte order<select name="data_big_endian"><option value="0">Little endian</option><option value="1">Big endian</option></select></label>
                </div><p>ID and DLC wrap to the initial value at their end. DATA wraps at the selected counter width.
                    The counter must fit the initial payload. Newly exposed bytes are zero-filled.</p></details>
                <div class="tx-actions"><button type="submit" class="primary" disabled>Start job</button>
                    <button type="button" data-tx="stop">Stop selected</button><button type="button" data-tx="stop-all">Stop all jobs</button>
                    <button type="button" data-tx="refresh">Refresh status</button></div>
            </form><p class="tx-status" role="status" aria-live="polite">Loading transmission service…</p>
            <div class="tx-table"><table><thead><tr><th>Job / bus</th><th>State</th><th>Attempts</th><th>Queued</th><th>Confirmed</th><th>Failed / aborted</th><th>Unknown</th><th>Pending</th><th>Last error</th></tr></thead><tbody></tbody></table></div>
            <p class="tx-note">Timing is best-effort, minimum 10 ms. One pending frame per job; no catch-up bursts.
                A submission error or a missing confirmation for 5 seconds stops the job. Jobs are not saved across reboot.</p></details>`;
        const form = host.querySelector('form');
        const control = name => form.elements.namedItem(name);
        const panel = host.querySelector('.tx-panel');
        const status = host.querySelector('.tx-status');
        const start = form.querySelector('[type="submit"]');
        let jobs = null, busy = false, polling = false;
        function controls() {
            const job = jobs?.find(j => j.slot === Number(control('slot').value));
            start.disabled = busy || !job || job.state === 'active' || job.pending !== 0;
            const primary = control('bus').value === '0';
            for (const option of control('format').options)
                option.disabled = primary && option.value !== 'classic';
            if (primary)
                control('format').value = 'classic';
            const fd = control('format').value !== 'classic';
            for (const option of control('dlc').options)
                option.disabled = !fd && Number(option.value) > 8;
            if (!fd && Number(control('dlc').value) > 8)
                control('dlc').value = '8';
            control('dlc_end').max = fd ? '15' : '8';
        }
        async function api(payload) {
            const controller = new AbortController();
            const timer = setTimeout(() => controller.abort(), 10000);
            try {
                const response = await fetch('/api/can/transmit', {
                    cache : 'no-store',
                    signal : controller.signal,
                    ...(payload ? {
                        method : 'POST',
                        headers : {'Content-Type' : 'application/json'},
                        body : JSON.stringify(payload)
                    }
                                : {})
                });
                const data = await response.json();
                if (!response.ok)
                    throw new Error(data.message || `HTTP ${response.status}`);
                return data;
            } finally {
                clearTimeout(timer);
            }
        }
        async function refresh() {
            if (polling)
                return;
            polling = true;
            try {
                const result = await api();
                if (!Array.isArray(result.jobs) || result.jobs.length !== 4)
                    throw new Error('Invalid job status');
                jobs = result.jobs;
                const table = host.querySelector('tbody');
                table.replaceChildren();
                for (const j of jobs) {
                    const row = document.createElement('tr');
                    for (const value
                             of [`${j.slot + 1} / ${j.bus === 0 ? 'P' : 'S'}`, j.state, j.attempts,
                                 j.queued, j.completed, `${j.failed} / ${j.aborted}`, j.unknown,
                                 j.pending ? 'Yes' : 'No', j.last_error]) {
                        const cell = document.createElement('td');
                        cell.textContent = String(value);
                        row.append(cell);
                    }
                    table.append(row);
                }
            } catch (error) {
                jobs = null;
                status.textContent = `Transmission status unavailable: ${error.message}`;
            } finally {
                polling = false;
                controls();
            }
        }
        function integer(name, min, max) {
            const raw = control(name).value.trim(), n = Number(raw);
            if (!/^\d+$/.test(raw) || !Number.isSafeInteger(n) || n < min || n > max)
                throw new Error(`Invalid ${name}`);
            return n;
        }
        function hex(name, max) {
            const raw = control(name).value.trim();
            if (!/^(?:0x)?[\da-f]+$/i.test(raw))
                throw new Error(`Invalid hexadecimal ${name}`);
            const n = parseInt(raw.replace(/^0x/i, ''), 16);
            if (!Number.isSafeInteger(n) || n > max)
                throw new Error(`${name} is out of range`);
            return n;
        }
        async function command(payload) {
            if (busy)
                return;
            busy = true;
            controls();
            try {
                await api(payload);
                status.textContent =
                    'Command accepted. Check the job counters for actual transmission results.';
            } catch (error) {
                status.textContent = `Command failed or result unknown: ${
                    error.message}. Check status before retrying.`;
            } finally {
                busy = false;
                await refresh();
                controls();
            }
        }
        form.addEventListener('change', controls);
        form.addEventListener('submit', async event => {
            event.preventDefault();
            if (start.disabled)
                return;
            try {
                const extended = control('extended').value === '1',
                      fd = control('format').value !== 'classic';
                const id = hex('id', extended ? 0x1fffffff : 0x7ff),
                      dlc = integer('dlc', 0, fd ? 15 : 8);
                const raw = control('data').value.trim();
                const tokens = raw ? raw.split(/\s+/) : [];
                if (tokens.length !== lengths[dlc] || tokens.some(t => !/^[\da-f]{2}$/i.test(t)))
                    throw new Error(`DATA must contain exactly ${lengths[dlc]} hexadecimal bytes`);
                const payload = {
                    action : 'start',
                    slot : integer('slot', 0, 3),
                    bus : integer('bus', 0, 1),
                    id,
                    extended,
                    fd,
                    brs : control('format').value === 'brs',
                    dlc,
                    data : tokens.map(t => parseInt(t, 16)),
                    interval_ms : integer('interval_ms', 10, 3600000),
                    count : integer('count', 0, 1000000),
                    id_step : hex('id_step', extended ? 0x1fffffff : 0x7ff),
                    id_end : hex('id_end', extended ? 0x1fffffff : 0x7ff),
                    increment_dlc : control('increment_dlc').value === '1',
                    dlc_end : integer('dlc_end', 0, fd ? 15 : 8),
                    data_offset : integer('data_offset', 0, 63),
                    data_width : integer('data_width', 0, 8),
                    data_step : integer('data_step', 1, 4294967295),
                    data_big_endian : control('data_big_endian').value === '1'
                };
                if (payload.id_step && payload.id_end < id)
                    throw new Error('ID end must be at least the initial ID');
                if (payload.increment_dlc && payload.dlc_end < dlc)
                    throw new Error('DLC end must be at least the initial DLC');
                if (payload.data_width && payload.data_offset + payload.data_width > lengths[dlc])
                    throw new Error('DATA counter must fit the initial payload');
                if (!window.confirm(`Start job ${payload.slot + 1} on ${
                        payload.bus === 0
                            ? 'Primary'
                            : 'Secondary'}? ${payload.count || 'Unlimited'} attempts, interval ${
                        payload.interval_ms} ms. This transmits real CAN traffic.`))
                    return;
                await command(payload);
            } catch (error) {
                status.textContent = error.message;
            }
        });
        host.querySelector('[data-tx="stop"]')
            .addEventListener(
                'click', () => command({action : 'stop', slot : Number(control('slot').value)}));
        host.querySelector('[data-tx="stop-all"]').addEventListener('click', () => command({
                                                                                 action : 'stop_all'
                                                                             }));
        host.querySelector('[data-tx="refresh"]').addEventListener('click', () => refresh());
        panel.addEventListener('toggle', () => {
            if (panel.open)
                refresh();
        });
        controls();
        refresh().then(() => {
            if (jobs)
                status.textContent = 'Ready. No transmission starts until you press Start job.';
        });
        setInterval(() => {
            if (!document.hidden && panel.open && !busy)
                refresh();
        }, 2000);
    }

    function init_hardware_filters() {
        const anchor = document.getElementById('can-transmit');
        if (!anchor)
            return;
        const panel = document.createElement('details');
        panel.className = 'tx-panel hardware-filters-panel';
        panel.innerHTML =
            `<summary>Hardware RX filters <span>Choose which frames the device receives</span></summary>
            <p class="tx-note">Applies to the whole device: Monitor, Logger and WebSocket. Runtime only; reinitializing a CAN interface can reset its filters. Not saved by “Save to device”. TX is not filtered.</p>
            <div class="hardware-filter-cards"></div>
            <div class="tx-actions"><button type="button" data-filter-reload>Reload from device</button></div>
            <p class="tx-status" role="status" aria-live="polite">Open this panel to read the hardware filters.</p>`;
        anchor.before(panel);
        const cards = panel.querySelector('.hardware-filter-cards');
        const status = panel.querySelector('.tx-status');
        let busy = false;
        let loaded = false;
        let dirty = false;
        let states = [];
        const reloadButton = panel.querySelector('[data-filter-reload]');

        async function request(options) {
            const controller = new AbortController();
            const timer = setTimeout(() => controller.abort(), 30000);
            try {
                const response =
                    await fetch('/api/can/filters',
                                {...options, cache : 'no-store', signal : controller.signal});
                const result = await response.json();
                if (!response.ok)
                    throw new Error(result.message || `HTTP ${response.status}`);
                return result;
            } finally {
                clearTimeout(timer);
            }
        }

        function row(form, filter = {id : 0, mask : 0x7ff, extended : false}) {
            const entry = document.createElement('div');
            entry.className = 'hardware-filter-row';
            entry.innerHTML =
                `<label>ID (hex)<input data-id maxlength="8" required autocomplete="off"></label>
                <label>Mask (hex)<input data-mask maxlength="8" required autocomplete="off"></label>
                <label>Format<select data-format><option value="standard">11-bit</option><option value="extended">29-bit</option></select></label>
                <button type="button" data-remove aria-label="Remove filter">×</button>`;
            entry.querySelector('[data-id]').value = filter.id.toString(16).toUpperCase();
            entry.querySelector('[data-mask]').value = filter.mask.toString(16).toUpperCase();
            entry.querySelector('[data-format]').value = filter.extended ? 'extended' : 'standard';
            entry.querySelector('[data-format]').addEventListener('change', () => {
                const mask = entry.querySelector('[data-mask]');
                if (/^(7ff|1fffffff)$/i.test(mask.value))
                    mask.value = entry.querySelector('[data-format]').value === 'extended'
                                     ? '1FFFFFFF'
                                     : '7FF';
            });
            entry.querySelector('[data-remove]').addEventListener('click', () => {
                entry.remove();
                dirty = true;
                update(form);
            });
            form.querySelector('.hardware-filter-rows').append(entry);
        }

        function update(form) {
            const state = states[Number(form.dataset.bus)];
            const selected = form.querySelector('[data-mode]').value === 'selected';
            const count = form.querySelectorAll('.hardware-filter-row').length;
            form.querySelector('.hardware-filter-rows').hidden = !selected;
            form.querySelector('[data-add]').disabled = !selected || count >= state.capacity;
            form.querySelector('[data-count]').textContent =
                `${selected ? count : 0} / ${state.capacity} filters`;
        }

        function render(buses) {
            states = buses;
            cards.replaceChildren();
            for (const state of states) {
                const form = document.createElement('form');
                form.className = 'hardware-filter-form';
                form.dataset.bus = state.bus;
                form.innerHTML = `<fieldset ${state.available ? '' : 'disabled'}>
                    <legend>${state.bus === 0 ? 'Primary · TWAI' : 'Secondary · MCP2518FD'}</legend>
                    <p class="tx-note">${
                    state.bus === 0
                        ? '1 full-width mask filter. Dual 16-bit mode is not exposed by this driver.'
                        : 'Up to 32 independent filters; matching any row accepts the frame.'}</p>
                    <label>Receive<select data-mode><option value="all">All messages</option>
                        <option value="selected">Matching filters only</option>
                        ${
                    state.reject_all_supported
                        ? '<option value="none">No messages (reject all)</option>'
                        : ''}</select></label>
                    <div class="hardware-filter-rows"></div>
                    <div class="tx-actions"><button type="button" data-add>Add filter</button>
                        <span data-count></span><button type="submit">Apply RX filters</button></div>
                    <p class="tx-note">${
                    state.available ? 'Full mask = exact ID. 1 bits compare; 0 bits ignore.'
                                    : 'Interface unavailable: ' + state.error}</p>
                </fieldset>`;
                const mode = form.querySelector('[data-mode]');
                mode.value = state.accept_all ? 'all' : state.filters.length ? 'selected' : 'none';
                for (const filter of state.filters)
                    row(form, filter);
                mode.addEventListener('change', () => {
                    if (mode.value === 'selected' && !form.querySelector('.hardware-filter-row'))
                        row(form);
                    update(form);
                });
                form.addEventListener('input', () => { dirty = true; });
                form.addEventListener('change', () => { dirty = true; });
                form.querySelector('[data-add]').addEventListener('click', () => {
                    if (form.querySelectorAll('.hardware-filter-row').length < state.capacity) {
                        row(form);
                        dirty = true;
                        update(form);
                    }
                });
                form.addEventListener('submit', event => {
                    event.preventDefault();
                    apply(form);
                });
                cards.append(form);
                update(form);
            }
        }

        async function reload() {
            if (busy)
                return;
            if (dirty && !window.confirm('Discard unapplied filter edits and read the device?'))
                return;
            busy = true;
            reloadButton.disabled = true;
            try {
                const result = await request();
                if (!Array.isArray(result.buses) || result.buses.length !== 2 ||
                    result.buses.some((bus, index) => bus.bus !== index ||
                                                      !Number.isInteger(bus.capacity) ||
                                                      bus.capacity < 1 || bus.capacity > 32 ||
                                                      !Array.isArray(bus.filters))) {
                    throw new Error('Invalid hardware filter response');
                }
                render(result.buses);
                loaded = true;
                dirty = false;
                status.textContent = 'Current runtime filters loaded. Changes require Apply.';
            } catch (error) {
                loaded = false;
                cards.querySelectorAll('fieldset')
                    .forEach(fieldset => { fieldset.disabled = true; });
                status.textContent = 'Cannot read hardware filters: ' + error.message;
            } finally {
                busy = false;
                reloadButton.disabled = false;
            }
        }

        async function apply(form) {
            if (busy || !loaded)
                return;
            const bus = Number(form.dataset.bus);
            const mode = form.querySelector('[data-mode]').value;
            const payload = {bus, accept_all : mode === 'all', filters : []};
            let submitted = false;
            try {
                if (mode === 'selected') {
                    for (const entry of form.querySelectorAll('.hardware-filter-row')) {
                        const extended = entry.querySelector('[data-format]').value === 'extended';
                        const maximum = extended ? 0x1fffffff : 0x7ff;
                        const hex = selector => {
                            const value = entry.querySelector(selector).value.trim();
                            if (!/^[0-9a-f]{1,8}$/i.test(value) || parseInt(value, 16) > maximum)
                                throw new Error(
                                    'ID and mask must fit the selected 11/29-bit format.');
                            return parseInt(value, 16);
                        };
                        payload.filters.push(
                            {id : hex('[data-id]'), mask : hex('[data-mask]'), extended});
                    }
                    if (!payload.filters.length)
                        throw new Error('Add at least one filter or select No messages.');
                }
                if (!window.confirm(`Apply RX filters to ${
                        bus === 0
                            ? 'Primary'
                            : 'Secondary'}? This affects all capture clients and recording. Frames can be lost while applying; Primary restarts and may abort pending TX. Other unapplied filter edits will be reloaded.`))
                    return;
                busy = true;
                reloadButton.disabled = true;
                submitted = true;
                cards.querySelectorAll('fieldset')
                    .forEach(fieldset => { fieldset.disabled = true; });
                await request({
                    method : 'POST',
                    headers : {'Content-Type' : 'application/json'},
                    body : JSON.stringify(payload)
                });
                dirty = false;
                busy = false;
                await reload();
            } catch (error) {
                status.textContent = submitted
                                         ? 'Filter update not confirmed: ' + error.message +
                                               '. Reload to check the actual state before retrying.'
                                         : error.message;
                if (submitted)
                    loaded = false;
            } finally {
                busy = false;
                reloadButton.disabled = false;
            }
        }
        reloadButton.addEventListener('click', reload);
        panel.addEventListener('toggle', () => {
            if (panel.open && !loaded && !busy)
                reload();
        });
    }

    function init_panel_layout() {
        const settings = document.getElementById('can-settings');
        if (settings) {
            const panel = document.createElement('details');
            panel.className = 'tx-panel can-settings-panel';
            panel.open = true;
            const summary = document.createElement('summary');
            summary.textContent = 'CAN bus settings';
            settings.before(panel);
            panel.append(summary, settings);
        }
        const panels = [
            [ 'settings', document.querySelector('.can-settings-panel') ],
            [ 'transmit', document.querySelector('#can-transmit > details') ],
            [ 'filters', document.querySelector('.hardware-filters-panel') ]
        ];
        for (const [name, panel] of panels) {
            if (!panel)
                continue;
            const key = `spectra:${location.pathname}:panel:${name}`;
            try {
                const value = localStorage.getItem(key);
                if (value !== null)
                    panel.open = value === 'open';
            } catch (_) { /* Storage can be unavailable in private browsing. */
            }
            panel.addEventListener('toggle', () => {
                try {
                    localStorage.setItem(key, panel.open ? 'open' : 'closed');
                } catch (_) { /* Folding remains usable without persistence. */
                }
            });
        }
    }

    const pages = {
        'page-overview' : init_overview,
        'page-settings' : init_settings,
        'page-files' : init_files,
        'page-test' : init_test,
        'page-logger' : init_logger,
        'page-analyzer' : init_analyzer
    };

    for (const [page, initialize] of Object.entries(pages)) {

        if (document.body.classList.contains(page)) {

            initialize();

            if (page === 'page-logger' || page === 'page-analyzer') {
                init_can_settings();
                init_transmit();
                init_hardware_filters();
                init_panel_layout();
            }

            break;
        }
    }
})();
