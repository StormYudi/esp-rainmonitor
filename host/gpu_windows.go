//go:build windows

package main

import (
	"github.com/yusufpapurcu/wmi"
)

type win32Processor struct {
	Name              string
	CurrentClockSpeed uint32
}

type win32VideoController struct {
	Name string
}

type gpuEngine struct {
	UtilizationPercentage uint64
}

type nvGpuClock struct {
	GraphicsClock     uint32
	CurrentClockSpeed uint32
	Clock             uint32
}

func platformGPUUtil() (float64, bool) {
	var rows []gpuEngine
	queries := []string{
		`SELECT UtilizationPercentage FROM Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine`,
		`SELECT UtilizationPercentage FROM Win32_PerfFormattedData_GPUEngine`,
	}
	for _, q := range queries {
		rows = nil
		if err := wmi.Query(q, &rows); err != nil || len(rows) == 0 {
			continue
		}
		var max float64
		found := false
		for _, row := range rows {
			v := float64(row.UtilizationPercentage)
			if !found || v > max {
				max = v
				found = true
			}
		}
		if found {
			return max, true
		}
	}
	return 0, false
}

func platformGPUFreq() (float64, bool) {
	queries := []struct {
		ns string
		q  string
	}{
		{`root\CIMV2\NV`, `SELECT GraphicsClock FROM Gpu`},
		{`root\CIMV2\NV`, `SELECT CurrentClockSpeed FROM Gpu`},
		{`root\cimv2`, `SELECT CurrentClockSpeed FROM Win32_VideoController`},
	}
	for _, item := range queries {
		var rows []nvGpuClock
		if err := wmi.QueryNamespace(item.q, &rows, item.ns); err != nil || len(rows) == 0 {
			continue
		}
		for _, row := range rows {
			mhz := float64(row.GraphicsClock)
			if mhz <= 0 {
				mhz = float64(row.CurrentClockSpeed)
			}
			if mhz <= 0 {
				mhz = float64(row.Clock)
			}
			if mhz > 0 {
				return mhz, true
			}
		}
	}
	return 0, false
}

func platformCPUModel() string {
	var rows []win32Processor
	if err := wmi.Query("SELECT Name FROM Win32_Processor", &rows); err != nil || len(rows) == 0 {
		return ""
	}
	return rows[0].Name
}

func platformGPUModel() string {
	var rows []win32VideoController
	if err := wmi.Query("SELECT Name FROM Win32_VideoController", &rows); err != nil {
		return ""
	}
	for _, row := range rows {
		if row.Name != "" {
			return row.Name
		}
	}
	return ""
}

func platformCPUFreqMHz() float64 {
	var rows []win32Processor
	if err := wmi.Query("SELECT CurrentClockSpeed FROM Win32_Processor", &rows); err != nil || len(rows) == 0 {
		return 0
	}
	return float64(rows[0].CurrentClockSpeed)
}
