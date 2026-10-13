package main

import (
	"context"
	"os/exec"
	"strings"
	"time"

	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/mem"
	"github.com/shirou/gopsutil/v4/net"
)

const modelLimit = 36

type sample struct {
	CPU          float64
	GPU          float64
	GPUValid     bool
	FreqMHz      float64
	FreqValid    bool
	GPUFreqMHz   float64
	GPUFreqValid bool
	MemPct       float64
	MemUsedGB    float64
	MemTotalGB   float64
	MemValid     bool
	NetUpKBs     float64
	NetDownKBs   float64
	CPUModel     string
	GPUModel     string
	Cores        []float64
}

type gpuFilter struct {
	value float64
	ok    bool
}

func (g *gpuFilter) update(raw float64, valid bool) (float64, bool) {
	if !valid {
		return g.value, g.ok
	}
	sample := clampPct(raw)
	if !g.ok {
		g.value = sample
		g.ok = true
	} else {
		g.value = g.value*0.45 + sample*0.55
	}
	return g.value, true
}

type netRates struct {
	prevSent uint64
	prevRecv uint64
	prevT    time.Time
	ready    bool
}

func (n *netRates) read() (up, down float64) {
	counters, err := net.IOCounters(false)
	if err != nil || len(counters) == 0 {
		return 0, 0
	}
	now := time.Now()
	sent := counters[0].BytesSent
	recv := counters[0].BytesRecv
	if !n.ready {
		n.prevSent = sent
		n.prevRecv = recv
		n.prevT = now
		n.ready = true
		return 0, 0
	}
	dt := now.Sub(n.prevT).Seconds()
	if dt < 0.001 {
		dt = 0.001
	}
	up = float64(diffU64(sent, n.prevSent)) / dt / 1024
	down = float64(diffU64(recv, n.prevRecv)) / dt / 1024
	n.prevSent = sent
	n.prevRecv = recv
	n.prevT = now
	return up, down
}

func diffU64(cur, prev uint64) uint64 {
	if cur >= prev {
		return cur - prev
	}
	return 0
}

func clampPct(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 100 {
		return 100
	}
	return v
}

func cpuStats(interval time.Duration) (overall float64, cores []float64) {
	vals, err := cpu.Percent(interval, true)
	if err != nil || len(vals) == 0 {
		return 0, nil
	}
	cores = make([]float64, 0, len(vals))
	sum := 0.0
	for _, v := range vals {
		c := clampPct(v)
		cores = append(cores, c)
		sum += c
	}
	if len(cores) > 16 {
		cores = cores[:16]
	}
	if len(cores) > 0 {
		overall = sum / float64(len(vals))
	}
	return overall, cores
}

func cpuFreqMHz() (float64, bool) {
	infos, err := cpu.Info()
	if err == nil {
		for _, info := range infos {
			if mhz := normalizeFreqMHz(info.Mhz); mhz > 0 {
				return mhz, true
			}
		}
	}
	if mhz := normalizeFreqMHz(platformCPUFreqMHz()); mhz > 0 {
		return mhz, true
	}
	return 0, false
}

func normalizeFreqMHz(mhz float64) float64 {
	if mhz <= 0 {
		return 0
	}
	// Apple / some hosts report GHz as a small number like 4.0.
	if mhz >= 0.5 && mhz <= 20 {
		return mhz * 1000
	}
	return mhz
}

func memoryStats() (pct, usedGB, totalGB float64, ok bool) {
	vm, err := mem.VirtualMemory()
	if err != nil || vm.Total == 0 {
		return 0, 0, 0, false
	}
	const gib = 1024 * 1024 * 1024
	used := float64(vm.Total-vm.Available) / gib
	total := float64(vm.Total) / gib
	return vm.UsedPercent, used, total, true
}

func cpuModel() string {
	infos, err := cpu.Info()
	if err == nil {
		for _, info := range infos {
			if name := sanitizeModel(info.ModelName, modelLimit); name != "" {
				return name
			}
		}
	}
	if name := sanitizeModel(platformCPUModel(), modelLimit); name != "" {
		return name
	}
	return "CPU"
}

func gpuModel() string {
	if out, ok := runCmd(4*time.Second, "nvidia-smi", "--query-gpu=name", "--format=csv,noheader"); ok {
		for _, line := range strings.Split(out, "\n") {
			if name := sanitizeModel(strings.TrimSpace(line), modelLimit); name != "" {
				return name
			}
		}
	}
	if name := sanitizeModel(platformGPUModel(), modelLimit); name != "" {
		return name
	}
	return "GPU"
}

func runCmd(timeout time.Duration, name string, args ...string) (string, bool) {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Stderr = nil
	out, err := cmd.Output()
	if err != nil {
		return "", false
	}
	return string(out), true
}
