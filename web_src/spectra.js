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
            setText(
                "device-title",
                "Spectra"
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
                "Spectra";

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

        const directoryTree = document.getElementById("directory-tree");

        const refreshTreeButton = document.getElementById("refresh-tree");

        const backButton = document.getElementById("back");

        const previousPageButton = document.getElementById("previous-page");

        const nextPageButton = document.getElementById("next-page");

        const pageStatusElement = document.getElementById("page-status");

        const createFolderButton = document.getElementById("create-folder");
        const createFileButton = document.getElementById("create-file");
        const uploadInput = document.getElementById("upload-files");
        const uploadProgress = document.getElementById("upload-progress");
        const formatButton = document.getElementById("format-sd");
        const viewer = document.getElementById("file-viewer");
        const viewerTitle = document.getElementById("file-viewer-title");
        const viewerMeta = document.getElementById("file-viewer-meta");
        const viewerContent = document.getElementById("file-viewer-content");

        let currentPath = "/";
        let currentOffset = 0;
        let hasMore = false;
        let activeRequest = null;
        let treeGeneration = 0;

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

        function isTextFile(name) {
            return /\.(txt|log|json|cfg|conf|ini|csv|asc|dbc|md|xml|yaml|yml|html|css|js|c|h)$/i.test(name);
        }

        async function fileOperation(action, path, body = null, confirmation = null) {
            const query = new URLSearchParams({action, volume : "sd", path});

            if (confirmation !== null)
                query.set("confirm", confirmation);

            const response = await fetch(`/api/files?${query.toString()}`, {
                method : "POST",
                body,
                cache : "no-store"
            });
            let result = {};

            try {
                result = await response.json();
            } catch (_) {
                /* The HTTP status remains useful for malformed responses. */
            }

            if (!response.ok)
                throw new Error(result.message || `HTTP ${response.status}`);

            return result;
        }

        async function refreshAfterMutation(message) {
            currentOffset = 0;
            await loadFiles();
            loadDirectoryTree();
            setStatus(message);
        }

        async function previewFile(volume, path, name, size) {
            if ((size > 1024 * 1024) &&
                !window.confirm("This file is larger than 1 MiB. Show only its first 128 KiB?")) {

                return;
            }

            const query = new URLSearchParams({volume, path});
            const response = await fetch(`/api/files/download?${query.toString()}`, {
                headers : {Range : "bytes=0-131071"},
                cache : "no-store"
            });

            if (!response.ok && (response.status !== 206))
                throw new Error(`HTTP ${response.status}`);

            viewerTitle.textContent = name;
            viewerMeta.textContent = size > 131072
                ? `Showing first 128 KiB of ${formatSize(size)}`
                : formatSize(size);
            viewerContent.textContent = await response.text();
            viewer.showModal();
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
            const nameIcon = document.createElement("span");
            nameIcon.className = "file-entry-icon";
            nameIcon.textContent = entry.type === "directory" ? "▸" : "·";
            const name = document.createElement("span");
            name.className = "file-entry-name";
            name.textContent = entry.name;
            nameCell.append(nameIcon, name);
            nameCell.title = entry.name;
            typeCell.textContent = entry.type === "directory" ? "Folder" : "File";
            sizeCell.textContent = entry.type === "directory" ? "-" : formatSize(entry.size);
            const actions = document.createElement("div");
            actions.className = "file-row-actions";
            const entryPath = joinPath(currentPath, entry.name);

            if (entry.type === "directory") {
                row.classList.add("directory-row");
                const openButton = document.createElement("button");
                openButton.type = "button";
                openButton.textContent = "Open";

                openButton.addEventListener("click", () => {
                    currentPath = entryPath;
                    currentOffset = 0;
                    hasMore = false;
                    loadFiles();
                });

                actions.appendChild(openButton);

            } else {
                if (isTextFile(entry.name)) {
                    const viewButton = document.createElement("button");
                    viewButton.type = "button";
                    viewButton.textContent = "View";
                    viewButton.addEventListener("click", async () => {
                        try {
                            await previewFile(volume, entryPath, entry.name, entry.size);
                        } catch (error) {
                            setStatus(`Preview failed: ${error.message}`, true);
                        }
                    });
                    actions.appendChild(viewButton);
                }

                const downloadButton = document.createElement("button");
                downloadButton.type = "button";
                downloadButton.textContent = "Download";
                downloadButton.addEventListener("click", () => {
                    const query = new URLSearchParams({volume, path : entryPath});

                    window.location.href = `/api/files/download?` + query.toString();
                });
                actions.appendChild(downloadButton);
            }

            if (volume === "sd") {
                const deleteButton = document.createElement("button");
                deleteButton.type = "button";
                deleteButton.className = "file-delete-action";
                deleteButton.textContent = "Delete";
                deleteButton.addEventListener("click", async () => {
                    if (!window.confirm(`Delete ${entry.type} “${entry.name}”?`))
                        return;

                    try {
                        await fileOperation("delete", entryPath);
                        await refreshAfterMutation(`${entry.name} deleted`);
                    } catch (error) {
                        setStatus(`Delete failed: ${error.message}`, true);
                    }
                });
                actions.appendChild(deleteButton);
            }

            actionCell.appendChild(actions);
            row.append(nameCell, typeCell, sizeCell, actionCell);

            return row;
        }

        async function loadDirectoryFolders(volume, path, generation) {
            const folders = [];
            let offset = 0;
            let more = false;

            do {
                const query = new URLSearchParams({
                    volume,
                    path,
                    offset : String(offset),
                    limit : "32"
                });

                const response = await fetch(
                    `/api/files?${query.toString()}`,
                    {cache : "no-store"}
                );

                const result = await response.json();

                if (!response.ok)
                    throw new Error(result.message || `HTTP ${response.status}`);
                if (generation !== treeGeneration)
                    return [];
                if (!Array.isArray(result.entries))
                    throw new Error("Invalid folder-list response");

                folders.push(
                    ...result.entries.filter(entry =>
                        entry && entry.type === "directory" &&
                        typeof entry.name === "string")
                );

                offset += result.entries.length;
                more = result.has_more === true;
            } while (more && offset <= 1024);

            return folders.sort((left, right) =>
                left.name.localeCompare(right.name, undefined, {sensitivity : "base"}));
        }

        function createDirectoryNode(volume, path, label, depth, generation) {
            const node = document.createElement("div");
            const row = document.createElement("div");
            const toggle = document.createElement("button");
            const select = document.createElement("button");
            const children = document.createElement("div");

            node.className = "directory-node";
            node.dataset.path = path;
            row.className = "directory-node-row";
            row.style.setProperty("--tree-depth", String(depth));
            toggle.className = "directory-toggle";
            toggle.type = "button";
            toggle.textContent = "›";
            toggle.setAttribute("aria-label", `Expand ${label}`);
            toggle.setAttribute("aria-expanded", "false");
            select.className = "directory-name";
            select.type = "button";
            select.innerHTML = `<span aria-hidden="true">▰</span><span></span>`;
            select.lastElementChild.textContent = label;
            select.title = path;
            children.className = "directory-children";
            children.hidden = true;

            select.addEventListener("click", () => {
                currentPath = path;
                currentOffset = 0;
                hasMore = false;
                loadFiles();
            });

            toggle.addEventListener("click", async () => {
                if (node.dataset.loaded === "true") {
                    const expanded = children.hidden;
                    children.hidden = !expanded;
                    toggle.classList.toggle("expanded", expanded);
                    toggle.setAttribute("aria-expanded", String(expanded));
                    return;
                }

                toggle.disabled = true;
                toggle.classList.add("loading");

                try {
                    const folders = await loadDirectoryFolders(volume, path, generation);

                    if (generation !== treeGeneration)
                        return;

                    for (const folder of folders) {
                        children.append(
                            createDirectoryNode(
                                volume,
                                joinPath(path, folder.name),
                                folder.name,
                                depth + 1,
                                generation
                            )
                        );
                    }

                    updateActiveDirectory();

                    node.dataset.loaded = "true";
                    toggle.classList.toggle("empty", folders.length === 0);
                    toggle.classList.toggle("expanded", folders.length !== 0);
                    toggle.setAttribute("aria-expanded", String(folders.length !== 0));
                    children.hidden = folders.length === 0;
                } catch (error) {
                    setStatus(`Failed to load folders: ${error.message}`, true);
                } finally {
                    toggle.disabled = false;
                    toggle.classList.remove("loading");
                }
            });

            row.append(toggle, select);
            node.append(row, children);

            return node;
        }

        function updateActiveDirectory() {
            for (const node of directoryTree.querySelectorAll(".directory-node")) {
                const name = node.firstElementChild?.querySelector(".directory-name");

                name?.classList.toggle(
                    "active",
                    node.dataset.path === currentPath
                );
            }
        }

        function loadDirectoryTree() {
            const generation = ++treeGeneration;
            const volume = volumeSelector.value;
            const root = createDirectoryNode(volume, "/", volume === "sd" ? "SD card" : "Internal", 0,
                                             generation);

            directoryTree.replaceChildren(root);
            root.querySelector(".directory-toggle").click();
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

                updateActiveDirectory();

                hasMore = result.has_more === true;

                const entries = [...result.entries].sort((left, right) => {
                    if (left.type !== right.type)
                        return left.type === "directory" ? -1 : 1;

                    return String(left.name).localeCompare(
                        String(right.name),
                        undefined,
                        {sensitivity : "base"}
                    );
                });

                for (const entry of entries) {
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
            loadDirectoryTree();
            updateWriteControls();
        });

        refreshTreeButton.addEventListener("click", loadDirectoryTree);

        function updateWriteControls() {
            const writable = volumeSelector.value === "sd";
            createFolderButton.disabled = !writable;
            createFileButton.disabled = !writable;
            uploadInput.disabled = !writable;
            formatButton.disabled = !writable;
        }

        createFolderButton.addEventListener("click", async () => {
            const name = window.prompt("New folder name:");

            if (!name)
                return;

            try {
                await fileOperation("mkdir", joinPath(currentPath, name));
                await refreshAfterMutation(`Folder ${name} created`);
            } catch (error) {
                setStatus(`Create folder failed: ${error.message}`, true);
            }
        });

        createFileButton.addEventListener("click", async () => {
            const name = window.prompt("New file name:");

            if (!name)
                return;

            try {
                await fileOperation("create", joinPath(currentPath, name));
                await refreshAfterMutation(`File ${name} created`);
            } catch (error) {
                setStatus(`Create file failed: ${error.message}`, true);
            }
        });

        uploadInput.addEventListener("change", async () => {
            const files = [...uploadInput.files];

            for (let index = 0; index < files.length; ++index) {
                const file = files[index];
                uploadProgress.textContent =
                    `Uploading ${index + 1}/${files.length}: ${file.name}`;

                try {
                    await fileOperation("upload", joinPath(currentPath, file.name), file);
                } catch (error) {
                    setStatus(`Upload failed: ${error.message}`, true);
                    uploadInput.value = "";
                    uploadProgress.textContent = "";
                    return;
                }
            }

            uploadInput.value = "";
            uploadProgress.textContent = "";
            await refreshAfterMutation(
                `${files.length} file${files.length === 1 ? "" : "s"} uploaded`
            );
        });

        formatButton.addEventListener("click", async () => {
            const confirmation = window.prompt(
                "Formatting permanently deletes the entire SD card. Type FORMAT to continue:"
            );

            if (confirmation !== "FORMAT")
                return;

            formatButton.disabled = true;
            setStatus("Formatting SD card...");

            try {
                await fileOperation("format", "/", null, confirmation);
                currentPath = "/";
                await refreshAfterMutation(
                    "SD card formatted and Spectra folders recreated"
                );
            } catch (error) {
                setStatus(`Format failed: ${error.message}`, true);
            } finally {
                updateWriteControls();
            }
        });

        document.getElementById("file-viewer-close").addEventListener(
            "click",
            () => viewer.close()
        );

        loadFiles();
        loadDirectoryTree();
        updateWriteControls();
    
    }

    // ===== legacy CAN WebSocket test =====
    function init_legacy_websocket_test() {

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

    function init_diagnostics() {
        const REFRESH_INTERVAL_MS = 10000;
        const HISTORY_POINT_COUNT = 60;

        let refreshTimer = null;
        let benchmarkPollTimer = null;
        let refreshInProgress = false;
        let previousSample = null;
        let previousCounters = null;
        let previousQueueDrops = null;
        let previousHealthCounters = null;
        let latestSystem = null;
        let latestDiagnostics = null;
        let latestHealth = null;

        const history = {
            cpu: [],
            heap: [],
            primaryRx: [],
            secondaryRx: [],
            queue: []
        };

        const element = id =>
            document.getElementById(id);

        const setText = (id, value) => {
            const target = element(id);

            if (target !== null) {
                target.textContent = value;
            }
        };

        function formatBytes(value) {
            const bytes = Number(value ?? 0);

            if (!Number.isFinite(bytes)) {
                return "—";
            }

            if (bytes >= (1024 * 1024 * 1024)) {
                return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GiB`;
            }

            if (bytes >= (1024 * 1024)) {
                return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
            }

            return `${(bytes / 1024).toFixed(1)} KiB`;
        }

        function formatSpeed(value) {
            const bytesPerSecond = Number(value ?? 0);

            if (!Number.isFinite(bytesPerSecond) ||
                (bytesPerSecond <= 0)) {

                return "—";
            }

            return `${(bytesPerSecond / (1024 * 1024)).toFixed(2)} MiB/s`;
        }

        function formatMicroseconds(value) {
            const microseconds = Number(value ?? 0);

            if (!Number.isFinite(microseconds) ||
                (microseconds <= 0)) {

                return "—";
            }

            return microseconds >= 1000
                ? `${(microseconds / 1000).toFixed(1)} ms`
                : `${microseconds.toFixed(0)} µs`;
        }

        function formatCount(value) {
            return Number(value ?? 0).toLocaleString();
        }

        function formatUptime(value) {
            let seconds = Math.max(0, Number(value ?? 0));
            const days = Math.floor(seconds / 86400);
            seconds %= 86400;
            const hours = Math.floor(seconds / 3600);
            seconds %= 3600;
            const minutes = Math.floor(seconds / 60);

            return days > 0
                ? `${days}d ${hours}h ${minutes}m`
                : `${hours}h ${minutes}m`;
        }

        function formatQueue(current, peak, capacity) {
            return `${formatCount(current)} / ${formatCount(capacity)} · peak ${formatCount(peak)}`;
        }

        function counterDelta(current, previous) {
            const currentValue = Number(current ?? 0);
            const previousValue = Number(previous ?? 0);

            if ((previous === undefined) ||
                !Number.isFinite(currentValue) ||
                !Number.isFinite(previousValue) ||
                (currentValue < previousValue)) {

                return null;
            }

            return currentValue - previousValue;
        }

        function formatDelta(value) {
            return value === null
                ? "(+—)"
                : `(+${formatCount(value)})`;
        }

        function formatCounterWithDelta(value, previous) {
            return `${formatCount(value)} ${formatDelta(counterDelta(value, previous))}`;
        }

        function appendHistory(series, value) {
            series.push(Number.isFinite(value) ? value : 0);

            if (series.length > HISTORY_POINT_COUNT) {
                series.shift();
            }
        }

        function drawHistory(canvasId, seriesList, colors, fixedMaximum = null) {
            const canvas = element(canvasId);

            if (canvas === null) {
                return;
            }

            const width = Math.max(1, Math.floor(canvas.clientWidth));
            const height = Math.max(1, Math.floor(canvas.clientHeight));
            const scale = Math.max(1, window.devicePixelRatio || 1);

            if ((canvas.width !== Math.floor(width * scale)) ||
                (canvas.height !== Math.floor(height * scale))) {

                canvas.width = Math.floor(width * scale);
                canvas.height = Math.floor(height * scale);
            }

            const context = canvas.getContext("2d");
            context.setTransform(scale, 0, 0, scale, 0, 0);
            context.clearRect(0, 0, width, height);

            context.strokeStyle = "#27313d";
            context.lineWidth = 1;

            for (let line = 1; line < 4; line += 1) {
                const y = Math.round((height * line) / 4) + 0.5;
                context.beginPath();
                context.moveTo(0, y);
                context.lineTo(width, y);
                context.stroke();
            }

            const maximum = fixedMaximum ?? Math.max(
                1,
                ...seriesList.flat()
            );

            seriesList.forEach((series, seriesIndex) => {
                if (series.length === 0) {
                    return;
                }

                context.strokeStyle = colors[seriesIndex];
                context.lineWidth = 1.5;
                context.lineJoin = "round";
                context.beginPath();

                series.forEach((value, index) => {
                    const x = series.length === 1
                        ? width
                        : (index * width) / (HISTORY_POINT_COUNT - 1);
                    const y = height -
                        (Math.min(maximum, Math.max(0, value)) / maximum) *
                        (height - 2) - 1;

                    if (index === 0) {
                        context.moveTo(x, y);
                    } else {
                        context.lineTo(x, y);
                    }
                });

                context.stroke();
            });
        }

        function queueUsage(current, capacity) {
            const currentValue = Number(current ?? 0);
            const capacityValue = Number(capacity ?? 0);

            return capacityValue > 0
                ? (currentValue * 100) / capacityValue
                : 0;
        }

        function updateHistory(system, diagnostics) {
            const now = performance.now();
            const can = diagnostics.can ?? {};
            const consumers = diagnostics.consumers ?? {};
            const heap = diagnostics.heap ?? {};
            const elapsedSeconds = previousSample === null
                ? 0
                : Math.max(0.001, (now - previousSample.time) / 1000);
            const primaryRxDelta = previousSample === null
                ? 0
                : counterDelta(
                    can.primary_received_frames,
                    previousSample.primaryRx
                );
            const secondaryRxDelta = previousSample === null
                ? 0
                : counterDelta(
                    can.secondary_received_frames,
                    previousSample.secondaryRx
                );
            const primaryRate = (previousSample === null) ||
                (primaryRxDelta === null)
                ? 0
                : primaryRxDelta / elapsedSeconds;
            const secondaryRate = (previousSample === null) ||
                (secondaryRxDelta === null)
                ? 0
                : secondaryRxDelta / elapsedSeconds;
            const maximumQueueUsage = Math.max(
                queueUsage(can.router_queue_current, can.router_queue_capacity),
                queueUsage(can.monitor_queue_current, can.monitor_queue_capacity),
                queueUsage(consumers.stream_queue_current, consumers.stream_queue_capacity),
                queueUsage(consumers.logger_queue_current, consumers.logger_queue_capacity)
            );

            appendHistory(history.cpu, Number(system.cpu_usage ?? 0));
            appendHistory(
                history.heap,
                Number(heap.internal_free ?? system.free_heap ?? 0) / 1024
            );
            appendHistory(history.primaryRx, primaryRate);
            appendHistory(history.secondaryRx, secondaryRate);
            appendHistory(history.queue, maximumQueueUsage);

            previousSample = {
                time: now,
                primaryRx: Number(can.primary_received_frames ?? 0),
                secondaryRx: Number(can.secondary_received_frames ?? 0)
            };

            setText(
                "diagnostics-cpu-history-value",
                `${Number(system.cpu_usage ?? 0).toFixed(1)}%`
            );
            setText(
                "diagnostics-heap-history-value",
                formatBytes(heap.internal_free ?? system.free_heap)
            );
            setText(
                "diagnostics-can-history-value",
                `${primaryRate.toFixed(0)} / ${secondaryRate.toFixed(0)} frame/s`
            );
            setText(
                "diagnostics-queue-history-value",
                `${maximumQueueUsage.toFixed(1)}% max`
            );

            drawHistory("diagnostics-cpu-history", [history.cpu], ["#3b82f6"], 100);
            drawHistory("diagnostics-heap-history", [history.heap], ["#a78bfa"]);
            drawHistory(
                "diagnostics-can-history",
                [history.primaryRx, history.secondaryRx],
                ["#3b82f6", "#31c48d"]
            );
            drawHistory("diagnostics-queue-history", [history.queue], ["#f6b73c"], 100);
        }

        function setBadge(id, text, state) {
            const badge = element(id);

            if (badge === null) {
                return;
            }

            badge.textContent = text;
            badge.classList.remove("ok", "warning", "error");

            if (state !== "") {
                badge.classList.add(state);
            }
        }

        function updateHealth(system, diagnostics) {
            const heap = diagnostics.heap ?? {};
            const can = diagnostics.can ?? {};
            const consumers = diagnostics.consumers ?? {};
            const queues = Array.isArray(diagnostics.queues)
                ? diagnostics.queues
                : [];
            const tasks = Array.isArray(diagnostics.tasks)
                ? diagnostics.tasks
                : [];
            const reasons = [];
            let severity = 0;

            const addReason = (level, text) => {
                severity = Math.max(severity, level);
                reasons.push(text);
            };

            const cpu = Number(system.cpu_usage ?? 0);
            const internalFree = Number(
                heap.internal_free ?? system.free_heap ?? 0
            );
            const internalLargest = Number(
                heap.internal_largest_block ?? 0
            );

            if (cpu >= 90) {
                addReason(2, `CPU load is critical at ${cpu.toFixed(1)}%`);
            } else if (cpu >= 75) {
                addReason(1, `CPU load is high at ${cpu.toFixed(1)}%`);
            }

            if (internalFree < (16 * 1024)) {
                addReason(2, `Internal heap is critically low: ${formatBytes(internalFree)}`);
            } else if (internalFree < (32 * 1024)) {
                addReason(1, `Internal heap is low: ${formatBytes(internalFree)}`);
            }

            if (internalLargest < (6 * 1024)) {
                addReason(2, `Largest internal block is only ${formatBytes(internalLargest)}`);
            } else if (internalLargest < (12 * 1024)) {
                addReason(1, `Largest internal block is low: ${formatBytes(internalLargest)}`);
            }

            if (!can.primary_running) {
                addReason(1, "Primary CAN service is stopped");
            } else if (!can.primary_driver_available) {
                addReason(1, "Primary CAN driver state is unavailable");
            } else if (Number(can.primary_driver_state) === 4) {
                addReason(2, "Primary CAN is bus-off");
            } else if (Number(can.primary_driver_state) >= 2) {
                addReason(1, "Primary CAN controller is not error-active");
            }

            if (!can.secondary_running) {
                addReason(1, "Secondary CAN service is stopped");
            } else if (!can.secondary_driver_available) {
                addReason(1, "Secondary CAN driver state is unavailable");
            } else if (Number(can.secondary_driver_state) === 4) {
                addReason(2, "Secondary CAN is bus-off");
            } else if (Number(can.secondary_driver_state) >= 2) {
                addReason(1, "Secondary CAN controller is not error-active");
            }

            for (const queue of queues) {
                if (!queue.available) {
                    continue;
                }

                const usage = queueUsage(queue.current, queue.capacity);

                if (usage >= 90) {
                    addReason(
                        2,
                        `${queue.owner} queue is ${usage.toFixed(1)}% full`
                    );
                } else if (usage >= 75) {
                    addReason(
                        1,
                        `${queue.owner} queue is ${usage.toFixed(1)}% full`
                    );
                }
            }

            const systemTaskNames = new Set([
                "IDLE0",
                "IDLE1",
                "ipc0",
                "ipc1",
                "esp_timer",
                "sys_evt",
                "Tmr Svc"
            ]);
            let lowestApplicationStack = null;
            let lowestSystemStack = null;

            for (const task of tasks) {
                const reserve = Number(task.stack_high_watermark_bytes ?? 0);
                const stack = {
                    name: task.name ?? "Unknown task",
                    reserve
                };

                if (reserve <= 0) {
                    continue;
                }

                if (systemTaskNames.has(stack.name)) {
                    if ((lowestSystemStack === null) ||
                        (reserve < lowestSystemStack.reserve)) {

                        lowestSystemStack = stack;
                    }
                } else if ((lowestApplicationStack === null) ||
                           (reserve < lowestApplicationStack.reserve)) {

                    lowestApplicationStack = stack;
                }
            }

            if ((lowestApplicationStack !== null) &&
                (lowestApplicationStack.reserve < 512)) {

                addReason(
                    2,
                    `${lowestApplicationStack.name} stack reserve is ` +
                    formatBytes(lowestApplicationStack.reserve)
                );
            } else if ((lowestApplicationStack !== null) &&
                       (lowestApplicationStack.reserve < 1024)) {

                addReason(
                    1,
                    `${lowestApplicationStack.name} stack reserve is ` +
                    formatBytes(lowestApplicationStack.reserve)
                );
            }

            if ((lowestSystemStack !== null) &&
                (lowestSystemStack.reserve < 128)) {

                addReason(
                    2,
                    `${lowestSystemStack.name} system stack reserve is ` +
                    formatBytes(lowestSystemStack.reserve)
                );
            }

            if (!system.sd_card_mounted) {
                addReason(1, "SD card is not mounted");
            }

            if ((Number(system.uptime_sec ?? 0) >= 60) &&
                !system.time?.synchronized) {

                addReason(1, "System time is not synchronized");
            }

            const healthCounters = {
                router: can.router_dropped_events,
                monitor: can.monitor_dropped_events,
                primary_rx: can.primary_dropped_rx_frames,
                primary_tx: can.primary_dropped_confirmations,
                primary_bus: can.primary_bus_errors,
                primary_ack: can.primary_ack_errors,
                secondary_rx: can.secondary_dropped_rx_frames,
                secondary_overflow: can.secondary_rx_overflows,
                secondary_tef: can.secondary_tef_overflows,
                secondary_receive: can.secondary_receive_errors,
                secondary_tx_event: can.secondary_tx_event_errors,
                secondary_bus: can.secondary_bus_errors,
                secondary_tx: can.secondary_tx_failures,
                logger_drop: consumers.logger_dropped_events,
                logger_write: consumers.logger_write_failures,
                logger_sync: consumers.logger_sync_failures
            };

            queues
                .filter(queue => [
                    "Network",
                    "Buzzer",
                    "ISO-TP",
                    "System logging"
                ].includes(queue.owner))
                .forEach((queue, index) => {
                    healthCounters[`auxiliary_queue_${index}`] = queue.dropped;
                });

            if (previousHealthCounters !== null) {
                const newErrors = Object.entries(healthCounters).reduce(
                    (total, [name, value]) =>
                        total +
                        (counterDelta(
                            value,
                            previousHealthCounters[name]
                        ) ?? 0),
                    0
                );

                if (newErrors > 0) {
                    addReason(
                        2,
                        `${formatCount(newErrors)} new error or dropped event counters`
                    );
                }
            }

            previousHealthCounters = healthCounters;

            const health = element("diagnostics-health");
            const reasonList = element("diagnostics-health-reasons");
            const states = ["Healthy", "Warning", "Critical"];
            const classes = ["", "warning", "error"];

            health.classList.remove("warning", "error");

            if (classes[severity] !== "") {
                health.classList.add(classes[severity]);
            }

            setText("diagnostics-health-state", states[severity]);
            reasonList.replaceChildren();

            const visibleReasons = reasons.length > 0
                ? reasons
                : ["No active problems detected."];

            for (const reason of visibleReasons) {
                const item = document.createElement("li");
                item.textContent = reason;
                reasonList.append(item);
            }

            return {
                state: states[severity],
                severity: severity === 0
                    ? "ok"
                    : classes[severity],
                reasons: [...reasons]
            };
        }

        function updateSystem(system, diagnostics) {
            const heap = diagnostics.heap ?? {};
            const benchmark = diagnostics.storage_benchmark ?? {};
            const cpu = Number(system.cpu_usage ?? 0);
            const internalFree = Number(heap.internal_free ?? system.free_heap ?? 0);
            const internalMinimum = Number(heap.internal_minimum_free ?? system.minimum_free_heap ?? 0);
            const internalLargest = Number(heap.internal_largest_block ?? 0);

            setText("diagnostics-cpu", `${cpu.toFixed(1)}%`);
            element("diagnostics-cpu-meter").style.width =
                `${Math.min(100, Math.max(0, cpu))}%`;
            setText("diagnostics-internal", formatBytes(internalFree));
            setText("diagnostics-internal-minimum", `Minimum ${formatBytes(internalMinimum)}`);
            setText(
                "diagnostics-temperature",
                system.chip_temperature_valid
                    ? `${Number(system.chip_temperature_celsius).toFixed(1)} °C`
                    : "—"
            );
            setText(
                "diagnostics-temperature-state",
                system.chip_temperature_valid ? "Sensor active" : "Sensor unavailable"
            );
            setText("diagnostics-uptime", formatUptime(system.uptime_sec));
            setText("diagnostics-reset", `Reset: ${system.reset_reason_name ?? "unknown"}`);

            setText("diagnostics-internal-free", formatBytes(internalFree));
            setText("diagnostics-internal-largest", formatBytes(internalLargest));
            setText("diagnostics-dma-free", formatBytes(heap.dma_free));
            setText("diagnostics-dma-largest", formatBytes(heap.dma_largest_block));
            setText("diagnostics-psram-free", formatBytes(heap.psram_free ?? system.psram_free));
            setText("diagnostics-psram-largest", formatBytes(heap.psram_largest_block));

            const memoryWarning =
                (internalFree < (32 * 1024)) ||
                (internalLargest < (12 * 1024));
            setBadge(
                "diagnostics-memory-state",
                memoryWarning ? "Low" : "Healthy",
                memoryWarning ? "warning" : "ok"
            );

            setText("diagnostics-sd-mounted", system.sd_card_mounted ? "Mounted" : "Not mounted");
            setText("diagnostics-sd-filesystem", system.sd_card_filesystem || "—");
            setText("diagnostics-sd-free", formatBytes(system.sd_card_free_bytes));
            setText("diagnostics-sd-used", formatBytes(system.sd_card_used_bytes));
            setText(
                "diagnostics-sd-write",
                benchmark.available
                    ? formatSpeed(benchmark.write_bytes_per_second)
                    : "Not measured"
            );
            setText(
                "diagnostics-sd-read",
                benchmark.available
                    ? `${formatSpeed(benchmark.read_bytes_per_second)} / ${formatSpeed(benchmark.raw_read_bytes_per_second)}`
                    : "Not measured"
            );
            setText(
                "diagnostics-sd-write-latency",
                benchmark.available
                    ? formatMicroseconds(benchmark.maximum_write_us)
                    : "—"
            );
            setText(
                "diagnostics-sd-sync",
                benchmark.available
                    ? formatMicroseconds(benchmark.sync_us)
                    : "—"
            );
            setText("diagnostics-time", system.time?.local ?? "Unavailable");
            setText("diagnostics-time-sync", system.time?.synchronized ? "Synchronized" : "Not synchronized");
            setBadge(
                "diagnostics-storage-state",
                system.sd_card_mounted ? "Ready" : "Unavailable",
                system.sd_card_mounted ? "ok" : "warning"
            );

            setText("diagnostics-device", system.device_name || system.device_id || "Spectra");
            setText("diagnostics-firmware", system.firmware_version ?? "—");
            setText("diagnostics-chip", `${system.chip_model ?? "—"} · ${system.chip_cores ?? 0} cores`);
            setText("diagnostics-frequency", `${system.cpu_frequency_mhz ?? 0} MHz`);
            setText("diagnostics-internet", system.internet_available ? "Available" : "Unavailable");
            setText("diagnostics-ota", system.ota_available ? "Available" : "Unavailable");
            setBadge(
                "diagnostics-network-state",
                system.internet_available ? "Online" : "Offline",
                system.internet_available ? "ok" : "warning"
            );
        }

        function updateBenchmarkControl(system, diagnostics) {
            const button = element("diagnostics-sd-benchmark");
            const benchmark = diagnostics.storage_benchmark ?? {};
            const consumers = diagnostics.consumers ?? {};
            const loggerState = Number(consumers.logger_state ?? 0);
            const loggerBusy = [1, 2, 3].includes(loggerState);
            const running = Boolean(benchmark.running);

            button.textContent = running
                ? "Benchmark running…"
                : "Run benchmark";
            button.disabled =
                !system.sd_card_mounted ||
                loggerBusy ||
                running;

            if (!system.sd_card_mounted) {
                button.title = "SD card is not mounted";
            } else if (loggerBusy) {
                button.title = "Stop CAN recording before running the benchmark";
            } else if (running) {
                button.title = "SD-card benchmark is running";
            } else {
                button.title = "Measure SD-card write and read performance";
            }
        }

        function updateCan(diagnostics) {
            const can = diagnostics.can ?? {};
            const consumers = diagnostics.consumers ?? {};
            const previous = previousCounters ?? {};

            setText("diagnostics-primary-state", can.primary_running ? "Running" : "Stopped");
            setText("diagnostics-primary-traffic", `${formatCount(can.primary_received_frames)} / ${formatCount(can.primary_transmitted_frames)}`);
            setText("diagnostics-secondary-state", can.secondary_running ? "Running" : "Stopped");
            setText("diagnostics-secondary-traffic", `${formatCount(can.secondary_received_frames)} / ${formatCount(can.secondary_transmitted_frames)}`);
            setText("diagnostics-router-queue", formatQueue(can.router_queue_current, can.router_queue_peak, can.router_queue_capacity));
            setText("diagnostics-monitor-queue", formatQueue(can.monitor_queue_current, can.monitor_queue_peak, can.monitor_queue_capacity));
            setText("diagnostics-identifiers", `${formatCount(can.monitor_identifiers)} / ${formatCount(can.monitor_identifier_capacity)}`);

            const canHealthy = can.primary_running && can.secondary_running;
            setBadge("diagnostics-can-state", canHealthy ? "Running" : "Partial", canHealthy ? "ok" : "warning");

            setText("diagnostics-stream-client", consumers.stream_client_connected ? "Connected" : "Disconnected");
            setText("diagnostics-stream-queue", formatQueue(consumers.stream_queue_current, consumers.stream_queue_peak, consumers.stream_queue_capacity));
            setText("diagnostics-stream-errors", `${formatCount(consumers.stream_dropped_events)} / ${formatCount(consumers.stream_send_failures)}`);

            const loggerStates = ["Idle", "Starting", "Recording", "Stopping", "Error"];
            setText("diagnostics-logger-state", loggerStates[consumers.logger_state] ?? "Unknown");
            setText("diagnostics-logger-queue", formatQueue(consumers.logger_queue_current, consumers.logger_queue_peak, consumers.logger_queue_capacity));
            setText("diagnostics-logger-written", `${formatCount(consumers.logger_written_events)} / ${formatBytes(consumers.logger_written_bytes)}`);
            setBadge("diagnostics-consumer-state", "Available", "ok");

            setText("diagnostics-router-drops", formatCounterWithDelta(can.router_dropped_events, previous.router_dropped_events));
            setText("diagnostics-monitor-drops", formatCounterWithDelta(can.monitor_dropped_events, previous.monitor_dropped_events));
            setText("diagnostics-primary-rx-drops", formatCounterWithDelta(can.primary_dropped_rx_frames, previous.primary_dropped_rx_frames));
            setText("diagnostics-primary-drops", formatCounterWithDelta(can.primary_dropped_confirmations, previous.primary_dropped_confirmations));
            setText("diagnostics-primary-errors", `${formatCounterWithDelta(can.primary_bus_errors, previous.primary_bus_errors)} / ${formatCounterWithDelta(can.primary_ack_errors, previous.primary_ack_errors)}`);
            setText("diagnostics-secondary-overflow", `${formatCounterWithDelta(can.secondary_dropped_rx_frames, previous.secondary_dropped_rx_frames)} / ${formatCounterWithDelta(can.secondary_rx_overflows, previous.secondary_rx_overflows)}`);
            setText("diagnostics-secondary-tef-overflow", formatCounterWithDelta(can.secondary_tef_overflows, previous.secondary_tef_overflows));
            setText("diagnostics-secondary-errors", `${formatCounterWithDelta(can.secondary_receive_errors, previous.secondary_receive_errors)} / ${formatCounterWithDelta(can.secondary_tx_event_errors, previous.secondary_tx_event_errors)}`);
            setText("diagnostics-secondary-bus-errors", `${formatCounterWithDelta(can.secondary_bus_errors, previous.secondary_bus_errors)} / ${formatCounterWithDelta(can.secondary_tx_failures, previous.secondary_tx_failures)}`);
            setText("diagnostics-logger-drops", formatCounterWithDelta(consumers.logger_dropped_events, previous.logger_dropped_events));
            setText("diagnostics-logger-errors", `${formatCounterWithDelta(consumers.logger_write_failures, previous.logger_write_failures)} / ${formatCounterWithDelta(consumers.logger_sync_failures, previous.logger_sync_failures)}`);

            const totalErrors =
                Number(can.router_dropped_events ?? 0) +
                Number(can.monitor_dropped_events ?? 0) +
                Number(can.primary_dropped_confirmations ?? 0) +
                Number(can.primary_dropped_rx_frames ?? 0) +
                Number(can.primary_bus_errors ?? 0) +
                Number(can.primary_ack_errors ?? 0) +
                Number(can.secondary_dropped_rx_frames ?? 0) +
                Number(can.secondary_rx_overflows ?? 0) +
                Number(can.secondary_tef_overflows ?? 0) +
                Number(can.secondary_receive_errors ?? 0) +
                Number(can.secondary_tx_event_errors ?? 0) +
                Number(can.secondary_bus_errors ?? 0) +
                Number(can.secondary_tx_failures ?? 0) +
                Number(consumers.logger_dropped_events ?? 0) +
                Number(consumers.logger_write_failures ?? 0) +
                Number(consumers.logger_sync_failures ?? 0);
            const currentCounters = {
                router_dropped_events: can.router_dropped_events,
                monitor_dropped_events: can.monitor_dropped_events,
                primary_dropped_confirmations: can.primary_dropped_confirmations,
                primary_dropped_rx_frames: can.primary_dropped_rx_frames,
                primary_bus_errors: can.primary_bus_errors,
                primary_ack_errors: can.primary_ack_errors,
                secondary_dropped_rx_frames: can.secondary_dropped_rx_frames,
                secondary_rx_overflows: can.secondary_rx_overflows,
                secondary_tef_overflows: can.secondary_tef_overflows,
                secondary_receive_errors: can.secondary_receive_errors,
                secondary_tx_event_errors: can.secondary_tx_event_errors,
                secondary_bus_errors: can.secondary_bus_errors,
                secondary_tx_failures: can.secondary_tx_failures,
                logger_dropped_events: consumers.logger_dropped_events,
                logger_write_failures: consumers.logger_write_failures,
                logger_sync_failures: consumers.logger_sync_failures
            };
            const totalNewErrors = previousCounters === null
                ? null
                : Object.entries(currentCounters).reduce(
                    (total, [name, value]) =>
                        total + (counterDelta(value, previous[name]) ?? 0),
                    0
                );

            setBadge(
                "diagnostics-error-state",
                totalNewErrors === null
                    ? `${formatCount(totalErrors)} total`
                    : totalNewErrors === 0
                        ? "Stable"
                        : `+${formatCount(totalNewErrors)} new`,
                (totalNewErrors === null) || (totalNewErrors === 0)
                    ? "ok"
                    : "warning"
            );

            previousCounters = currentCounters;
        }

        function updateTasks(diagnostics) {
            const body = element("diagnostics-task-list");
            const tasks = Array.isArray(diagnostics.tasks)
                ? diagnostics.tasks
                : [];

            body.replaceChildren();

            setBadge(
                "diagnostics-task-count",
                diagnostics.tasks_available
                    ? `${tasks.length} tasks`
                    : "Unavailable",
                diagnostics.tasks_available ? "ok" : "warning"
            );

            if (tasks.length === 0) {
                const row = document.createElement("tr");
                const cell = document.createElement("td");
                cell.colSpan = 6;
                cell.textContent = "Task statistics are unavailable.";
                row.append(cell);
                body.append(row);
                return;
            }

            tasks.sort((left, right) =>
                Number(right.runtime_percent ?? 0) -
                Number(left.runtime_percent ?? 0)
            );

            for (const task of tasks) {
                const row = document.createElement("tr");
                const values = [
                    task.name ?? "—",
                    task.state ?? "—",
                    Number(task.core) < 0 ? "Any" : task.core,
                    task.priority ?? "—",
                    `${Number(task.runtime_percent ?? 0).toFixed(2)}%`,
                    formatBytes(task.stack_high_watermark_bytes)
                ];

                for (const value of values) {
                    const cell = document.createElement("td");
                    cell.textContent = value;
                    row.append(cell);
                }

                body.append(row);
            }
        }

        function updateQueues(diagnostics) {
            const body = element("diagnostics-queue-list");
            const queues = Array.isArray(diagnostics.queues)
                ? diagnostics.queues
                : [];
            const currentDrops = {};
            let highestUsage = 0;
            let newDrops = 0;

            body.replaceChildren();

            if (queues.length === 0) {
                const row = document.createElement("tr");
                const cell = document.createElement("td");
                cell.colSpan = 7;
                cell.textContent = "Queue statistics are unavailable.";
                row.append(cell);
                body.append(row);
                setBadge("diagnostics-queue-state", "Unavailable", "warning");
                return;
            }

            for (const queue of queues) {
                const row = document.createElement("tr");
                const capacity = Number(queue.capacity ?? 0);
                const current = Number(queue.current ?? 0);
                const usage = capacity > 0
                    ? Math.min(100, Math.max(0, (current * 100) / capacity))
                    : 0;
                const key = `${queue.owner}:${queue.name}`;
                const droppedDelta = previousQueueDrops === null
                    ? null
                    : counterDelta(queue.dropped, previousQueueDrops[key]);

                currentDrops[key] = Number(queue.dropped ?? 0);
                highestUsage = Math.max(highestUsage, usage);
                newDrops += droppedDelta ?? 0;

                const values = [
                    queue.owner ?? "—",
                    queue.name ?? "—"
                ];

                for (const value of values) {
                    const cell = document.createElement("td");
                    cell.textContent = value;
                    row.append(cell);
                }

                const usageCell = document.createElement("td");
                const usageContainer = document.createElement("span");
                const meter = document.createElement("span");
                const meterValue = document.createElement("span");
                const usageText = document.createElement("span");

                usageContainer.className = "diagnostics-queue-usage";
                meter.className = "diagnostics-queue-meter";

                if (usage >= 90) {
                    meter.classList.add("error");
                } else if (usage >= 75) {
                    meter.classList.add("warning");
                }

                meterValue.style.width = `${usage}%`;
                usageText.textContent = queue.available
                    ? `${usage.toFixed(1)}%`
                    : "Unavailable";
                meter.append(meterValue);
                usageContainer.append(meter, usageText);
                usageCell.append(usageContainer);
                row.append(usageCell);

                const numericValues = [
                    queue.available ? formatCount(current) : "—",
                    queue.available ? formatCount(queue.peak) : "—",
                    queue.available ? formatCount(capacity) : "—",
                    queue.available
                        ? `${formatCount(queue.dropped)} ${formatDelta(droppedDelta)}`
                        : "—"
                ];

                for (const value of numericValues) {
                    const cell = document.createElement("td");
                    cell.textContent = value;
                    row.append(cell);
                }

                body.append(row);
            }

            previousQueueDrops = currentDrops;

            if (newDrops > 0) {
                setBadge(
                    "diagnostics-queue-state",
                    `+${formatCount(newDrops)} dropped`,
                    "error"
                );
            } else if (highestUsage >= 90) {
                setBadge("diagnostics-queue-state", "Critical", "error");
            } else if (highestUsage >= 75) {
                setBadge("diagnostics-queue-state", "High", "warning");
            } else {
                setBadge("diagnostics-queue-state", "Healthy", "ok");
            }
        }

        async function refresh() {
            if (refreshInProgress) {
                return false;
            }

            refreshInProgress = true;
            element("diagnostics-download").disabled = true;
            element("diagnostics-reset-history").disabled = true;

            try {
                const [systemResponse, diagnosticsResponse] = await Promise.all([
                    fetch("/api/system", {cache: "no-store"}),
                    fetch("/api/diagnostics", {cache: "no-store"})
                ]);

                if (!systemResponse.ok || !diagnosticsResponse.ok) {
                    throw new Error("Device rejected the diagnostics request");
                }

                const [system, diagnostics] = await Promise.all([
                    systemResponse.json(),
                    diagnosticsResponse.json()
                ]);

                updateSystem(system, diagnostics);
                updateBenchmarkControl(system, diagnostics);
                updateCan(diagnostics);
                updateHistory(system, diagnostics);
                updateQueues(diagnostics);
                updateTasks(diagnostics);
                latestHealth = updateHealth(system, diagnostics);
                latestSystem = system;
                latestDiagnostics = diagnostics;
                setText("status", "Connected");
                setText("diagnostics-updated", `Updated ${new Date().toLocaleTimeString()}`);
                element("status").classList.remove("warning");
                return true;
            } catch (error) {
                setText("status", "Unavailable");
                setText("diagnostics-updated", error.message);
                element("status").classList.add("warning");
                return false;
            } finally {
                refreshInProgress = false;
                element("diagnostics-download").disabled =
                    latestDiagnostics === null;
                element("diagnostics-reset-history").disabled =
                    latestDiagnostics === null;
            }
        }

        function resetHistory() {
            if ((latestSystem === null) ||
                (latestDiagnostics === null)) {

                return;
            }

            for (const series of Object.values(history)) {
                series.length = 0;
            }

            const can = latestDiagnostics.can ?? {};

            previousSample = {
                time: performance.now(),
                primaryRx: Number(can.primary_received_frames ?? 0),
                secondaryRx: Number(can.secondary_received_frames ?? 0)
            };
            previousCounters = null;
            previousQueueDrops = null;
            previousHealthCounters = null;

            updateCan(latestDiagnostics);
            updateQueues(latestDiagnostics);
            latestHealth = updateHealth(latestSystem, latestDiagnostics);

            setText("diagnostics-can-history-value", "0 / 0 frame/s");

            drawHistory(
                "diagnostics-cpu-history",
                [history.cpu],
                ["#3b82f6"],
                100
            );
            drawHistory(
                "diagnostics-heap-history",
                [history.heap],
                ["#a78bfa"]
            );
            drawHistory(
                "diagnostics-can-history",
                [history.primaryRx, history.secondaryRx],
                ["#3b82f6", "#31c48d"]
            );
            drawHistory(
                "diagnostics-queue-history",
                [history.queue],
                ["#f6b73c"],
                100
            );

            setText(
                "diagnostics-updated",
                `History reset ${new Date().toLocaleTimeString()}`
            );
        }

        function reportFileTimestamp(date) {
            const value = number =>
                String(number).padStart(2, "0");

            return `${date.getFullYear()}${value(date.getMonth() + 1)}` +
                `${value(date.getDate())}-${value(date.getHours())}` +
                `${value(date.getMinutes())}${value(date.getSeconds())}`;
        }

        async function downloadReport() {
            const button = element("diagnostics-download");
            button.disabled = true;

            try {
                const refreshed = await refresh();

                if (!refreshed ||
                    (latestSystem === null) ||
                    (latestDiagnostics === null) ||
                    (latestHealth === null)) {

                    throw new Error("Unable to collect a fresh diagnostic snapshot");
                }

                const generatedAt = new Date();
                const report = {
                    schema: "spectra-diagnostic-report",
                    schema_version: 1,
                    generated_at: generatedAt.toISOString(),
                    browser: {
                        user_agent: navigator.userAgent,
                        language: navigator.language,
                        online: navigator.onLine
                    },
                    health: latestHealth,
                    system: latestSystem,
                    diagnostics: latestDiagnostics,
                    browser_history: {
                        sample_interval_ms: REFRESH_INTERVAL_MS,
                        maximum_points: HISTORY_POINT_COUNT,
                        cpu_percent: [...history.cpu],
                        internal_heap_kib: [...history.heap],
                        primary_rx_frames_per_second: [...history.primaryRx],
                        secondary_rx_frames_per_second: [...history.secondaryRx],
                        maximum_queue_usage_percent: [...history.queue]
                    }
                };
                const blob = new Blob(
                    [JSON.stringify(report, null, 2)],
                    {type: "application/json"}
                );
                const url = URL.createObjectURL(blob);
                const link = document.createElement("a");

                link.href = url;
                link.download =
                    `diagnostic-report-${reportFileTimestamp(generatedAt)}.json`;
                document.body.append(link);
                link.click();
                link.remove();
                URL.revokeObjectURL(url);
                setText(
                    "diagnostics-updated",
                    `Report saved ${generatedAt.toLocaleTimeString()}`
                );
            } catch (error) {
                setText("diagnostics-updated", error.message);
            } finally {
                button.disabled = latestDiagnostics === null;
            }
        }

        async function startSdBenchmark() {
            const button = element("diagnostics-sd-benchmark");

            button.disabled = true;
            button.textContent = "Starting…";

            try {
                const response = await fetch(
                    "/api/diagnostics/sd-benchmark",
                    {
                        method: "POST",
                        cache: "no-store"
                    }
                );
                const result = await response.json();

                if (!response.ok || !result.success) {
                    throw new Error(
                        result.message || "Failed to start SD benchmark"
                    );
                }

                button.textContent = "Benchmark running…";
                setText("diagnostics-updated", "SD benchmark started");

                await refresh();

                if (benchmarkPollTimer === null) {
                    benchmarkPollTimer = window.setInterval(
                        async () => {
                            await refresh();

                            if (!latestDiagnostics?.storage_benchmark?.running) {
                                clearInterval(benchmarkPollTimer);
                                benchmarkPollTimer = null;
                            }
                        },
                        2000
                    );
                }
            } catch (error) {
                setText("diagnostics-updated", error.message);

                if ((latestSystem !== null) &&
                    (latestDiagnostics !== null)) {

                    updateBenchmarkControl(
                        latestSystem,
                        latestDiagnostics
                    );
                }
            }
        }

        element("diagnostics-refresh").addEventListener("click", refresh);
        element("diagnostics-reset-history").addEventListener(
            "click",
            resetHistory
        );
        element("diagnostics-download").addEventListener("click", downloadReport);
        element("diagnostics-sd-benchmark").addEventListener(
            "click",
            startSdBenchmark
        );

        refresh();
        refreshTimer = setInterval(refresh, REFRESH_INTERVAL_MS);

        window.addEventListener("pagehide", () => {
            clearInterval(refreshTimer);
            clearInterval(benchmarkPollTimer);
        });
    }

    function initResizableCanTables(root = document) {
        const tables = root.querySelectorAll(
            ".resizable-can-row[data-resize-key]"
        );

        for (const table of tables) {
            const storageKey =
                `spectra:can-table-height:${table.dataset.resizeKey}`;

            try {
                const storedHeight =
                    Number(
                        localStorage.getItem(storageKey)
                    );

                if (Number.isFinite(storedHeight) &&
                    (storedHeight >= 210)) {

                    table.style.height = `${storedHeight}px`;
                }
            } catch (_) {
                /*
                 * Resizing remains available without browser storage.
                 */
            }
        }

        if (!("ResizeObserver" in window)) {
            return;
        }

        const timers = new WeakMap();
        const observer = new ResizeObserver(entries => {
            for (const entry of entries) {
                const height =
                    Math.round(
                        entry.target.getBoundingClientRect().height
                    );
                const resizeKey =
                    entry.target.dataset.resizeKey;

                clearTimeout(
                    timers.get(entry.target)
                );

                timers.set(
                    entry.target,
                    setTimeout(
                        () => {
                            try {
                                localStorage.setItem(
                                    `spectra:can-table-height:${resizeKey}`,
                                    String(
                                        height
                                    )
                                );
                            } catch (_) {
                                /*
                                 * The height still applies to this page
                                 * session.
                                 */
                            }
                        },
                        150
                    )
                );
            }
        });

        for (const table of tables) {
            observer.observe(table);
        }
    }

    // ===== can_logger.html =====
    function init_logger() {

        "use strict";

        const element = id => document.getElementById(id);
        const eventNames = ["RX", "TX queued", "TX done", "TX failed", "TX aborted"];
        const channels = [0, 1].map(() => ({
            identifiers: new Map(), history: [], historyOffset: 0, rx: 0, tx: 0,
            previousRx: 0, previousTx: 0, dirty: true
        }));
        let socket = null;
        let paused = false;
        let pending = false;
        let received = 0;
        let previousRateTime = performance.now();
        let subscriptionTimer = null;
        const loggerLimits = [500, 1000, 5000, 10000, 25000, 50000];
        let historyLimit = 500;

        try {
            historyLimit =
                Number(localStorage.getItem("spectra:logger-buffer-limit"));
        } catch (_) {
            /* The default remains available when browser storage is disabled. */
        }

        if (!loggerLimits.includes(historyLimit))
            historyLimit = 500;

        element("logger-buffer-limit").value = String(historyLimit);

        for (let bus = 0; bus < 2; bus++) {
            const section = document.createElement("section");
            section.className = "channel " + (bus ? "secondary" : "");
            section.innerHTML = '<div class="channel-title"><span class="tag">CH ' + (bus + 1) +
                '</span><h2>' + (bus ? "Secondary CAN" : "Primary CAN") +
                '</h2><span class="rate" id="rate' + bus + '">RX 0/s · TX 0/s</span></div>' +
                '<div class="tables resizable-can-row" data-resize-key="logger-' + bus + '-tables"><div class="panel"><div class="panel-title">Identifiers<span id="ids' + bus +
                '">0 IDs</span></div><div class="scroll"><table class="identifier-table"><thead><tr><th>ID / type</th><th>Count</th><th>Data</th><th>Δ ms</th></tr></thead><tbody id="idrows' + bus +
                '"></tbody></table></div></div><div class="panel"><div class="panel-title">Event buffer<span id="buffer' + bus +
                '">0 events</span></div><div class="scroll"><table class="event-table"><thead><tr><th>No.</th><th>Time / source</th><th>Event</th><th>ID</th><th>Data</th><th>Text</th></tr></thead><tbody id="history' + bus +
                '"></tbody></table></div></div></div>';
            element("channels").append(section);
        }

        initResizableCanTables(
            element("channels")
        );

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
                    type, bus,
                    direction: view.getUint8(offset + 2),
                    source,
                    flags: view.getUint8(offset + 3),
                    dlc: view.getUint8(offset + 4),
                    sequence: view.getUint32(offset + 8, true),
                    transaction: view.getUint32(offset + 12, true),
                    nativeSequence: view.getUint32(offset + 16, true),
                    id: view.getUint32(offset + 20, true),
                    result: view.getUint32(offset + 24, true),
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

            if ((channel.history.length - channel.historyOffset) > historyLimit)
                channel.historyOffset++;

            if (channel.historyOffset >= 1024) {
                channel.history.splice(0, channel.historyOffset);
                channel.historyOffset = 0;
            }
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

        function payloadText(event) {
            if (event.flags & 2)
                return "—";

            const limit = 12;
            const text = event.data
                .slice(0, limit)
                .map(byte => byte >= 32 && byte <= 126
                    ? String.fromCharCode(byte)
                    : "·")
                .join("")
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;");

            return text + (event.data.length > limit ? "…" : "");
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
                    "</td><td>" + row.count + "</td><td class=\"data-cell\">" + payload(row.event, row) +
                    "</td><td>" + (row.delta === null ? "—" : row.delta.toFixed(1)) + "</td></tr>"
                ).join("") || '<tr><td class="empty" colspan="4">No matching frames received</td></tr>';
                const history = channel.history
                    .slice(channel.historyOffset)
                    .filter(event => identifier(event).includes(query));
                element("history" + bus).innerHTML = history.slice(-500).reverse().map(event =>
                    "<tr><td>" + event.sequence + "</td><td>" + timeText(event) +
                    (event.source === 1 ? " SW" : event.source === 2 ? " HW" : "") +
                    "</td><td>" + eventNames[event.type] + "</td><td>" + identifier(event) +
                    "</td><td class=\"data-cell\">" + payload(event) + "</td><td class=\"text-cell\">" +
                    payloadText(event) + "</td></tr>"
                ).join("") || '<tr><td class="empty" colspan="6">Connect to start receiving CAN events</td></tr>';
                element("ids" + bus).textContent = channel.identifiers.size + " IDs";
                element("buffer" + bus).textContent =
                    (channel.history.length - channel.historyOffset) + " events";
            });
            element("received").textContent = received.toLocaleString();

            const primarySize =
                channels[0].history.length - channels[0].historyOffset;
            const secondarySize =
                channels[1].history.length - channels[1].historyOffset;
            const totalSize = primarySize + secondarySize;
            const totalCapacity = historyLimit * 2;
            const progress = element("logger-buffer-progress");
            const channelPeak = Math.max(primarySize, secondarySize);

            element("logger-buffer-total").textContent =
                `${totalSize.toLocaleString()} / ${totalCapacity.toLocaleString()}`;
            element("logger-buffer-primary").textContent =
                `P ${primarySize.toLocaleString()} / ${historyLimit.toLocaleString()}`;
            element("logger-buffer-secondary").textContent =
                `S ${secondarySize.toLocaleString()} / ${historyLimit.toLocaleString()}`;
            element("logger-buffer-fill").style.width =
                `${totalSize / totalCapacity * 100}%`;
            progress.setAttribute("aria-valuenow", totalSize);
            progress.setAttribute("aria-valuemax", totalCapacity);
            progress.classList.toggle(
                "warning",
                channelPeak >= historyLimit * 0.8 && channelPeak < historyLimit
            );
            progress.classList.toggle("full", channelPeak >= historyLimit);
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
        element("logger-buffer-limit").onchange = event => {
            historyLimit = Number(event.target.value);

            try {
                localStorage.setItem(
                    "spectra:logger-buffer-limit",
                    String(historyLimit)
                );
            } catch (_) {
                /* The selection still applies to the current page session. */
            }

            channels.forEach(channel => {
                const retained =
                    channel.history.length - channel.historyOffset;

                if (retained > historyLimit)
                    channel.historyOffset += retained - historyLimit;

                channel.dirty = true;
            });
            render();
        };
        element("clear").onclick = () => {
            channels.forEach(channel => {
                channel.identifiers.clear(); channel.history.length = 0;
                channel.historyOffset = 0;
                channel.rx = channel.tx = channel.previousRx = channel.previousTx = 0;
                channel.dirty = true;
            });
            received = 0;
        };

        function bufferedEvents() {
            return channels
                .flatMap(channel => channel.history.slice(channel.historyOffset))
                .sort((left, right) => left.sequence - right.sequence);
        }

        function downloadBufferedFile(data, type, extension) {
            const url = URL.createObjectURL(new Blob([data], {type}));
            const link = document.createElement("a");
            const date = new Date().toISOString().replace(/[-:]/g, "").slice(0, 15);

            link.href = url;
            link.download = `spectra-can-buffer-${date}.${extension}`;
            link.click();
            setTimeout(() => URL.revokeObjectURL(url), 1000);
        }

        function encodeBufferedScl(events) {
            const fileHeaderSize = 32;
            const eventHeaderSize = 56;
            const totalSize = events.reduce(
                (size, event) => size + eventHeaderSize + event.data.length,
                fileHeaderSize
            );
            const buffer = new ArrayBuffer(totalSize);
            const view = new DataView(buffer);
            const bytes = new Uint8Array(buffer);
            const validTimestamps = events
                .filter(event => event.source !== 0)
                .map(event => event.timestamp);
            const sessionStarted = validTimestamps.length
                ? validTimestamps.reduce(
                    (minimum, value) => value < minimum ? value : minimum
                )
                : 0n;

            bytes.set([0x53, 0x43, 0x4c, 0x31], 0);
            view.setUint16(4, 1, true);
            view.setUint16(6, fileHeaderSize, true);
            view.setUint32(8, 0x12345678, true);
            view.setUint32(12, 0, true);
            view.setBigUint64(16, 0n, true);
            view.setBigUint64(24, sessionStarted, true);

            let offset = fileHeaderSize;

            for (const event of events) {
                const recordSize = eventHeaderSize + event.data.length;
                const capture =
                    event.source !== 0 && event.timestamp >= sessionStarted
                        ? event.timestamp - sessionStarted
                        : 0n;

                view.setUint16(offset, recordSize, true);
                view.setUint8(offset + 2, 1);
                view.setUint8(offset + 3, event.type);
                view.setUint8(offset + 4, event.bus);
                view.setUint8(offset + 5, event.direction);
                view.setUint8(offset + 6, event.source);
                view.setUint8(offset + 7, event.dlc);
                view.setUint8(offset + 8, event.data.length);
                view.setUint32(offset + 12, event.flags, true);
                view.setUint32(offset + 16, event.id, true);
                view.setUint32(offset + 20, event.sequence, true);
                view.setUint32(offset + 24, event.transaction, true);
                view.setUint32(offset + 28, event.nativeSequence, true);
                view.setUint32(offset + 32, event.result, true);
                view.setBigUint64(offset + 40, event.timestamp, true);
                view.setBigUint64(offset + 48, capture, true);
                bytes.set(event.data, offset + eventHeaderSize);
                offset += recordSize;
            }

            return buffer;
        }

        function encodeBufferedAsc(events) {
            const eventLabels = [
                "RX",
                "TX_QUEUED",
                "TX_COMPLETED",
                "TX_FAILED",
                "TX_ABORTED"
            ];
            const validTimestamps = events
                .filter(event => event.source !== 0)
                .map(event => event.timestamp);
            const sessionStarted = validTimestamps.length
                ? validTimestamps.reduce(
                    (minimum, value) => value < minimum ? value : minimum
                )
                : 0n;
            const now = new Date();
            const days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
            const months = [
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
            ];
            const twoDigits = value => String(value).padStart(2, "0");
            const date =
                `${days[now.getDay()]} ${months[now.getMonth()]} ` +
                `${twoDigits(now.getDate())} ${twoDigits(now.getHours())}:` +
                `${twoDigits(now.getMinutes())}:${twoDigits(now.getSeconds())} ` +
                `${now.getFullYear()}`;
            const lines = [
                `date ${date}`,
                "base hex  timestamps absolute",
                "internal events logged",
                "Begin Triggerblock"
            ];

            for (const event of events) {
                const elapsed =
                    event.source !== 0 && event.timestamp >= sessionStarted
                        ? event.timestamp - sessionStarted
                        : 0n;
                const timestamp =
                    `${elapsed / 1000000n}.` +
                    `${elapsed % 1000000n}`.padStart(6, "0");
                const channel = event.bus === 0 ? 1 : 2;
                const extended = (event.flags & 1) !== 0;
                const remote = (event.flags & 2) !== 0;
                const fd = (event.flags & 4) !== 0;
                const direction = event.direction === 0 ? "Rx" : "Tx";
                const identifierText =
                    event.id.toString(16).toUpperCase() + (extended ? "x" : "");
                const lifecycleOnly =
                    event.type === 1 || event.type === 3 || event.type === 4;
                let line;

                if (lifecycleOnly) {
                    line =
                        `// ${timestamp} ${eventLabels[event.type]} bus=${channel} ` +
                        `id=${identifierText} transaction=${event.transaction} ` +
                        `native=${event.nativeSequence} result=${event.result | 0}`;
                } else if (fd) {
                    const brs = (event.flags & 8) !== 0 ? 1 : 0;
                    const esi = (event.flags & 16) !== 0 ? 1 : 0;

                    line =
                        `${timestamp} CANFD ${channel} ${direction} ${identifierText} ` +
                        `${brs} ${esi} ${event.dlc} ${event.data.length}`;
                } else {
                    line =
                        `${timestamp} ${channel} ${identifierText} ${direction} ` +
                        `${remote ? "r" : "d"} ${event.dlc}`;
                }

                if (!remote && !lifecycleOnly && event.data.length > 0) {
                    line += " " + event.data
                        .map(byte => byte.toString(16).toUpperCase().padStart(2, "0"))
                        .join(" ");
                }

                lines.push(line);
            }

            lines.push("End TriggerBlock");
            return lines.join("\r\n") + "\r\n";
        }

        element("export-csv").onclick = () => {
            const rows = ["bus,sequence,timestamp_us,timestamp_source,event,id,flags,data"];
            bufferedEvents().forEach(event => rows.push([
                event.bus ? "Secondary" : "Primary", event.sequence, event.timestamp.toString(),
                event.source, eventNames[event.type], identifier(event), event.flags,
                event.data.map(byte => hex(byte)).join(" ")
            ].join(",")));
            downloadBufferedFile(rows.join("\r\n"), "text/csv", "csv");
        };
        element("export-scl").onclick = () => {
            downloadBufferedFile(
                encodeBufferedScl(bufferedEvents()),
                "application/octet-stream",
                "scl"
            );
        };
        element("export-asc").onclick = () => {
            downloadBufferedFile(
                encodeBufferedAsc(bufferedEvents()),
                "text/plain",
                "asc"
            );
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
        initResizableCanTables();

        const $ = id => document.getElementById(id);
        const PAGE = 100;
        const analyzerLimits = [10000, 50000, 100000, 250000, 500000, 1000000];
        let limit = 100000;

        try {
            limit =
                Number(localStorage.getItem("spectra:analyzer-buffer-limit"));
        } catch (_) {
            /* The default remains available when browser storage is disabled. */
        }

        if (!analyzerLimits.includes(limit))
            limit = 100000;

        $("analyzer-buffer-limit").value = String(limit);
        const names = ["RX", "TX queued", "TX done", "TX failed", "TX aborted"];
        let records = [], selected = new Set(), groups = [], filtered = [];
        let socket = null, paused = false, pending = false, ackTimer = null;
        let page = 0, dirty = true, busy = false, source = "No source";
        const hex = (n, width = 2) => n.toString(16).toUpperCase().padStart(width, "0");
        const key = e => e.bus + ":" + e.id + ":" + (e.flags & 7);
        const idText = e => hex(e.id, e.flags & 1 ? 8 : 3);
        const timeText = us => (us / 1000000n).toString() + "." + (us % 1000000n).toString().padStart(6, "0");
        const dataText = e => e.flags & 2 ? "RTR" : e.data.map(b => hex(b)).join(" ");
        const payloadText = e => {
            if (e.flags & 2)
                return "—";
            const limit = 12;
            const text = e.data.slice(0, limit).map(byte =>
                byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : "·").join("");
            return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") +
                (e.data.length > limit ? "…" : "");
        };
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
            while (o < v.byteLength && result.length < limit) {
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
            $("pause").disabled = !socket || socket.readyState !== 1 || pending || records.length >= limit;
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
                            if (records.length >= limit && !paused) subscribe(true);
                        } else if (msg.type === "error") {
                            closeLive(); $("message").textContent = msg.code || "Server command failed";
                        } else if (msg.type === "stream_statistics" && msg.dropped_events > 0) {
                            $("message").textContent = "Server reports dropped events: " + msg.dropped_events;
                        }
                        return;
                    }
                    const incoming = parseLive(event.data);
                    for (const e of incoming) { if (records.length >= limit) break; records.push(e); }
                    dirty = true;
                    if (records.length >= limit) {
                        subscribe(true);
                        $("message").textContent =
                            `${limit.toLocaleString()}-event limit reached. Capture paused; later events are not retained.`;
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
                $("message").textContent = parsed.truncated
                    ? `Only the first ${limit.toLocaleString()} events were imported; the remaining file was not analyzed.`
                    : "SCL file loaded.";
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
            const progress = $("analyzer-buffer-progress");
            const fill = Math.min(100, records.length / limit * 100);

            $("analyzer-buffer-total").textContent =
                `${records.length.toLocaleString()} / ${limit.toLocaleString()}`;
            $("analyzer-buffer-fill").style.width = `${fill}%`;
            progress.setAttribute("aria-valuenow", records.length);
            progress.classList.toggle(
                "warning",
                records.length >= limit * 0.8 && records.length < limit
            );
            progress.classList.toggle("full", records.length >= limit);

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
                    '</td><td>' + time + '</td><td>' + names[e.type] + '</td><td>' + idText(e) +
                    '</td><td class="data-cell">' + dataText(e) + '</td><td class="text-cell">' +
                    payloadText(e) + '</td></tr>';
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
        $("analyzer-buffer-limit").onchange = event => {
            limit = Number(event.target.value);

            try {
                localStorage.setItem(
                    "spectra:analyzer-buffer-limit",
                    String(limit)
                );
            } catch (_) {
                /* The selection still applies to the current page session. */
            }

            if (records.length > limit) {
                records = records.slice(records.length - limit);
                selected.clear();
                page = 0;
                $("message").textContent =
                    "The oldest events were removed to apply the smaller buffer limit.";
            }

            dirty = true;
            controls();
        };
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
                    <label>Job<select name="slot"><option value="">Loading…</option></select></label>
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
                    <label>DATA increment<select name="data_mode"><option value="off">Off</option><option value="counter">Integer counter</option><option value="bytes">Masked bits per byte</option></select></label>
                    <label>Counter offset (byte)<input name="data_offset" type="number" min="0" max="63" value="0" required></label>
                    <label>Counter width<input name="data_width" type="number" min="1" max="8" value="1" required></label>
                    <label>Counter step<input name="data_step" type="number" min="1" max="4294967295" value="1" required></label>
                    <label>Counter byte order<select name="data_big_endian"><option value="0">Little endian</option><option value="1">Big endian</option></select></label>
                    <label>Bit mask (ALL or hex bytes)<input name="data_byte_mask" value="ALL" maxlength="191" autocomplete="off"></label>
                </div><p>ID and DLC wrap to the initial value at their end. DATA wraps at the selected counter width.
                    F0 increments the upper nibble; 0F increments the lower nibble. Unmasked bits stay unchanged.</p></details>
                <div class="tx-actions"><button type="button" data-tx="once" class="primary" disabled>Send once</button>
                    <button type="submit" disabled>Start sequence</button>
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
        const sendOnce = form.querySelector('[data-tx="once"]');
        let jobs = null, busy = false, polling = false;
        function updateJobOptions() {
            const select = control('slot');
            const selected = Number(select.value);

            select.replaceChildren();

            for (const job of jobs) {
                const option = document.createElement('option');
                option.value = String(job.slot);
                option.textContent = `Job ${job.slot + 1}`;
                option.selected = job.slot === selected;
                select.append(option);
            }
        }
        function controls() {
            const job = jobs?.find(j => j.slot === Number(control('slot').value));
            start.disabled = busy || !job || job.state === 'active' || job.pending !== 0;
            sendOnce.disabled = start.disabled;
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
            const dataMode = control('data_mode').value;
            control('data_offset').disabled = dataMode !== 'counter';
            control('data_width').disabled = dataMode !== 'counter';
            control('data_big_endian').disabled = dataMode !== 'counter';
            control('data_byte_mask').disabled = dataMode !== 'bytes';
            control('data_step').disabled = dataMode === 'off';
            control('data_step').max = dataMode === 'bytes'
                ? '255'
                : '4294967295';
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
                if (!Array.isArray(result.jobs) || result.jobs.length === 0)
                    throw new Error('Invalid job status');
                jobs = result.jobs;
                updateJobOptions();
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
        function byteMask(length) {
            const raw = control('data_byte_mask').value.trim();

            if (raw.toUpperCase() === 'ALL' && length > 0)
                return Array(length).fill(0xff);

            const tokens = raw ? raw.split(/\s+/) : [];
            if (!tokens.length || tokens.length > length ||
                tokens.some(token => !/^[\da-f]{2}$/i.test(token)))
                throw new Error(
                    `Bit mask must be ALL or up to ${length} hexadecimal bytes`);

            const mask = Array.from(
                {length},
                (_, index) => index < tokens.length
                    ? parseInt(tokens[index], 16)
                    : 0);

            if (!mask.some(value => value !== 0))
                throw new Error('Bit mask must select at least one bit');

            return mask;
        }
        function incrementMaskedByte(value, mask, step) {
            let counter = 0, width = 0;

            for (let bit = 0; bit < 8; ++bit) {
                if ((mask & (1 << bit)) === 0)
                    continue;
                if ((value & (1 << bit)) !== 0)
                    counter |= 1 << width;
                ++width;
            }

            counter = (counter + step) % (2 ** width);
            let result = value & (~mask & 0xff), counterBit = 0;

            for (let bit = 0; bit < 8; ++bit) {
                if ((mask & (1 << bit)) === 0)
                    continue;
                if ((counter & (1 << counterBit)) !== 0)
                    result |= 1 << bit;
                ++counterBit;
            }

            return result;
        }
        function advanceData(payload) {
            const data = [...payload.data];

            if (payload.data_width > 0) {
                let carry = BigInt(payload.data_step);

                for (let i = 0; i < payload.data_width; ++i) {
                    const index = payload.data_offset +
                        (payload.data_big_endian ? payload.data_width - 1 - i : i);
                    carry += BigInt(data[index]);
                    data[index] = Number(carry & 0xffn);
                    carry >>= 8n;
                }
            } else if (payload.increment_data_bytes) {
                for (let i = 0; i < data.length; ++i) {
                    const mask = payload.data_byte_mask[i];
                    if (mask !== 0)
                        data[i] = incrementMaskedByte(data[i], mask, payload.data_step);
                }
            }

            control('data').value = data
                .map(value => value.toString(16).toUpperCase().padStart(2, '0'))
                .join(' ');
        }
        async function command(payload) {
            if (busy)
                return false;
            busy = true;
            controls();
            let accepted = false;
            try {
                await api(payload);
                accepted = true;
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
            return accepted;
        }
        async function transmit(single) {
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
                const dataMode = control('data_mode').value;
                const payload = {
                    action : 'start',
                    slot : integer('slot', 0, jobs.length - 1),
                    bus : integer('bus', 0, 1),
                    id,
                    extended,
                    fd,
                    brs : control('format').value === 'brs',
                    dlc,
                    data : tokens.map(t => parseInt(t, 16)),
                    interval_ms : single
                        ? 10
                        : integer('interval_ms', 10, 3600000),
                    count : single
                        ? 1
                        : integer('count', 0, 1000000),
                    id_step : single
                        ? 0
                        : hex('id_step', extended ? 0x1fffffff : 0x7ff),
                    id_end : single
                        ? id
                        : hex('id_end', extended ? 0x1fffffff : 0x7ff),
                    increment_dlc : !single &&
                        control('increment_dlc').value === '1',
                    dlc_end : single
                        ? dlc
                        : integer('dlc_end', 0, fd ? 15 : 8),
                    data_offset : dataMode === 'counter'
                        ? integer('data_offset', 0, 63)
                        : 0,
                    data_width : dataMode === 'counter'
                        ? integer('data_width', 1, 8)
                        : 0,
                    data_step : dataMode === 'off'
                        ? 1
                        : integer(
                            'data_step',
                            1,
                            dataMode === 'bytes' ? 255 : 4294967295),
                    data_big_endian : dataMode === 'counter' &&
                        control('data_big_endian').value === '1',
                    increment_data_bytes : dataMode === 'bytes',
                    data_byte_mask : dataMode === 'bytes'
                        ? byteMask(lengths[dlc])
                        : []
                };

                if (payload.id_step && payload.id_end < id)
                    throw new Error('ID end must be at least the initial ID');
                if (payload.increment_dlc && payload.dlc_end < dlc)
                    throw new Error('DLC end must be at least the initial DLC');
                if (payload.data_width && payload.data_offset + payload.data_width > lengths[dlc])
                    throw new Error('DATA counter must fit the initial payload');
                const confirmation = single
                    ? `Send one frame on ${
                        payload.bus === 0
                            ? 'Primary'
                            : 'Secondary'}? This transmits real CAN traffic.`
                    : `Start sequence ${payload.slot + 1} on ${
                        payload.bus === 0
                            ? 'Primary'
                            : 'Secondary'}? ${payload.count || 'Unlimited'} attempts, interval ${
                        payload.interval_ms} ms. This transmits real CAN traffic.`;

                if (!window.confirm(confirmation))
                    return;
                const accepted = await command(payload);

                if (single && accepted)
                    advanceData(payload);
            } catch (error) {
                status.textContent = error.message;
            }
        }

        form.addEventListener('change', controls);
        form.addEventListener('submit', async event => {
            event.preventDefault();
            await transmit(false);
        });
        sendOnce.addEventListener('click', () => transmit(true));
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
                status.textContent =
                    'Ready. No transmission starts until you press Send once or Start sequence.';
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

    function init_isotp() {
        const element = id => document.getElementById(id);
        const status = element('isotp-status');
        const message = element('isotp-message');
        const sendButton = element('isotp-send');
        const format = element('isotp-format');
        const linkLength = element('isotp-link-length');
        const addressing = element('isotp-addressing');
        const brs = element('isotp-brs');
        const payload = element('isotp-payload');
        const response = element('isotp-response-data');
        const responseSize = element('isotp-response-size');
        const byteCount = element('isotp-byte-count');
        let lastSequence = -1;
        let latestPayload = '';

        const stateNames = [
            'Closed',
            'Ready',
            'Transmitting',
            'Response received',
            'Error'
        ];

        function parseHex(text) {
            const compact = text.replace(/[\s,:-]+/g, '');

            if (!compact.length)
                throw new Error('Enter at least one payload byte.');

            if (!/^[0-9a-f]+$/i.test(compact) || compact.length % 2)
                throw new Error('Payload must contain complete hexadecimal bytes.');

            if (compact.length / 2 > 1024)
                throw new Error('Payload exceeds the 1024-byte web limit.');

            return compact.match(/../g).join(' ').toUpperCase();
        }

        function parseIdentifier(input, extended) {
            const text = input.value.trim();
            const maximum = extended ? 0x1fffffff : 0x7ff;

            if (!/^[0-9a-f]+$/i.test(text) || parseInt(text, 16) > maximum)
                throw new Error(
                    `CAN ID must fit the selected ${extended ? 29 : 11}-bit format.`);

            return parseInt(text, 16);
        }

        function parseByte(input) {
            const text = input.value.trim();

            if (!/^[0-9a-f]{1,2}$/i.test(text))
                throw new Error('Address and padding values must be HEX bytes.');

            return parseInt(text, 16);
        }

        function updateAddressing() {
            const addressed = addressing.value !== '0';

            for (const field of document.querySelectorAll('.isotp-address-field'))
                field.hidden = !addressed;
        }

        async function command(body) {
            const reply = await fetch('/api/isotp', {
                method : 'POST',
                headers : {'Content-Type' : 'application/json'},
                body : JSON.stringify(body)
            });
            const result = await reply.json();

            if (!reply.ok || !result.success)
                throw new Error(result.message || `HTTP ${reply.status}`);

            return result;
        }

        function updateFormat() {
            const fd = format.value === 'fd';
            brs.disabled = !fd;

            for (const option of linkLength.options)
                option.disabled = !fd && option.value !== '8';

            if (!fd)
                linkLength.value = '8';
        }

        function updateByteCount() {
            try {
                const normalized = parseHex(payload.value);
                byteCount.textContent = `${normalized.split(' ').length} bytes`;
                byteCount.classList.remove('error');
            } catch (error) {
                byteCount.textContent = error.message;
                byteCount.classList.add('error');
            }
        }

        async function refresh() {
            try {
                const reply = await fetch('/api/isotp', {cache : 'no-store'});
                const data = await reply.json();

                if (!reply.ok)
                    throw new Error(`HTTP ${reply.status}`);

                status.textContent = stateNames[data.state] || 'Unknown';
                status.classList.toggle('error', data.state === 4);
                sendButton.disabled = !data.open || data.state === 2;

                if (data.sequence !== lastSequence) {
                    lastSequence = data.sequence;

                    if (data.payload_length > 0) {
                        latestPayload = data.payload;
                        response.textContent = data.payload;
                        responseSize.textContent = `${data.payload_length} bytes`;
                        message.textContent = 'A complete ISO-TP response was received.';
                    } else if (data.state === 4) {
                        message.textContent =
                            `ISO-TP error ${data.session_error}; result ${data.result}.`;
                    } else if (data.state === 1) {
                        message.textContent = 'Channel is ready.';
                    } else if (data.state === 2) {
                        message.textContent = 'ISO-TP transmission is in progress.';
                    }
                }
            } catch (error) {
                status.textContent = 'Unavailable';
                status.classList.add('error');
                sendButton.disabled = true;
                message.textContent = error.message;
            }
        }

        element('isotp-open').addEventListener('click', async () => {
            try {
                const extended = element('isotp-extended').checked;
                const fd = format.value === 'fd';

                await command({
                    action : 'configure',
                    bus : Number(element('isotp-bus').value),
                    tx_id : parseIdentifier(element('isotp-tx-id'), extended),
                    rx_id : parseIdentifier(element('isotp-rx-id'), extended),
                    extended,
                    fd,
                    brs : fd && brs.checked,
                    functional : element('isotp-functional').checked,
                    link_data_length : Number(linkLength.value),
                    addressing_mode : Number(addressing.value),
                    tx_address : parseByte(element('isotp-tx-address')),
                    rx_address : parseByte(element('isotp-rx-address')),
                    padding_byte : parseByte(element('isotp-padding')),
                    block_size : Number(element('isotp-block-size').value),
                    st_min : Number(element('isotp-st-min').value),
                    timeout_ms : Number(element('isotp-timeout').value)
                });
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('isotp-close').addEventListener('click', async () => {
            try {
                await command({action : 'close'});
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        sendButton.addEventListener('click', async () => {
            try {
                const data = parseHex(payload.value);
                await command({action : 'send', data});
                message.textContent = `Submitted ${data.split(' ').length} bytes.`;
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('isotp-copy').addEventListener('click', async () => {
            if (!latestPayload)
                return;

            try {
                await navigator.clipboard.writeText(latestPayload);
                message.textContent = 'Response copied to the clipboard.';
            } catch (_) {
                message.textContent = 'Clipboard access is unavailable.';
            }
        });

        element('isotp-clear').addEventListener('click', () => {
            latestPayload = '';
            response.textContent = 'No response received.';
            responseSize.textContent = '0 bytes';
        });

        format.addEventListener('change', updateFormat);
        addressing.addEventListener('change', updateAddressing);
        payload.addEventListener('input', updateByteCount);
        updateFormat();
        updateAddressing();
        updateByteCount();
        refresh();
        setInterval(refresh, 300);
    }

    function init_uds() {
        const element = id => document.getElementById(id);
        const status = element('uds-status');
        const message = element('uds-message');
        const sendButton = element('uds-send');
        const format = element('uds-format');
        const service = element('uds-service');
        const response = element('uds-response-data');
        const summary = element('uds-response-summary');
        const brs = element('uds-brs');
        let lastSequence = -1;

        const stateNames = [
            'Closed',
            'Ready',
            'Transmitting',
            'Waiting for response',
            'Response pending',
            'Complete',
            'Negative response',
            'Error'
        ];

        function parseIdentifier(input, extended) {
            const text = input.value.trim();
            const maximum = extended ? 0x1fffffff : 0x7ff;

            if (!/^[0-9a-f]+$/i.test(text) || parseInt(text, 16) > maximum)
                throw new Error(
                    `CAN ID must fit the selected ${extended ? 29 : 11}-bit format.`);

            return parseInt(text, 16);
        }

        function parseHexNumber(input, maximum, name) {
            const text = input.value.trim();

            if (!/^[0-9a-f]+$/i.test(text) || parseInt(text, 16) > maximum)
                throw new Error(`${name} is not a valid hexadecimal value.`);

            return parseInt(text, 16);
        }

        function parseHexIntegerText(input, byteLength, name, allowZero) {
            const text = input.value.trim();

            if (!/^[0-9a-f]+$/i.test(text) ||
                text.length > 16 ||
                text.length > byteLength * 2) {

                throw new Error(
                    `${name} does not fit the selected ${byteLength}-byte length.`
                );
            }

            if (!allowZero && BigInt(`0x${text}`) === 0n)
                throw new Error(`${name} must be greater than zero.`);

            return text.toUpperCase();
        }

        function normalizeHex(text, allowEmpty) {
            const compact = text.replace(/[\s,:-]+/g, '');

            if (!compact.length)
                return allowEmpty ? '' : null;

            if (!/^[0-9a-f]+$/i.test(compact) || compact.length % 2)
                throw new Error('Parameters must contain complete hexadecimal bytes.');

            if (compact.length / 2 > 1023)
                throw new Error('Parameters exceed the 1023-byte web limit.');

            return compact.match(/../g).join(' ').toUpperCase();
        }

        async function command(body) {
            const reply = await fetch('/api/uds', {
                method : 'POST',
                headers : {'Content-Type' : 'application/json'},
                body : JSON.stringify(body)
            });
            const result = await reply.json();

            if (!reply.ok || !result.success)
                throw new Error(result.message || `HTTP ${reply.status}`);

            return result;
        }

        function updateFormat() {
            const fd = format.value === 'fd';
            brs.disabled = !fd;
        }

        function updateService() {
            const selected = service.value;
            element('uds-did-field').hidden =
                selected !== 'read_did' &&
                selected !== 'write_did';
            element('uds-write-data-field').hidden =
                selected !== 'write_did';
            element('uds-dtc-subfunction-field').hidden =
                selected !== 'read_dtc';
            element('uds-dtc-mask-field').hidden =
                selected !== 'read_dtc' ||
                element('uds-dtc-subfunction').value === '0A';
            element('uds-dtc-group-field').hidden =
                selected !== 'clear_dtc';
            element('uds-routine-type-field').hidden =
                selected !== 'routine';
            element('uds-routine-id-field').hidden =
                selected !== 'routine';
            element('uds-routine-data-field').hidden =
                selected !== 'routine';
            const security =
                selected === 'security_seed' ||
                selected === 'security_key';
            element('uds-security-level-field').hidden = !security;
            element('uds-security-data-field').hidden = !security;
            element('uds-security-data-label').textContent =
                selected === 'security_key'
                    ? 'Calculated key (HEX bytes)'
                    : 'Seed request data record (HEX bytes, optional)';
            const download = selected === 'request_download';
            element('uds-download-format-field').hidden = !download;
            element('uds-download-address-field').hidden = !download;
            element('uds-download-address-length-field').hidden = !download;
            element('uds-download-size-field').hidden = !download;
            element('uds-download-size-length-field').hidden = !download;
            element('uds-transfer-counter-field').hidden =
                selected !== 'transfer_data';
            const transfer =
                selected === 'transfer_data' ||
                selected === 'transfer_exit';
            element('uds-transfer-data-field').hidden = !transfer;
            element('uds-transfer-data-label').textContent =
                selected === 'transfer_exit'
                    ? 'Transfer exit parameter record (HEX bytes, optional)'
                    : 'Transfer data (HEX bytes)';
            element('uds-subfunction-field').hidden =
                selected !== 'session' && selected !== 'reset';
            element('uds-raw-sid-field').hidden = selected !== 'raw';
            element('uds-raw-data-field').hidden = selected !== 'raw';
            element('uds-suppress').disabled =
                selected === 'read_did' ||
                selected === 'write_did' ||
                selected === 'read_dtc' ||
                security ||
                download ||
                transfer ||
                selected === 'raw';
        }

        function decodeDtcResponse(payloadText) {
            const bytes = payloadText
                .trim()
                .split(/\s+/)
                .filter(Boolean)
                .map(value => parseInt(value, 16));

            if (bytes.length < 2)
                return null;

            const subfunction = bytes[0];
            const availability = bytes[1];
            const hex = (value, width) =>
                value.toString(16).toUpperCase().padStart(width, '0');

            if (subfunction === 0x01 && bytes.length === 5) {
                const count = (bytes[3] << 8) | bytes[4];

                return `DTC count: ${count}\n` +
                    `Status availability: 0x${hex(availability, 2)}\n` +
                    `DTC format: 0x${hex(bytes[2], 2)}\n\n` +
                    `Raw: ${payloadText}`;
            }

            if ((subfunction === 0x02 || subfunction === 0x0A) &&
                ((bytes.length - 2) % 4 === 0)) {
                const lines = [];

                for (let offset = 2; offset < bytes.length; offset += 4) {
                    const code =
                        (bytes[offset] << 16) |
                        (bytes[offset + 1] << 8) |
                        bytes[offset + 2];

                    lines.push(
                        `0x${hex(code, 6)}  status 0x${hex(bytes[offset + 3], 2)}` +
                        `  ${decodeDtcStatus(bytes[offset + 3])}`
                    );
                }

                return `DTC records: ${lines.length}\n` +
                    `Status availability: 0x${hex(availability, 2)}\n\n` +
                    `${lines.length ? lines.join('\n') : 'No matching DTCs.'}\n\n` +
                    `Raw: ${payloadText}`;
            }

            return null;
        }

        function decodeDtcStatus(status) {
            const names = [
                'test failed',
                'failed this cycle',
                'pending',
                'confirmed',
                'not completed since clear',
                'failed since clear',
                'not completed this cycle',
                'warning indicator requested'
            ];
            const active = names.filter(
                (_, bit) => (status & (1 << bit)) !== 0
            );

            return active.length ? active.join(', ') : 'no status flags';
        }

        function decodeRoutineResponse(payloadText) {
            const bytes = payloadText
                .trim()
                .split(/\s+/)
                .filter(Boolean)
                .map(value => parseInt(value, 16));

            if (bytes.length < 3)
                return null;

            const operationNames = {
                0x01 : 'Start routine',
                0x02 : 'Stop routine',
                0x03 : 'Routine results'
            };
            const operation = bytes[0] & 0x7f;
            const routineIdentifier =
                (bytes[1] << 8) |
                bytes[2];
            const lines = [
                `${operationNames[operation] || 'Routine operation'} · ` +
                `ID 0x${routineIdentifier.toString(16).padStart(4, '0').toUpperCase()}`
            ];

            if (bytes.length > 3) {
                lines.push(
                    `Status record: ${bytes.slice(3)
                        .map(value => value.toString(16).padStart(2, '0').toUpperCase())
                        .join(' ')}`
                );
            }

            return lines.join('\n');
        }

        function decodeSecurityAccessResponse(payloadText) {
            const bytes = payloadText
                .trim()
                .split(/\s+/)
                .filter(Boolean)
                .map(value => parseInt(value, 16));

            if (!bytes.length)
                return null;

            const subfunction = bytes[0];
            const level =
                (subfunction & 1) !== 0
                    ? subfunction
                    : subfunction - 1;
            const lines = [
                `Security level 0x${level.toString(16).padStart(2, '0').toUpperCase()}`
            ];

            if ((subfunction & 1) !== 0) {
                lines.push(
                    bytes.length > 1
                        ? `Seed: ${bytes.slice(1)
                            .map(value => value.toString(16).padStart(2, '0').toUpperCase())
                            .join(' ')}`
                        : 'Seed: empty (security level already unlocked)'
                );
            } else {
                lines.push('Key accepted by ECU.');
            }

            return lines.join('\n');
        }

        function decodeDownloadResponse(payloadText) {
            const bytes = payloadText
                .trim()
                .split(/\s+/)
                .filter(Boolean)
                .map(value => parseInt(value, 16));

            if (bytes.length < 2)
                return null;

            const length = bytes[0] >> 4;

            if (!length || length > 8 || bytes.length !== 1 + length)
                return null;

            let maximumBlockLength = 0n;

            for (let index = 0; index < length; ++index)
                maximumBlockLength =
                    (maximumBlockLength << 8n) |
                    BigInt(bytes[1 + index]);

            return `Download accepted\nMaximum block length: ${maximumBlockLength}`;
        }

        function decodeTransferDataResponse(payloadText) {
            const bytes = payloadText
                .trim()
                .split(/\s+/)
                .filter(Boolean)
                .map(value => parseInt(value, 16));

            if (!bytes.length)
                return null;

            const counter =
                bytes[0].toString(16).padStart(2, '0').toUpperCase();
            const record = bytes.slice(1)
                .map(value => value.toString(16).padStart(2, '0').toUpperCase())
                .join(' ');

            return `Block 0x${counter} accepted` +
                (record ? `\nResponse record: ${record}` : '');
        }

        async function refresh() {
            try {
                const reply = await fetch('/api/uds', {cache : 'no-store'});
                const data = await reply.json();

                if (!reply.ok)
                    throw new Error(`HTTP ${reply.status}`);

                status.textContent = stateNames[data.state] || 'Unknown';
                status.classList.toggle(
                    'error',
                    data.state === 6 || data.state === 7
                );
                sendButton.disabled =
                    !data.open ||
                    data.state === 2 ||
                    data.state === 3 ||
                    data.state === 4;
                if (data.sequence === lastSequence)
                    return;

                lastSequence = data.sequence;
                summary.classList.toggle('error', !data.positive && data.nrc !== 0);

                if (data.nrc !== 0) {
                    summary.textContent =
                        `NRC 0x${data.nrc.toString(16).padStart(2, '0').toUpperCase()}`;
                    response.textContent = data.payload || 'No response payload.';
                    message.textContent =
                        `${data.nrc_name} · request SID 0x${data.request_sid
                            .toString(16).padStart(2, '0').toUpperCase()}.`;
                } else if (data.positive) {
                    summary.textContent =
                        `Positive · SID 0x${data.response_sid
                            .toString(16).padStart(2, '0').toUpperCase()}`;
                    const decoded =
                        data.response_sid === 0x59
                            ? decodeDtcResponse(data.payload || '')
                            : data.response_sid === 0x71
                                ? decodeRoutineResponse(data.payload || '')
                                : data.response_sid === 0x67
                                    ? decodeSecurityAccessResponse(data.payload || '')
                                    : data.response_sid === 0x74
                                        ? decodeDownloadResponse(data.payload || '')
                                        : data.response_sid === 0x76
                                            ? decodeTransferDataResponse(data.payload || '')
                                            : null;

                    response.textContent =
                        decoded ||
                        data.payload ||
                        'Positive response without parameters.';
                    message.textContent = 'A complete UDS response was received.';
                } else if (data.state === 4) {
                    summary.textContent = 'Response pending';
                    message.textContent = 'ECU requested additional response time.';
                } else if (data.state === 7) {
                    summary.textContent = 'Request failed';
                    message.textContent =
                        `UDS result ${data.result}; transport error ${data.transport_error}.`;
                } else if (data.open) {
                    summary.textContent = 'No response';
                    message.textContent = stateNames[data.state] || 'UDS channel is ready.';
                }
            } catch (error) {
                status.textContent = 'Unavailable';
                status.classList.add('error');
                sendButton.disabled = true;
                message.textContent = error.message;
            }
        }

        element('uds-open').addEventListener('click', async () => {
            try {
                const extended = element('uds-extended').checked;
                const fd = format.value === 'fd';

                await command({
                    action : 'configure',
                    bus : Number(element('uds-bus').value),
                    tx_id : parseIdentifier(element('uds-tx-id'), extended),
                    rx_id : parseIdentifier(element('uds-rx-id'), extended),
                    extended,
                    fd,
                    brs : fd && brs.checked,
                    link_data_length : fd ? 64 : 8,
                    block_size : Number(element('uds-block-size').value),
                    st_min : Number(element('uds-st-min').value),
                    p2_ms : Number(element('uds-p2').value),
                    p2_star_ms : Number(element('uds-p2-star').value)
                });
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('uds-close').addEventListener('click', async () => {
            try {
                await command({action : 'close'});
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        sendButton.addEventListener('click', async () => {
            try {
                const kind = service.value;
                const request = {action : 'request', kind};

                if (kind === 'read_did') {
                    request.did =
                        parseHexNumber(element('uds-did'), 0xffff, 'Data identifier');
                } else if (kind === 'write_did') {
                    const confirmed = window.confirm(
                        'Write data to the selected ECU data identifier?'
                    );

                    if (!confirmed)
                        return;

                    request.did =
                        parseHexNumber(
                            element('uds-did'),
                            0xffff,
                            'Data identifier'
                        );
                    request.data =
                        normalizeHex(
                            element('uds-write-data').value,
                            false
                        );

                    if (request.data === null)
                        throw new Error(
                            'Write Data payload must contain at least one byte.'
                        );

                    if (request.data.split(' ').length > 256)
                        throw new Error(
                            'Write Data payload exceeds the 256-byte limit.'
                        );
                } else if (kind === 'read_dtc') {
                    request.value =
                        parseHexNumber(
                            element('uds-dtc-subfunction'),
                            0xff,
                            'DTC report'
                        );
                    request.status_mask =
                        parseHexNumber(
                            element('uds-dtc-mask'),
                            0xff,
                            'DTC status mask'
                        );
                } else if (kind === 'clear_dtc') {
                    const confirmed = window.confirm(
                        'Clear diagnostic information in the selected ECU?'
                    );

                    if (!confirmed)
                        return;

                    request.group =
                        parseHexNumber(
                            element('uds-dtc-group'),
                            0xffffff,
                            'Group of DTC'
                        );
                } else if (kind === 'routine') {
                    request.value =
                        parseHexNumber(
                            element('uds-routine-type'),
                            0x7f,
                            'Routine operation'
                        );
                    request.routine_id =
                        parseHexNumber(
                            element('uds-routine-id'),
                            0xffff,
                            'Routine identifier'
                        );
                    request.data =
                        normalizeHex(
                            element('uds-routine-data').value,
                            true
                        );
                    request.suppress = element('uds-suppress').checked;

                    if (request.data &&
                        request.data.split(' ').length > 256) {

                        throw new Error(
                            'Routine option record exceeds the 256-byte limit.'
                        );
                    }

                    if (request.value !== 0x03) {
                        const confirmed = window.confirm(
                            request.value === 0x01
                                ? 'Start the selected ECU routine?'
                                : 'Stop the selected ECU routine?'
                        );

                        if (!confirmed)
                            return;
                    }
                } else if (kind === 'security_seed' ||
                           kind === 'security_key') {

                    request.value =
                        parseHexNumber(
                            element('uds-security-level'),
                            0x7d,
                            'Security level'
                        );

                    if ((request.value & 1) === 0 ||
                        request.value === 0) {

                        throw new Error(
                            'Security level must be an odd value from 01 to 7D.'
                        );
                    }

                    request.data =
                        normalizeHex(
                            element('uds-security-data').value,
                            kind === 'security_seed'
                        );

                    if (request.data === null)
                        throw new Error(
                            'Send Key requires at least one key byte.'
                        );

                    if (request.data &&
                        request.data.split(' ').length > 256) {

                        throw new Error(
                            'Security Access data exceeds the 256-byte limit.'
                        );
                    }

                    if (kind === 'security_key' &&
                        !window.confirm(
                            'Send this calculated security key to the selected ECU?'
                        )) {

                        return;
                    }
                } else if (kind === 'request_download') {
                    const addressLength =
                        Number(element('uds-download-address-length').value);
                    const sizeLength =
                        Number(element('uds-download-size-length').value);

                    request.data_format =
                        parseHexNumber(
                            element('uds-download-format'),
                            0xff,
                            'Data format identifier'
                        );
                    request.address_length = addressLength;
                    request.size_length = sizeLength;
                    request.address =
                        parseHexIntegerText(
                            element('uds-download-address'),
                            addressLength,
                            'Memory address',
                            true
                        );
                    request.size =
                        parseHexIntegerText(
                            element('uds-download-size'),
                            sizeLength,
                            'Memory size',
                            false
                        );

                    if (!window.confirm(
                            'Request permission to program the selected ECU memory region?'
                        )) {

                        return;
                    }
                } else if (kind === 'transfer_data') {
                    request.counter =
                        parseHexNumber(
                            element('uds-transfer-counter'),
                            0xff,
                            'Block sequence counter'
                        );
                    request.data =
                        normalizeHex(
                            element('uds-transfer-data').value,
                            false
                        );

                    if (request.data === null)
                        throw new Error(
                            'Transfer Data requires at least one data byte.'
                        );

                    if (request.data.split(' ').length > 512)
                        throw new Error(
                            'Transfer Data exceeds the 512-byte web limit.'
                        );
                } else if (kind === 'transfer_exit') {
                    request.data =
                        normalizeHex(
                            element('uds-transfer-data').value,
                            true
                        );

                    if (request.data &&
                        request.data.split(' ').length > 256) {

                        throw new Error(
                            'Transfer exit record exceeds the 256-byte limit.'
                        );
                    }

                    if (!window.confirm(
                            'Finish the current ECU download transfer?'
                        )) {

                        return;
                    }
                } else if (kind === 'session' || kind === 'reset') {
                    request.value =
                        parseHexNumber(element('uds-subfunction'), 0x7f, 'Sub-function');
                    request.suppress = element('uds-suppress').checked;
                } else if (kind === 'tester_present') {
                    request.suppress = element('uds-suppress').checked;
                } else {
                    request.sid =
                        parseHexNumber(element('uds-raw-sid'), 0xbf, 'Service ID');
                    request.data = normalizeHex(element('uds-raw-data').value, true);
                }

                await command(request);
                message.textContent = 'UDS request submitted.';
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        format.addEventListener('change', updateFormat);
        service.addEventListener('change', updateService);
        element('uds-dtc-subfunction').addEventListener(
            'change',
            updateService
        );
        updateFormat();
        updateService();
        refresh();
        setInterval(refresh, 300);
    }

    function init_diagnostics_transport() {
        init_isotp();
        init_uds();
    }

    function init_uds_programming() {
        const element = id => document.getElementById(id);
        const status = element('program-status');
        const message = element('program-message');
        const fileSelector = element('program-file');
        const format = element('program-format');
        const brs = element('program-brs');
        const applyButton = element('program-apply');
        const closeButton = element('program-close');
        const startButton = element('program-start');
        const cancelButton = element('program-cancel');
        const transportFields = element('program-transport-fields');
        const imageFields = element('program-image-fields');
        let files = [];
        let lastTransferred = 0;
        let lastSampleTime = 0;
        let measuredSpeed = 0;
        let channelOpen = false;
        let downloadReserved = false;

        const clientStateNames = [
            'Closed',
            'Ready',
            'Transmitting',
            'Waiting for response',
            'Response pending',
            'Complete',
            'Negative response',
            'Error'
        ];
        const downloadStateNames = [
            'Closed',
            'Ready',
            'Requesting download',
            'Transferring firmware',
            'Finalizing transfer',
            'Programming completed',
            'Programming cancelled',
            'Programming failed'
        ];
        const nrcNames = {
            0x21 : 'Busy repeat request',
            0x22 : 'Conditions not correct',
            0x24 : 'Request sequence error',
            0x31 : 'Request out of range',
            0x33 : 'Security access denied',
            0x35 : 'Invalid key',
            0x36 : 'Exceeded attempts',
            0x37 : 'Required delay not expired',
            0x70 : 'Upload/download not accepted',
            0x71 : 'Transfer data suspended',
            0x72 : 'General programming failure',
            0x73 : 'Wrong block sequence counter',
            0x78 : 'Response pending'
        };

        function formatBytes(value) {
            const bytes = Number(value) || 0;

            if (bytes >= 1024 * 1024)
                return `${(bytes / (1024 * 1024)).toFixed(2)} MiB`;
            if (bytes >= 1024)
                return `${(bytes / 1024).toFixed(1)} KiB`;

            return `${bytes} B`;
        }

        function formatDuration(milliseconds) {
            if (!Number.isFinite(milliseconds) || milliseconds < 0)
                return '—';

            const seconds = Math.ceil(milliseconds / 1000);
            const minutes = Math.floor(seconds / 60);
            const remainder = seconds % 60;

            return minutes
                ? `${minutes}m ${remainder}s`
                : `${remainder}s`;
        }

        function parseIdentifier(input, extended) {
            const text = input.value.trim();
            const maximum = extended ? 0x1fffffff : 0x7ff;

            if (!/^[0-9a-f]+$/i.test(text) || parseInt(text, 16) > maximum)
                throw new Error(
                    `CAN ID must fit the selected ${extended ? 29 : 11}-bit format.`
                );

            return parseInt(text, 16);
        }

        function parseHex(input, maximum, name) {
            const text = input.value.trim();

            if (!/^[0-9a-f]+$/i.test(text) || parseInt(text, 16) > maximum)
                throw new Error(`${name} is not a valid hexadecimal value.`);

            return parseInt(text, 16);
        }

        function parseAddress(input, byteLength) {
            const text = input.value.trim();

            if (!/^[0-9a-f]+$/i.test(text) ||
                text.length > 16 ||
                text.length > byteLength * 2) {

                throw new Error(
                    `Memory address does not fit the selected ${byteLength}-byte length.`
                );
            }

            return text.toUpperCase();
        }

        async function command(body) {
            const response = await fetch('/api/uds', {
                method : 'POST',
                headers : {'Content-Type' : 'application/json'},
                body : JSON.stringify(body)
            });
            const result = await response.json();

            if (!response.ok || !result.success)
                throw new Error(result.message || `HTTP ${response.status}`);

            return result;
        }

        function updateFileInfo() {
            const selected = files.find(
                entry => `/firmwares/${entry.name}` === fileSelector.value
            );

            element('program-file-info').textContent = selected
                ? `${selected.name} · ${formatBytes(selected.size)}`
                : 'No image selected.';
            startButton.disabled =
                !selected || !channelOpen || downloadReserved;
        }

        async function loadFiles() {
            const previous = fileSelector.value;
            fileSelector.disabled = true;

            try {
                const query = new URLSearchParams({
                    volume : 'sd',
                    path : '/firmwares',
                    offset : '0',
                    limit : '64'
                });
                const response = await fetch(
                    `/api/files?${query.toString()}`,
                    {cache : 'no-store'}
                );
                const result = await response.json();

                if (!response.ok)
                    throw new Error(result.message || `HTTP ${response.status}`);

                files = (result.entries || [])
                    .filter(entry =>
                        entry.type !== 'directory' &&
                        /\.(bin|hex|srec|mot)$/i.test(entry.name)
                    )
                    .sort((left, right) =>
                        left.name.localeCompare(right.name)
                    );

                fileSelector.replaceChildren();

                if (!files.length) {
                    fileSelector.add(new Option('No firmware images found', ''));
                } else {
                    fileSelector.add(new Option('Select firmware image…', ''));

                    for (const entry of files) {
                        fileSelector.add(
                            new Option(
                                `${entry.name} · ${formatBytes(entry.size)}`,
                                `/firmwares/${entry.name}`
                            )
                        );
                    }

                    if (files.some(entry =>
                            `/firmwares/${entry.name}` === previous
                        )) {

                        fileSelector.value = previous;
                    }
                }

                message.textContent = files.length
                    ? `${files.length} firmware image${files.length === 1 ? '' : 's'} available.`
                    : 'No supported files found in /firmwares.';
            } catch (error) {
                files = [];
                fileSelector.replaceChildren(
                    new Option('Firmware directory unavailable', '')
                );
                message.textContent = error.message;
            } finally {
                fileSelector.disabled = false;
                updateFileInfo();
            }
        }

        function updateProgress(data) {
            const total = Number(data.download_total) || 0;
            const transferred = Number(data.download_transferred) || 0;
            const percent = total
                ? Math.min(100, (transferred / total) * 100)
                : 0;
            const now = performance.now();

            if (data.download_active &&
                transferred >= lastTransferred &&
                lastSampleTime > 0 &&
                now > lastSampleTime &&
                transferred !== lastTransferred) {

                const sampleSpeed =
                    (transferred - lastTransferred) * 1000 /
                    (now - lastSampleTime);
                measuredSpeed = measuredSpeed
                    ? measuredSpeed * 0.7 + sampleSpeed * 0.3
                    : sampleSpeed;
            }

            if (!data.download_active || transferred < lastTransferred)
                measuredSpeed = 0;

            lastTransferred = transferred;
            lastSampleTime = now;

            element('program-progress').max = total || 1;
            element('program-progress').value = transferred;
            element('program-percent').textContent = `${percent.toFixed(1)}%`;
            element('program-stage').textContent =
                downloadStateNames[data.download_state] || 'Unknown state';
            element('program-transferred').textContent =
                `${formatBytes(transferred)} / ${formatBytes(total)}`;
            element('program-speed').textContent = measuredSpeed
                ? `${formatBytes(measuredSpeed)}/s`
                : '—';
            element('program-eta').textContent =
                measuredSpeed && transferred < total
                    ? formatDuration((total - transferred) * 1000 / measuredSpeed)
                    : '—';
            element('program-elapsed').textContent =
                formatDuration(Number(data.download_elapsed_ms));
            element('program-block').textContent = data.download_block_size
                ? `${data.download_block_size} bytes`
                : '—';
            element('program-sequence').textContent =
                data.download_active
                    ? `0x${Number(data.download_sequence_counter)
                        .toString(16).padStart(2, '0').toUpperCase()}`
                    : '—';
            element('program-blocks').textContent = data.download_blocks || 0;
            element('program-retries').textContent =
                data.download_block_retry
                    ? `${data.download_retries || 0} · current ${data.download_block_retry}`
                    : data.download_retries || 0;
            element('program-nrc').textContent = data.download_nrc
                ? `0x${Number(data.download_nrc)
                    .toString(16).padStart(2, '0').toUpperCase()} · ` +
                  (nrcNames[data.download_nrc] || 'Unknown NRC')
                : 'None';
            element('program-journal').textContent =
                data.download_journal || '—';
        }

        async function refresh() {
            try {
                const response = await fetch('/api/uds', {cache : 'no-store'});
                const data = await response.json();

                if (!response.ok)
                    throw new Error(`HTTP ${response.status}`);

                const active = data.download_active &&
                    data.download_state >= 2 &&
                    data.download_state <= 4;
                const terminal = data.download_active &&
                    data.download_state >= 5;
                channelOpen = data.open;
                downloadReserved = data.download_active;

                status.textContent = active
                    ? 'Programming'
                    : terminal
                        ? downloadStateNames[data.download_state]
                        : clientStateNames[data.state] || 'Unknown';
                status.classList.toggle('active', active);
                status.classList.toggle(
                    'error',
                    data.download_state === 7 || data.state === 7
                );
                element('program-channel-state').textContent = data.open
                    ? clientStateNames[data.state] || 'Channel open'
                    : 'Channel closed';

                transportFields.disabled = data.download_active;
                imageFields.disabled = data.download_active;
                applyButton.disabled = data.download_active;
                startButton.disabled =
                    data.download_active || !data.open || !fileSelector.value;
                cancelButton.disabled = !active;
                closeButton.disabled = active || !data.open;
                element('program-refresh-files').disabled = data.download_active;
                updateProgress(data);

                if (data.download_state === 5)
                    message.textContent = 'Programming completed successfully.';
                else if (data.download_state === 6)
                    message.textContent = 'Programming was cancelled.';
                else if (data.download_state === 7)
                    message.textContent =
                        `Programming failed with result ${data.download_result}.`;
            } catch (error) {
                status.textContent = 'Unavailable';
                status.classList.add('error');
                startButton.disabled = true;
                cancelButton.disabled = true;
                message.textContent = error.message;
            }
        }

        format.addEventListener('change', () => {
            brs.disabled = format.value !== 'fd';
        });
        fileSelector.addEventListener('change', updateFileInfo);
        element('program-refresh-files').addEventListener('click', loadFiles);

        applyButton.addEventListener('click', async () => {
            try {
                const extended = element('program-extended').checked;
                const fd = format.value === 'fd';

                await command({
                    action : 'configure',
                    bus : Number(element('program-bus').value),
                    tx_id : parseIdentifier(element('program-tx-id'), extended),
                    rx_id : parseIdentifier(element('program-rx-id'), extended),
                    extended,
                    fd,
                    brs : fd && brs.checked,
                    link_data_length : fd ? 64 : 8,
                    block_size : Number(element('program-block-size').value),
                    st_min : Number(element('program-st-min').value),
                    p2_ms : Number(element('program-p2').value),
                    p2_star_ms : Number(element('program-p2-star').value)
                });
                message.textContent = 'UDS channel configured.';
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        closeButton.addEventListener('click', async () => {
            try {
                await command({action : 'close'});
                message.textContent = 'UDS channel closed.';
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        startButton.addEventListener('click', async () => {
            try {
                const path = fileSelector.value;
                const addressLength =
                    Number(element('program-address-length').value);
                const sizeLength =
                    Number(element('program-size-length').value);

                if (!path)
                    throw new Error('Select a firmware image first.');

                if (!window.confirm(
                        `Program ${path} into the selected ECU? ` +
                        'Do not disconnect power or CAN during this operation.'
                    )) {

                    return;
                }

                await command({
                    action : 'download_start',
                    path,
                    data_format : parseHex(
                        element('program-data-format'),
                        0xff,
                        'Data format identifier'
                    ),
                    address : parseAddress(
                        element('program-address'),
                        addressLength
                    ),
                    address_length : addressLength,
                    size_length : sizeLength
                });
                message.textContent = 'Programming started.';
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        cancelButton.addEventListener('click', async () => {
            if (!window.confirm('Cancel the active programming operation?'))
                return;

            try {
                await command({action : 'download_cancel'});
                message.textContent = 'Cancellation requested.';
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        loadFiles();
        refresh();
        setInterval(refresh, 300);
    }

    function init_xcp() {
        const element = id => document.getElementById(id);
        const status = element('xcp-status');
        const message = element('xcp-message');
        const format = element('xcp-format');
        const transmitLength = element('xcp-tx-length');
        const brs = element('xcp-brs');
        const commandInput = element('xcp-command');
        const executeButton = element('xcp-execute');
        const responseData = element('xcp-response-data');
        const responseSummary = element('xcp-response-summary');
        const capabilities = element('xcp-capabilities');
        const decoded = element('xcp-decoded');
        const byteCount = element('xcp-byte-count');
        let latestResponse = '';
        let lastSequence = -1;

        const stateNames = [
            'Closed',
            'Configured',
            'Connecting',
            'Connected',
            'Command pending',
            'Disconnecting',
            'Error'
        ];

        function normalizeHex(text) {
            const compact = text.replace(/[\s,:-]+/g, '');

            if (!compact.length ||
                !/^[0-9a-f]+$/i.test(compact) ||
                (compact.length % 2) !== 0) {

                throw new Error('Enter complete hexadecimal bytes.');
            }

            if ((compact.length / 2) > 64)
                throw new Error('An XCP CTO cannot exceed 64 bytes.');

            const bytes = compact.match(/../g);

            if (parseInt(bytes[0], 16) < 0xc0)
                throw new Error('The first CTO byte must be a command PID from C0 to FF.');

            return bytes.join(' ').toUpperCase();
        }

        function normalizeDataHex(text) {
            const compact = text.replace(/[\s,:-]+/g, '');

            if (!compact.length ||
                !/^[0-9a-f]+$/i.test(compact) ||
                (compact.length % 2) !== 0) {

                throw new Error('Enter complete hexadecimal data bytes.');
            }

            if ((compact.length / 2) > 62)
                throw new Error('A single XCP DOWNLOAD cannot exceed 62 data bytes.');

            return compact.match(/../g).join(' ').toUpperCase();
        }

        function parseHexNumber(input, maximum, name) {
            const text = input.value.trim();

            if (!/^[0-9a-f]+$/i.test(text))
                throw new Error(`${name} must be hexadecimal.`);

            const value = parseInt(text, 16);

            if (value > maximum)
                throw new Error(`${name} is outside the selected range.`);

            return value;
        }

        async function request(body) {
            const reply = await fetch('/api/xcp', {
                method : 'POST',
                headers : {'Content-Type' : 'application/json'},
                body : JSON.stringify(body)
            });
            const result = await reply.json();

            if (!reply.ok || !result.success)
                throw new Error(result.message || `HTTP ${reply.status}`);

            return result;
        }

        function updateFormat() {
            const fd = format.value === 'fd';
            brs.disabled = !fd;

            for (const option of transmitLength.options)
                option.disabled = !fd && option.value !== '8';

            if (!fd)
                transmitLength.value = '8';
        }

        function updateByteCount() {
            try {
                const normalized = normalizeHex(commandInput.value);
                const count = normalized.split(' ').length;
                byteCount.textContent = `${count} ${count === 1 ? 'byte' : 'bytes'}`;
                byteCount.classList.remove('error');
            } catch (error) {
                byteCount.textContent = error.message;
                byteCount.classList.add('error');
            }
        }

        async function refresh() {
            try {
                const reply = await fetch('/api/xcp', {cache : 'no-store'});
                const data = await reply.json();

                if (!reply.ok)
                    throw new Error(`HTTP ${reply.status}`);

                status.textContent = stateNames[data.state] || 'Unknown';
                status.classList.toggle('error', data.state === 6);
                status.classList.toggle('open', data.connected);
                element('xcp-connect').disabled = !data.open || data.connected;
                element('xcp-disconnect').disabled = !data.connected;
                element('xcp-close').disabled = !data.open;
                executeButton.disabled = !data.connected || data.state === 4;
                for (const button of document.querySelectorAll(
                        '#xcp-get-status, #xcp-get-communication, #xcp-get-id, '
                        + '#xcp-set-mta, #xcp-upload')) {

                    button.disabled = !data.connected || data.state === 4;
                }
                element('xcp-write-memory').disabled =
                    !data.connected ||
                    data.state === 4 ||
                    !element('xcp-write-confirm').checked;
                capabilities.textContent =
                    `MAX_CTO ${data.maximum_cto || '—'} · MAX_DTO ${data.maximum_dto || '—'}`;

                if (data.operation === 'get_status') {
                    decoded.textContent =
                        `Session status 0x${data.session_status.toString(16).padStart(2, '0').toUpperCase()} · `
                        + `protection 0x${data.resource_protection.toString(16).padStart(2, '0').toUpperCase()} · `
                        + `configuration ${data.session_configuration_id}`;
                } else if (data.operation === 'get_comm_mode_info') {
                    decoded.textContent =
                        `Optional mode 0x${data.communication_mode_optional.toString(16).padStart(2, '0').toUpperCase()} · `
                        + `MAX_BS ${data.maximum_block_size} · MIN_ST ${data.minimum_separation_time} · `
                        + `driver v${(data.driver_version >> 4) & 15}.${data.driver_version & 15}`;
                } else if (data.operation === 'get_id') {
                    decoded.textContent =
                        `Identification length ${data.identification_length} bytes · `
                        + `transfer mode ${data.identification_transfer_mode}`
                        + (data.identification_text
                            ? ` · ${data.identification_text}`
                            : '');
                } else if (data.operation === 'set_mta') {
                    decoded.textContent = 'Memory Transfer Address accepted by the slave.';
                } else if (data.operation === 'upload') {
                    decoded.textContent = `Uploaded ${Math.max(0, data.response_length - 1)} response bytes.`;
                } else if (data.operation === 'write_memory') {
                    decoded.textContent = 'Memory write was accepted by the XCP slave.';
                }

                if (data.sequence !== lastSequence) {
                    lastSequence = data.sequence;

                    if (data.response_length > 0) {
                        latestResponse = data.response;
                        responseData.textContent = data.response;
                        responseSummary.textContent = `${data.response_length} bytes`;
                    }

                    if (data.xcp_error)
                        message.textContent = `XCP error 0x${data.xcp_error.toString(16).padStart(2, '0').toUpperCase()}.`;
                    else if (data.connected)
                        message.textContent = 'XCP session is connected and ready.';
                    else if (data.open)
                        message.textContent = 'Transport configured. Connect to the slave.';
                }
            } catch (error) {
                status.textContent = 'Unavailable';
                status.classList.add('error');
                executeButton.disabled = true;
                message.textContent = error.message;
            }
        }

        element('xcp-apply').addEventListener('click', async () => {
            try {
                const extended = element('xcp-extended').checked;
                const fd = format.value === 'fd';
                const maximumId = extended ? 0x1fffffff : 0x7ff;

                await request({
                    action : 'configure',
                    bus : Number(element('xcp-bus').value),
                    command_identifier : parseHexNumber(
                        element('xcp-command-id'), maximumId, 'CRO identifier'),
                    response_identifier : parseHexNumber(
                        element('xcp-response-id'), maximumId, 'DTO identifier'),
                    extended,
                    can_fd : fd,
                    brs : fd && brs.checked,
                    transmit_data_length : Number(transmitLength.value),
                    padding_byte : parseHexNumber(
                        element('xcp-padding'), 0xff, 'Padding byte'),
                    timeout_ms : Number(element('xcp-timeout').value)
                });
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('xcp-connect').addEventListener('click', async () => {
            try {
                await request({
                    action : 'connect',
                    mode : parseHexNumber(
                        element('xcp-connect-mode'), 0xff, 'CONNECT mode')
                });
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('xcp-disconnect').addEventListener('click', async () => {
            try {
                await request({action : 'disconnect'});
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('xcp-close').addEventListener('click', async () => {
            try {
                await request({action : 'close'});
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        executeButton.addEventListener('click', async () => {
            try {
                const command = normalizeHex(commandInput.value);
                await request({action : 'execute', command});
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        async function discovery(body, submittedMessage) {
            try {
                await request(body);
                message.textContent = submittedMessage;
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        }

        element('xcp-get-status').addEventListener('click', () =>
            discovery({action : 'get_status'}, 'GET_STATUS completed.'));

        element('xcp-get-communication').addEventListener('click', () =>
            discovery(
                {action : 'get_comm_mode_info'},
                'GET_COMM_MODE_INFO completed.'));

        element('xcp-get-id').addEventListener('click', () =>
            discovery({
                action : 'get_id',
                type : Number(element('xcp-id-type').value)
            }, 'GET_ID completed.'));

        element('xcp-set-mta').addEventListener('click', () =>
            discovery({
                action : 'set_mta',
                address_extension : parseHexNumber(
                    element('xcp-address-extension'), 0xff, 'Address extension'),
                address : parseHexNumber(
                    element('xcp-address'), 0xffffffff, 'MTA address')
            }, 'SET_MTA completed.'));

        element('xcp-upload').addEventListener('click', () =>
            discovery({
                action : 'upload',
                count : Number(element('xcp-upload-count').value)
            }, 'UPLOAD completed.'));

        element('xcp-write-confirm').addEventListener('change', () => {
            element('xcp-write-memory').disabled =
                !element('xcp-write-confirm').checked ||
                status.textContent !== 'Connected';
        });

        element('xcp-write-memory').addEventListener('click', async () => {
            try {
                const address = parseHexNumber(
                    element('xcp-address'), 0xffffffff, 'MTA address');
                const rangeStart = parseHexNumber(
                    element('xcp-write-range-start'), 0xffffffff, 'Range start');
                const rangeEnd = parseHexNumber(
                    element('xcp-write-range-end'), 0xffffffff, 'Range end');
                const data = normalizeDataHex(element('xcp-write-data').value);

                if (rangeStart > rangeEnd ||
                    address < rangeStart ||
                    address > rangeEnd) {

                    throw new Error('The MTA address is outside the allowed range.');
                }

                const confirmed = window.confirm(
                    `Write ${data.split(' ').length} byte(s) at 0x${
                        address.toString(16).padStart(8, '0').toUpperCase()
                    }? This can change ECU operation.`
                );

                if (!confirmed)
                    return;

                await request({
                    action : 'write_memory',
                    confirmed : true,
                    address_extension : parseHexNumber(
                        element('xcp-address-extension'), 0xff, 'Address extension'),
                    address,
                    range_start : rangeStart,
                    range_end : rangeEnd,
                    data
                });
                element('xcp-write-confirm').checked = false;
                message.textContent = 'DOWNLOAD completed.';
                await refresh();
            } catch (error) {
                message.textContent = error.message;
            }
        });

        element('xcp-copy').addEventListener('click', async () => {
            if (!latestResponse)
                return;

            try {
                await navigator.clipboard.writeText(latestResponse);
                message.textContent = 'Response copied to the clipboard.';
            } catch (_) {
                message.textContent = 'Clipboard access is unavailable.';
            }
        });

        element('xcp-clear').addEventListener('click', () => {
            latestResponse = '';
            responseData.textContent = 'No response received.';
            responseSummary.textContent = 'No response';
        });

        format.addEventListener('change', updateFormat);
        commandInput.addEventListener('input', updateByteCount);
        updateFormat();
        updateByteCount();
        refresh();
        setInterval(refresh, 500);
    }

    function init_dbc() {
        const element = id => document.getElementById(id);
        const fileInput = element('dbc-file');
        const localFileName = element('dbc-local-file-name');
        const status = element('dbc-status');
        const message = element('dbc-message');
        const messageList = element('dbc-message-list');
        const signalList = element('dbc-signal-list');
        const valueGrid = element('dbc-value-grid');
        const selectedCount = element('dbc-selected-count');
        const frameData = element('dbc-frame-data');
        const frameTime = element('dbc-frame-time');
        let database = null;
        let currentMessage = null;
        let socket = null;
        let paused = false;
        let pending = false;
        let renderPending = false;
        let messageListDirty = true;
        let hoveredSignal = null;
        const latest = new Map();
        const histories = new Map();
        let selected = new Set();

        try {
            selected = new Set(JSON.parse(
                localStorage.getItem('spectra:dbc-selected-signals') || '[]'));
        } catch (_) {
            selected = new Set();
        }

        const escapeHtml = text => String(text)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
        const hex = (value, width = 2) =>
            Number(value).toString(16).toUpperCase().padStart(width, '0');
        const signalKey = (messageDefinition, signal) =>
            `${messageDefinition.id}:${signal.name}`;

        function parseDbc(text, name) {
            const result = {name, nodes : [], messages : new Map()};
            let activeMessage = null;

            for (const line of text.split(/\r?\n/)) {
                const nodes = line.match(/^\s*BU_\s*:\s*(.*)$/);

                if (nodes) {
                    result.nodes = nodes[1].trim().split(/\s+/).filter(Boolean);
                    continue;
                }

                const messageMatch = line.match(
                    /^\s*BO_\s+(\d+)\s+([^:]+):\s*(\d+)\s+(\S+)/);

                if (messageMatch) {
                    const encodedId = Number(messageMatch[1]);
                    const extended = encodedId > 0x1fffffff;
                    const id = extended ? encodedId & 0x1fffffff : encodedId;
                    activeMessage = {
                        id,
                        extended,
                        name : messageMatch[2].trim(),
                        dlc : Number(messageMatch[3]),
                        transmitter : messageMatch[4],
                        signals : []
                    };
                    result.messages.set(id, activeMessage);
                    continue;
                }

                const signalMatch = line.match(
                    /^\s*SG_\s+(\w+)(?:\s+(M|m\d+M?))?\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^|]+)\|([^\]]+)\]\s*"([^"]*)"\s*(.*)$/);

                if (signalMatch && activeMessage) {
                    const multiplex = signalMatch[2] || '';
                    activeMessage.signals.push({
                        name : signalMatch[1],
                        multiplexor : multiplex === 'M',
                        multiplexValue : multiplex.startsWith('m')
                            ? Number(multiplex.match(/^m(\d+)/)[1])
                            : null,
                        start : Number(signalMatch[3]),
                        length : Number(signalMatch[4]),
                        littleEndian : signalMatch[5] === '1',
                        signed : signalMatch[6] === '-',
                        factor : Number(signalMatch[7]),
                        offset : Number(signalMatch[8]),
                        minimum : Number(signalMatch[9]),
                        maximum : Number(signalMatch[10]),
                        unit : signalMatch[11],
                        receivers : signalMatch[12].split(',').map(value => value.trim()),
                        values : new Map()
                    });
                    continue;
                }

                const valueMatch = line.match(/^\s*VAL_\s+(\d+)\s+(\w+)\s+(.*);\s*$/);

                if (valueMatch) {
                    const encodedId = Number(valueMatch[1]);
                    const id = encodedId > 0x1fffffff
                        ? encodedId & 0x1fffffff
                        : encodedId;
                    const definition = result.messages.get(id);
                    const signal = definition && definition.signals.find(
                        item => item.name === valueMatch[2]);

                    if (signal) {
                        const expression = /(-?\d+)\s+"([^"]*)"/g;
                        let entry;

                        while ((entry = expression.exec(valueMatch[3])) !== null)
                            signal.values.set(entry[1], entry[2]);
                    }
                }
            }

            if (result.messages.size === 0)
                throw new Error('The file contains no supported BO_ messages.');

            return result;
        }

        function extractRaw(signal, bytes) {
            if (signal.length < 1 || signal.length > 64)
                return null;

            let raw = 0n;

            if (signal.littleEndian) {
                for (let index = 0; index < signal.length; ++index) {
                    const bit = signal.start + index;

                    if (Math.floor(bit / 8) >= bytes.length)
                        return null;

                    raw |= BigInt((bytes[Math.floor(bit / 8)] >> (bit % 8)) & 1) << BigInt(index);
                }
            } else {
                let bit = signal.start;

                for (let index = 0; index < signal.length; ++index) {
                    if (bit < 0 || Math.floor(bit / 8) >= bytes.length)
                        return null;

                    raw = (raw << 1n) |
                        BigInt((bytes[Math.floor(bit / 8)] >> (bit % 8)) & 1);
                    bit = (bit % 8 === 0) ? bit + 15 : bit - 1;
                }
            }

            if (signal.signed) {
                const sign = 1n << BigInt(signal.length - 1);

                if ((raw & sign) !== 0n)
                    raw -= 1n << BigInt(signal.length);
            }

            return raw;
        }

        function signalBits(signal) {
            const bits = [];

            if (signal.littleEndian) {
                for (let index = 0; index < signal.length; ++index)
                    bits.push(signal.start + index);
            } else {
                let bit = signal.start;

                for (let index = 0; index < signal.length; ++index) {
                    bits.push(bit);
                    bit = (bit % 8 === 0) ? bit + 15 : bit - 1;
                }
            }

            return new Set(bits);
        }

        function decodeSignal(definition, signal, frame) {
            if (!frame)
                return null;

            if (signal.multiplexValue !== null) {
                const multiplexor = definition.signals.find(item => item.multiplexor);
                const multiplexRaw = multiplexor && extractRaw(multiplexor, frame.data);

                if (multiplexRaw === null || Number(multiplexRaw) !== signal.multiplexValue)
                    return {inactive : true};
            }

            const raw = extractRaw(signal, frame.data);

            if (raw === null)
                return null;

            const numericRaw = Number(raw);
            const physical = numericRaw * signal.factor + signal.offset;
            return {
                raw,
                physical,
                text : signal.values.get(raw.toString()) || '',
                valid : physical >= signal.minimum && physical <= signal.maximum
            };
        }

        function selectedFrame(definition) {
            const bus = element('dbc-bus').value;
            let frame = null;

            for (const candidate of latest.values()) {
                if (candidate.id !== definition.id ||
                    Boolean(candidate.flags & 1) !== definition.extended ||
                    (bus !== 'all' && candidate.bus !== Number(bus)))
                    continue;

                if (!frame || candidate.receivedAt > frame.receivedAt)
                    frame = candidate;
            }

            return frame;
        }

        function formatValue(decoded, signal) {
            if (!decoded)
                return '—';
            if (decoded.inactive)
                return 'inactive';
            if (decoded.text)
                return `${decoded.text} (${decoded.physical})`;

            const value = Number.isInteger(decoded.physical)
                ? String(decoded.physical)
                : decoded.physical.toFixed(3).replace(/\.?0+$/, '');
            return signal.unit ? `${value} ${signal.unit}` : value;
        }

        function saveSelection() {
            try {
                localStorage.setItem(
                    'spectra:dbc-selected-signals',
                    JSON.stringify([...selected])
                );
            } catch (_) {
                /* Selection remains available until the page is closed. */
            }
        }

        function renderMessages() {
            if (!database)
                return;

            messageListDirty = false;

            const query = element('dbc-search').value.trim().toLowerCase();
            const activeOnly = element('dbc-active-only').checked;
            const definitions = [...database.messages.values()]
                .filter(definition => {
                    const frame = selectedFrame(definition);
                    return (!activeOnly || frame) &&
                        (!query || definition.name.toLowerCase().includes(query) ||
                            hex(definition.id, definition.extended ? 8 : 3).toLowerCase().includes(query));
                })
                .sort((left, right) => left.name.localeCompare(
                    right.name,
                    undefined,
                    {numeric : true, sensitivity : 'base'}
                ));
            const groups = new Map();

            for (const definition of definitions) {
                const first = definition.name.trim().charAt(0).toUpperCase();
                const group = /^[A-Z0-9]$/.test(first) ? first : '#';

                if (!groups.has(group))
                    groups.set(group, []);

                groups.get(group).push(definition);
            }

            element('dbc-message-count').textContent =
                `${definitions.length} message${definitions.length === 1 ? '' : 's'}`;
            messageList.innerHTML = [...groups.entries()].map(([group, entries]) =>
                `<details class="dbc-message-group" open><summary>${escapeHtml(group)}`
                + `<span>${entries.length}</span></summary>`
                + entries.map(definition => {
                    const frame = selectedFrame(definition);
                    const active = currentMessage === definition ? ' active' : '';
                    return `<button class="dbc-message-entry${active}" data-id="${definition.id}">`
                        + `<b>${escapeHtml(definition.name)}</b>`
                        + `<span>0x${hex(definition.id, definition.extended ? 8 : 3)} · ${definition.dlc} B`
                        + `${frame ? ' · live' : ''}</span></button>`;
                }).join('') + '</details>'
            ).join('') || '<p class="note">No matching messages.</p>';

            for (const button of messageList.querySelectorAll('[data-id]')) {
                button.onclick = () => {
                    currentMessage = database.messages.get(Number(button.dataset.id));
                    hoveredSignal = null;
                    render();
                };
            }
        }

        function renderSignals() {
            signalList.innerHTML = '';

            if (!currentMessage) {
                element('dbc-message-title').textContent = 'Message signals';
                element('dbc-message-meta').textContent = 'No message selected';
                renderInspector(null, null);
                return;
            }

            const frame = selectedFrame(currentMessage);
            element('dbc-message-title').textContent = currentMessage.name;
            element('dbc-message-meta').textContent =
                `0x${hex(currentMessage.id, currentMessage.extended ? 8 : 3)} · `
                + `${currentMessage.dlc} bytes · ${currentMessage.transmitter}`;

            signalList.innerHTML = currentMessage.signals.map(signal => {
                const key = signalKey(currentMessage, signal);
                const decoded = decodeSignal(currentMessage, signal, frame);
                return `<tr data-signal-row="${escapeHtml(signal.name)}"><td><input type="checkbox" data-signal="${escapeHtml(signal.name)}" `
                    + `${selected.has(key) ? 'checked' : ''}></td>`
                    + `<td>${escapeHtml(signal.name)}</td>`
                    + `<td>${escapeHtml(formatValue(decoded, signal))}</td>`
                    + `<td>${decoded && !decoded.inactive ? escapeHtml(decoded.raw.toString()) : '—'}</td>`
                    + `<td>${escapeHtml(signal.unit || '—')}</td>`
                    + `<td>${signal.start}|${signal.length}</td>`
                    + `<td>${signal.littleEndian ? 'Intel' : 'Motorola'} · ${signal.signed ? 'signed' : 'unsigned'}</td></tr>`;
            }).join('') || '<tr><td colspan="7">No supported SG_ signals.</td></tr>';

            for (const input of signalList.querySelectorAll('[data-signal]')) {
                input.onchange = () => {
                    const signal = currentMessage.signals.find(item => item.name === input.dataset.signal);
                    const key = signalKey(currentMessage, signal);
                    input.checked ? selected.add(key) : selected.delete(key);
                    saveSelection();
                    renderValues();
                };
            }

            for (const row of signalList.querySelectorAll('[data-signal-row]')) {
                row.onmouseenter = () => {
                    hoveredSignal = currentMessage.signals.find(
                        signal => signal.name === row.dataset.signalRow) || null;
                    renderInspector(frame, hoveredSignal);
                };
                row.onmouseleave = () => {
                    hoveredSignal = null;
                    renderInspector(frame, null);
                };
            }

            renderInspector(frame, hoveredSignal);
        }

        function renderInspector(frame, signal) {
            if (!currentMessage) {
                frameData.innerHTML =
                    '<p class="note">Select a message, then point to a signal row.</p>';
                frameTime.textContent = 'No frame received';
                return;
            }

            const byteCount = Math.max(
                currentMessage.dlc,
                frame ? frame.data.length : 0
            );
            const bytes = frame
                ? [...frame.data]
                : Array(byteCount).fill(0);
            const activeBits = signal ? signalBits(signal) : new Set();
            const describedBits = new Set();

            for (const messageSignal of currentMessage.signals) {
                for (const bit of signalBits(messageSignal)) {
                    if (bit < byteCount * 8)
                        describedBits.add(bit);
                }
            }

            const byteCells = [];
            const asciiCells = [];
            const bitRows = [];

            for (let byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
                const value = bytes[byteIndex] || 0;
                let byteActive = false;

                for (let bit = 0; bit < 8; ++bit) {
                    if (activeBits.has(byteIndex * 8 + bit)) {
                        byteActive = true;
                        break;
                    }
                }

                byteCells.push(
                    `<span class="dbc-frame-byte${byteActive ? ' active' : ''}">`
                    + `<small>${byteIndex}</small><b>${hex(value)}</b></span>`);
                const character = value >= 32 && value <= 126
                    ? String.fromCharCode(value)
                    : '·';
                asciiCells.push(
                    `<span class="dbc-frame-ascii${byteActive ? ' active' : ''}">`
                    + `${escapeHtml(character)}</span>`);

                const bits = [];

                for (let bitInByte = 7; bitInByte >= 0; --bitInByte) {
                    const absoluteBit = byteIndex * 8 + bitInByte;
                    const active = activeBits.has(absoluteBit);
                    const described = describedBits.has(absoluteBit);
                    const bitState = active
                        ? 'active'
                        : described ? 'described' : 'unused';
                    const stateDescription = active
                        ? `Selected signal: ${signal.name}`
                        : described ? 'Described by DBC' : 'Not described by DBC';
                    bits.push(
                        `<span class="${bitState}" `
                        + `title="Byte ${byteIndex}, bit ${bitInByte} · ${escapeHtml(stateDescription)}">`
                        + `${(value >> bitInByte) & 1}</span>`);
                }

                bitRows.push(
                    `<div class="dbc-bit-byte"><small>B${byteIndex}</small>`
                    + `<div>${bits.join('')}</div></div>`);
            }

            const signalDescription = signal
                ? `<b>${escapeHtml(signal.name)}</b> · start ${signal.start} · `
                    + `${signal.length} bits · ${signal.littleEndian ? 'Intel' : 'Motorola'}`
                : 'Point to a signal row to highlight its location.';
            frameData.innerHTML =
                `<div class="dbc-inspector-caption">${signalDescription}</div>`
                + `<div class="dbc-inspector-line"><span>HEX</span><div>${byteCells.join('')}</div></div>`
                + `<div class="dbc-inspector-line"><span>ASCII</span><div>${asciiCells.join('')}</div></div>`
                + `<div class="dbc-bit-view">`
                + `<div class="dbc-bit-heading"><span>Bit view · MSB 7 → 0 LSB</span>`
                + `<span class="dbc-bit-legend">`
                + `<i class="active"></i>Selected <i class="described"></i>DBC `
                + `<i class="unused"></i>Unused</span></div>`
                + `<div class="dbc-bit-grid">${bitRows.join('')}</div></div>`;

            frameTime.textContent = frame
                ? `${frame.bus === 0 ? 'Primary' : 'Secondary'} · `
                    + `${(Number(frame.timestamp) / 1000000).toFixed(6)} s`
                : 'DBC layout · no live frame';
        }

        function renderValues() {
            const values = [];
            const plots = [];
            const historyLimit = Number(element('dbc-history-limit').value);

            if (database) {
                for (const definition of database.messages.values()) {
                    const frame = selectedFrame(definition);

                    for (const signal of definition.signals) {
                        if (!selected.has(signalKey(definition, signal)))
                            continue;

                        const decoded = decodeSignal(definition, signal, frame);
                        const key = signalKey(definition, signal);
                        let history = histories.get(key);

                        if (!history) {
                            history = [];
                            histories.set(key, history);
                        }

                        if (decoded && !decoded.inactive && frame &&
                            history.at(-1)?.timestamp !== frame.timestamp) {

                            history.push({
                                timestamp : frame.timestamp,
                                value : decoded.physical
                            });

                            if (history.length > historyLimit)
                                history.splice(0, history.length - historyLimit);
                        }

                        const plotId = `dbc-plot-${plots.length}`;
                        plots.push({id : plotId, history});
                        values.push(`<article class="dbc-value${decoded && !decoded.inactive && !decoded.valid ? ' invalid' : ''}">`
                            + `<span>${escapeHtml(definition.name)} · 0x${hex(definition.id, definition.extended ? 8 : 3)}</span>`
                            + `<strong>${escapeHtml(signal.name)}</strong>`
                            + `<b>${escapeHtml(formatValue(decoded, signal))}</b>`
                            + `<canvas id="${plotId}" class="dbc-sparkline" aria-label="${escapeHtml(signal.name)} history"></canvas>`
                            + `</article>`);
                    }
                }
            }

            selectedCount.textContent = `${values.length} signal${values.length === 1 ? '' : 's'}`;
            valueGrid.classList.toggle('empty', values.length === 0);
            valueGrid.innerHTML = values.join('') ||
                '<p class="note">Select signals from a message to build your dashboard.</p>';

            for (const plot of plots)
                drawHistory(element(plot.id), plot.history);
        }

        function drawHistory(canvas, history) {
            if (!canvas)
                return;

            const width = Math.max(120, canvas.clientWidth);
            const height = Math.max(46, canvas.clientHeight);
            const ratio = window.devicePixelRatio || 1;
            canvas.width = Math.round(width * ratio);
            canvas.height = Math.round(height * ratio);
            const context = canvas.getContext('2d');
            context.scale(ratio, ratio);
            context.clearRect(0, 0, width, height);

            if (history.length < 2)
                return;

            let minimum = Math.min(...history.map(point => point.value));
            let maximum = Math.max(...history.map(point => point.value));

            if (minimum === maximum) {
                minimum -= 0.5;
                maximum += 0.5;
            }

            context.strokeStyle = '#3d85f5';
            context.lineWidth = 1.5;
            context.beginPath();

            history.forEach((point, index) => {
                const x = index * (width - 2) / (history.length - 1) + 1;
                const y = height - 2 -
                    (point.value - minimum) / (maximum - minimum) * (height - 4);
                index === 0 ? context.moveTo(x, y) : context.lineTo(x, y);
            });
            context.stroke();
        }

        function render() {
            renderPending = false;
            renderMessages();
            renderSignals();
            renderValues();
        }

        function scheduleRender() {
            if (renderPending)
                return;
            renderPending = true;
            setTimeout(() => {
                renderPending = false;

                if (messageListDirty)
                    renderMessages();

                renderSignals();
                renderValues();
            }, 100);
        }

        function parseBatch(buffer) {
            const view = new DataView(buffer);
            const frames = [];

            if (view.byteLength < 8 || view.getUint8(0) !== 1 ||
                view.getUint8(1) !== 1 ||
                view.getUint32(4, true) !== view.byteLength - 8)
                throw new Error('Invalid CAN WebSocket batch.');

            let offset = 8;

            for (let index = 0; index < view.getUint16(2, true); ++index) {
                if (offset + 40 > view.byteLength)
                    throw new Error('Truncated CAN event.');

                const length = view.getUint8(offset + 5);

                if (length > 64 || offset + 40 + length > view.byteLength)
                    throw new Error('Truncated CAN payload.');

                if (view.getUint8(offset) === 0) {
                    frames.push({
                        bus : view.getUint8(offset + 1),
                        flags : view.getUint8(offset + 3),
                        id : view.getUint32(offset + 20, true),
                        timestamp : view.getBigUint64(offset + 28, true),
                        data : Array.from(new Uint8Array(buffer, offset + 40, length)),
                        receivedAt : performance.now()
                    });
                }

                offset += 40 + length;
            }

            return frames;
        }

        function controls() {
            const connected = socket && socket.readyState === WebSocket.OPEN;
            element('dbc-connect').textContent = socket ? 'Disconnect' : 'Connect live';
            element('dbc-pause').disabled = !connected || pending;
            element('dbc-pause').textContent = paused ? 'Resume stream' : 'Pause stream';
            status.textContent = socket
                ? connected ? paused ? 'Paused' : 'Live' : 'Connecting…'
                : database ? 'DBC loaded' : 'No DBC';
        }

        function subscribe(nextPaused) {
            if (!socket || socket.readyState !== WebSocket.OPEN || pending)
                return;

            pending = true;
            socket.send(JSON.stringify({
                command : 'subscribe',
                primary : true,
                secondary : true,
                rx : true,
                tx : true,
                paused : nextPaused
            }));
            controls();
        }

        function closeSocket() {
            if (socket) {
                const previous = socket;
                socket = null;
                previous.close();
            }
            pending = false;
            paused = false;
            controls();
        }

        function loadDatabase(text, name) {
            database = parseDbc(text, name);
            currentMessage = database.messages.values().next().value || null;
            latest.clear();
            histories.clear();
            messageListDirty = true;
            element('dbc-summary').textContent =
                `${name} · ${database.messages.size} messages · `
                + `${[...database.messages.values()].reduce((sum, item) => sum + item.signals.length, 0)} signals`;
            element('dbc-connect').disabled = false;
            message.textContent = 'DBC parsed locally. Select signals or connect to live CAN.';
            render();
            controls();
        }

        fileInput.onchange = async () => {
            const file = fileInput.files[0];

            if (!file) {
                localFileName.textContent = 'No file chosen';
                localFileName.title = 'No file chosen';
                return;
            }

            localFileName.textContent = file.name;
            localFileName.title = file.name;

            if (file.size > 16 * 1024 * 1024) {
                message.textContent = 'DBC file exceeds the 16 MiB browser limit.';
                return;
            }

            try {
                loadDatabase(await file.text(), file.name);
            } catch (error) {
                message.textContent = error.message;
            }
        };

        async function refreshSdFiles() {
            const selector = element('dbc-sd-file');
            const button = element('dbc-sd-refresh');
            button.disabled = true;
            selector.innerHTML = '<option value="">Loading /dbc…</option>';
            element('dbc-sd-open').disabled = true;

            try {
                const files = [];
                let offset = 0;
                let hasMore = false;

                do {
                    const query = new URLSearchParams({
                        volume : 'sd',
                        path : '/dbc',
                        offset : String(offset),
                        limit : '32'
                    });
                    const response = await fetch(
                        `/api/files?${query}`,
                        {cache : 'no-store'}
                    );
                    const result = await response.json();

                    if (!response.ok)
                        throw new Error(result.message || `HTTP ${response.status}`);
                    if (!Array.isArray(result.entries))
                        throw new Error('Invalid SD file-list response.');

                    files.push(...result.entries.filter(entry =>
                        entry.type === 'file' && /\.dbc$/i.test(entry.name)));
                    offset += result.entries.length;
                    hasMore = result.has_more === true;
                } while (hasMore && (offset <= 1024));

                files.sort((left, right) =>
                    left.name.localeCompare(
                        right.name,
                        undefined,
                        {sensitivity : 'base'}
                    ));
                selector.innerHTML = '<option value="">Choose a DBC file</option>';

                for (const file of files) {
                    const option = document.createElement('option');
                    option.value = `/dbc/${file.name}`;
                    option.textContent = file.name;
                    selector.append(option);
                }

                if (files.length === 0)
                    message.textContent = 'No .dbc files found in the SD /dbc directory.';
            } catch (error) {
                selector.innerHTML = '<option value="">SD /dbc unavailable</option>';
                message.textContent = `Failed to list SD DBC files: ${error.message}`;
            } finally {
                button.disabled = false;
            }
        }

        element('dbc-sd-file').onchange = () => {
            element('dbc-sd-open').disabled = !element('dbc-sd-file').value;
        };
        element('dbc-sd-refresh').onclick = refreshSdFiles;
        element('dbc-sd-open').onclick = async () => {
            const path = element('dbc-sd-file').value;

            if (!path)
                return;

            element('dbc-sd-open').disabled = true;

            try {
                const query = new URLSearchParams({volume : 'sd', path});
                const response = await fetch(`/api/files/download?${query}`);

                if (!response.ok)
                    throw new Error(`HTTP ${response.status}`);

                const text = await response.text();

                if (text.length > 16 * 1024 * 1024)
                    throw new Error('DBC file exceeds the 16 MiB browser limit.');

                loadDatabase(text, path.split('/').pop());
            } catch (error) {
                message.textContent = `Failed to open SD DBC file: ${error.message}`;
            } finally {
                element('dbc-sd-open').disabled = !path;
            }
        };

        element('dbc-connect').onclick = () => {
            if (socket) {
                closeSocket();
                return;
            }

            const connection = new WebSocket(
                `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws/can`);
            connection.binaryType = 'arraybuffer';
            socket = connection;
            controls();
            connection.onopen = () => socket === connection && subscribe(false);
            connection.onmessage = event => {
                if (socket !== connection)
                    return;

                try {
                    if (typeof event.data === 'string') {
                        const reply = JSON.parse(event.data);

                        if (reply.type === 'subscription') {
                            pending = false;
                            paused = reply.paused;
                            controls();
                        } else if (reply.type === 'error') {
                            message.textContent = reply.code || 'CAN stream error.';
                        }
                        return;
                    }

                    for (const frame of parseBatch(event.data)) {
                        const key = `${frame.bus}:${frame.id}`;

                        if (!latest.has(key))
                            messageListDirty = true;

                        latest.set(key, frame);
                    }
                    scheduleRender();
                } catch (error) {
                    message.textContent = error.message;
                }
            };
            connection.onerror = () => message.textContent = 'CAN WebSocket connection failed.';
            connection.onclose = () => socket === connection && closeSocket();
        };
        element('dbc-pause').onclick = () => subscribe(!paused);
        element('dbc-search').oninput = renderMessages;
        element('dbc-active-only').onchange = renderMessages;
        element('dbc-bus').onchange = render;
        element('dbc-history-limit').onchange = () => {
            const limit = Number(element('dbc-history-limit').value);

            for (const history of histories.values()) {
                if (history.length > limit)
                    history.splice(0, history.length - limit);
            }

            renderValues();
        };
        element('dbc-clear-history').onclick = () => {
            histories.clear();
            renderValues();
            message.textContent = 'Signal graph history cleared.';
        };
        controls();
        refreshSdFiles();
    }

    function init_navigation() {
        const entries = [
            ['/', 'Overview'],
            ['/can_logger', 'CAN Logger'],
            ['/can_analyzer', 'CAN Analyzer'],
            ['/dbc', 'DBC'],
            ['/isotp', 'ISO-TP'],
            ['/xcp', 'XCP'],
            ['/uds_programming', 'Programming'],
            ['/files', 'Files'],
            ['/settings', 'Settings'],
            ['/diagnostics', 'Diagnostics']
        ];
        const currentPath = location.pathname.replace(/\/$/, '') || '/';

        for (const navigation of document.querySelectorAll('.spectra-nav')) {
            const links = entries.map(([path, label]) => {
                const link = document.createElement('a');
                link.href = path;
                link.textContent = label;

                if (currentPath === path)
                    link.setAttribute('aria-current', 'page');

                return link;
            });

            navigation.replaceChildren(...links);
        }
    }

    init_navigation();

    const pages = {
        'page-overview' : init_overview,
        'page-settings' : init_settings,
        'page-files' : init_files,
        'page-diagnostics' : init_diagnostics,
        'page-logger' : init_logger,
        'page-analyzer' : init_analyzer,
        'page-dbc' : init_dbc,
        'page-xcp' : init_xcp,
        'page-isotp' : init_diagnostics_transport,
        'page-uds-programming' : init_uds_programming
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
