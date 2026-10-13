package main

import "testing"

func TestScorePortNeedsTwoHitsForAutoUSB(t *testing.T) {
	if scorePort(portInfo{Name: "/dev/cu.debug-console"}) >= 2 {
		t.Fatal("generic cu device should not look like mosaico")
	}
	if scorePort(portInfo{Name: "/dev/cu.usbmodem1234561"}) < 2 {
		t.Fatal("mac CDC usbmodem should be enough to switch off BLE")
	}
}

func TestScorePortRejectsBootloaderPort(t *testing.T) {
	if scorePort(portInfo{Name: "/dev/cu.usbmodem1101"}) >= 0 {
		t.Fatal("ESP32-S31 download port must not be treated as app USB")
	}
}

func TestScorePortRejectsBluetoothIncoming(t *testing.T) {
	if scorePort(portInfo{Name: "/dev/cu.Bluetooth-Incoming-Port"}) >= 0 {
		t.Fatal("macOS Bluetooth-Incoming-Port must not be treated as USB")
	}
	if _, score := pickScoredPort([]portInfo{{Name: "/dev/cu.Bluetooth-Incoming-Port"}}); score >= minAutoPortScore {
		t.Fatal("incoming port alone must not win auto USB")
	}
}

func TestScorePortPrefersRainMonitor(t *testing.T) {
	device := portInfo{Name: "/dev/cu.usbmodem1234561", Product: "RainMonitor", Manufacturer: "Espressif", VID: "303A"}
	other := portInfo{Name: "/dev/cu.debug-console"}
	if scorePort(device) <= scorePort(other) {
		t.Fatalf("rainmonitor score %d, other %d", scorePort(device), scorePort(other))
	}
	if scorePort(portInfo{Name: "/dev/cu.usbmodem1"}) <= scorePort(portInfo{Name: "/dev/tty.usbmodem1"}) {
		t.Fatal("cu should outrank tty")
	}
}
