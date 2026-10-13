package main

import (
	"strconv"
	"strings"
	"time"
)

type gpuReading struct {
	util    float64
	utilOK  bool
	freqMHz float64
	freqOK  bool
}

func readGPU() gpuReading {
	if g := gpuFromNvidia(); g.utilOK || g.freqOK {
		if !g.freqOK {
			g.freqMHz, g.freqOK = platformGPUFreq()
		}
		return g
	}
	util, utilOK := platformGPUUtil()
	freq, freqOK := platformGPUFreq()
	return gpuReading{util: util, utilOK: utilOK, freqMHz: freq, freqOK: freqOK}
}

func gpuFromNvidia() gpuReading {
	out, ok := runCmd(2*time.Second, "nvidia-smi",
		"--query-gpu=utilization.gpu,clocks.current.graphics",
		"--format=csv,noheader,nounits")
	if !ok {
		return gpuReading{}
	}

	var best gpuReading
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		parts := strings.Split(line, ",")
		if len(parts) == 0 {
			continue
		}
		util, err := strconv.ParseFloat(strings.TrimSpace(parts[0]), 64)
		if err != nil {
			continue
		}
		g := gpuReading{util: util, utilOK: true}
		if len(parts) > 1 {
			if freq, ferr := strconv.ParseFloat(strings.TrimSpace(parts[1]), 64); ferr == nil && freq > 0 {
				g.freqMHz = freq
				g.freqOK = true
			}
		}
		if !best.utilOK || g.util > best.util {
			best = g
		}
	}
	return best
}
