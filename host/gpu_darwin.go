//go:build darwin

package main

import (
	"regexp"
	"strconv"
	"strings"
	"time"
)

var (
	ioregUtilRe  = regexp.MustCompile(`"(?:Device Utilization %|Renderer Utilization %|Tiler Utilization %|gpu-core-utilization)"\s*=\s*(\d+(?:\.\d+)?)`)
	ioregClockRe = regexp.MustCompile(`"(?:gpu-core-clocks|GPU Core Clock|Core Clock|current-gpu-frequency)"\s*=\s*(\d+(?:\.\d+)?)`)
	pmGPURe      = regexp.MustCompile(`GPU (?:HW )?active residency:\s*(\d+(?:\.\d+)?)%`)
	pmGPUFreqRe  = regexp.MustCompile(`GPU (?:HW )?active frequency:\s*(\d+(?:\.\d+)?)\s*MHz`)
	chipsetRe    = regexp.MustCompile(`Chipset Model:\s*(.+)`)
)

func platformGPUUtil() (float64, bool) {
	if v, ok := gpuFromIOReg(); ok {
		return v, true
	}
	return gpuFromPowermetrics()
}

func gpuFromIOReg() (float64, bool) {
	out, ok := runCmd(2*time.Second, "ioreg", "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator")
	if !ok {
		return 0, false
	}
	m := ioregUtilRe.FindStringSubmatch(out)
	if m == nil {
		return 0, false
	}
	v, err := strconv.ParseFloat(m[1], 64)
	return v, err == nil
}

func gpuFromPowermetrics() (float64, bool) {
	out, ok := runCmd(4*time.Second, "powermetrics", "--samplers", "gpu_power", "-n", "1", "-i", "200")
	if !ok {
		return 0, false
	}
	m := pmGPURe.FindStringSubmatch(out)
	if m == nil {
		return 0, false
	}
	v, err := strconv.ParseFloat(m[1], 64)
	return v, err == nil
}

func platformGPUFreq() (float64, bool) {
	if out, ok := runCmd(2*time.Second, "ioreg", "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"); ok {
		if m := ioregClockRe.FindStringSubmatch(out); m != nil {
			if v, err := strconv.ParseFloat(m[1], 64); err == nil && v > 0 {
				if v > 20 && v < 500 {
					return v * 1000, true // GHz-style reading
				}
				return v, true
			}
		}
	}
	out, ok := runCmd(4*time.Second, "powermetrics", "--samplers", "gpu_power", "-n", "1", "-i", "200")
	if !ok {
		return 0, false
	}
	m := pmGPUFreqRe.FindStringSubmatch(out)
	if m == nil {
		return 0, false
	}
	v, err := strconv.ParseFloat(m[1], 64)
	return v, err == nil && v > 0
}

func platformCPUModel() string {
	out, ok := runCmd(4*time.Second, "sysctl", "-n", "machdep.cpu.brand_string")
	if !ok {
		return ""
	}
	return strings.TrimSpace(out)
}

func platformGPUModel() string {
	out, ok := runCmd(8*time.Second, "system_profiler", "SPDisplaysDataType")
	if !ok {
		return ""
	}
	m := chipsetRe.FindStringSubmatch(out)
	if m == nil {
		return ""
	}
	return strings.TrimSpace(m[1])
}

func platformCPUFreqMHz() float64 {
	out, ok := runCmd(2*time.Second, "sysctl", "-n", "hw.cpufrequency")
	if !ok {
		return 0
	}
	hz, err := strconv.ParseFloat(strings.TrimSpace(out), 64)
	if err != nil || hz <= 0 {
		return 0
	}
	if hz > 1e6 {
		return hz / 1e6
	}
	return hz
}
