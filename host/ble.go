//go:build !nobile

package main

import (
	"fmt"
	"time"

	"tinygo.org/x/bluetooth"
)

const (
	rainmonitorServiceUUID = "6b1a0001-7c21-4f3a-9b5e-2d8f0a1c4e70"
	rainmonitorRxUUID      = "6b1a0002-7c21-4f3a-9b5e-2d8f0a1c4e70"
	bleWriteChunk          = 160
)

type bleLink struct {
	device bluetooth.Device
	char   bluetooth.DeviceCharacteristic
}

func (l *bleLink) Write(p []byte) (int, error) {
	sent := 0
	for sent < len(p) {
		n := bleWriteChunk
		if sent+n > len(p) {
			n = len(p) - sent
		}
		chunk := p[sent : sent+n]
		wn, err := l.char.WriteWithoutResponse(chunk)
		if err != nil {
			wn, err = l.char.Write(chunk)
		}
		if err != nil {
			return sent, err
		}
		if wn == 0 {
			return sent, fmt.Errorf("bluetooth write returned 0")
		}
		sent += wn
	}
	return sent, nil
}

func (l *bleLink) Close() error {
	return l.device.Disconnect()
}

func openBLE(wantName string, timeout time.Duration) (lineSink, error) {
	adapter := bluetooth.DefaultAdapter
	if adapter == nil {
		return nil, fmt.Errorf("no bluetooth adapter")
	}
	if err := adapter.Enable(); err != nil {
		return nil, fmt.Errorf("enable adapter: %w", err)
	}

	serviceUUID := mustParseUUID(rainmonitorServiceUUID)
	rxUUID := mustParseUUID(rainmonitorRxUUID)

	var addr bluetooth.Address
	found := false
	timer := time.AfterFunc(timeout, func() {
		_ = adapter.StopScan()
	})
	err := adapter.Scan(func(a *bluetooth.Adapter, result bluetooth.ScanResult) {
		name := result.LocalName()
		if name != wantName && !result.HasServiceUUID(serviceUUID) {
			return
		}
		addr = result.Address
		found = true
		_ = a.StopScan()
	})
	timer.Stop()
	if err != nil && !found {
		return nil, fmt.Errorf("scan: %w", err)
	}
	if !found {
		return nil, fmt.Errorf("device %q not found", wantName)
	}

	device, err := adapter.Connect(addr, bluetooth.ConnectionParams{})
	if err != nil {
		return nil, fmt.Errorf("connect: %w", err)
	}
	services, err := device.DiscoverServices([]bluetooth.UUID{serviceUUID})
	if err != nil || len(services) == 0 {
		_ = device.Disconnect()
		if err == nil {
			err = fmt.Errorf("service missing")
		}
		return nil, fmt.Errorf("discover services: %w", err)
	}
	chars, err := services[0].DiscoverCharacteristics([]bluetooth.UUID{rxUUID})
	if err != nil || len(chars) == 0 {
		_ = device.Disconnect()
		if err == nil {
			err = fmt.Errorf("rx characteristic missing")
		}
		return nil, fmt.Errorf("discover characteristics: %w", err)
	}
	return &bleLink{device: device, char: chars[0]}, nil
}

func mustParseUUID(s string) bluetooth.UUID {
	u, err := bluetooth.ParseUUID(s)
	if err != nil {
		panic(err)
	}
	return u
}
