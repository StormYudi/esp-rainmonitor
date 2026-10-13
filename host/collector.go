package main

import (
	"errors"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"
)

type collectorConfig struct {
	Port       string
	Baud       int
	Interval   time.Duration
	Once       bool
	Transport  string
	BLEName    string
	BLEWait    time.Duration
	Service    bool
	Quiet      bool
	ResultPath string
}

func runCollector(cfg collectorConfig, stop <-chan struct{}) error {
	interrupt := waitStop(stop)

	cpuName := cpuModel()
	gpuName := gpuModel()
	if !cfg.Quiet {
		fmt.Printf("CPU %s  GPU %s\n", cpuName, gpuName)
	}

	gpu := gpuFilter{}
	net := netRates{}

	for {
		sink, label, err := openLink(cfg.Transport, cfg.Port, cfg.BLEName, cfg.Baud, cfg.BLEWait)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%v; retrying\n", err)
			select {
			case <-interrupt:
				if !cfg.Quiet {
					fmt.Println()
				}
				return nil
			case <-time.After(time.Second):
			}
			continue
		}
		if !cfg.Quiet {
			fmt.Printf("Opening %s\n", label)
		} else {
			fmt.Fprintf(os.Stderr, "opening %s\n", label)
		}

		watchUSB := strings.EqualFold(cfg.Transport, "auto") && label == "bluetooth"
		err = runSession(sink, cfg, cpuName, gpuName, &gpu, &net, interrupt, watchUSB)
		_ = sink.Close()
		if err == nil || cfg.Once {
			return nil
		}
		if isInterrupt(err) {
			if !cfg.Quiet {
				fmt.Println()
			}
			return nil
		}
		if errors.Is(err, errPreferUSB) {
			fmt.Fprintf(os.Stderr, "\n%s; switching to USB\n", err)
		} else {
			fmt.Fprintf(os.Stderr, "\nlink lost: %v; retrying\n", err)
		}
		select {
		case <-interrupt:
			if !cfg.Quiet {
				fmt.Println()
			}
			return nil
		case <-time.After(time.Second):
		}
	}
}

func runSession(sink lineSink, cfg collectorConfig, cpuName, gpuName string, gpu *gpuFilter, net *netRates, interrupt <-chan struct{}, watchUSB bool) error {
	for {
		done := make(chan struct{})
		var cpuVal float64
		var cores []float64
		go func() {
			cpuVal, cores = cpuStats(cfg.Interval)
			close(done)
		}()
		select {
		case <-interrupt:
			return errInterrupted
		case <-done:
		}

		reading := readGPU()
		gpuVal, gpuOK := gpu.update(reading.util, reading.utilOK)
		freq, freqOK := cpuFreqMHz()
		memPct, memUsed, memTotal, memOK := memoryStats()
		up, down := net.read()

		s := sample{
			CPU:          cpuVal,
			GPU:          gpuVal,
			GPUValid:     gpuOK,
			FreqMHz:      freq,
			FreqValid:    freqOK,
			GPUFreqMHz:   reading.freqMHz,
			GPUFreqValid: reading.freqOK,
			MemPct:       memPct,
			MemUsedGB:    memUsed,
			MemTotalGB:   memTotal,
			MemValid:     memOK,
			NetUpKBs:     up,
			NetDownKBs:   down,
			CPUModel:     cpuName,
			GPUModel:     gpuName,
			Cores:        cores,
		}
		if _, err := sink.Write(encodeLine(s)); err != nil {
			return err
		}
		if watchUSB {
			if name, ok := usbReady(cfg.Port); ok {
				return fmt.Errorf("%w: %s", errPreferUSB, name)
			}
		}

		if !cfg.Quiet {
			printSample(cpuVal, freq, freqOK, gpuVal, gpuOK, reading, memPct, memOK, down, up)
		}

		if cfg.Once {
			if !cfg.Quiet {
				fmt.Println()
			}
			return nil
		}
	}
}

func printSample(cpuVal, freq float64, freqOK bool, gpuVal float64, gpuOK bool, reading gpuReading, memPct float64, memOK bool, down, up float64) {
	gpuText := "n/a"
	if gpuOK {
		gpuText = fmt.Sprintf("%.1f%%", gpuVal)
	}
	if reading.freqOK {
		if reading.freqMHz >= 1000 {
			gpuText += fmt.Sprintf(" %.2fGHz", reading.freqMHz/1000)
		} else {
			gpuText += fmt.Sprintf(" %.0fMHz", reading.freqMHz)
		}
	}
	freqText := "--"
	if freqOK {
		freqText = fmt.Sprintf("%.2fGHz", freq/1000)
	}
	memText := "--"
	if memOK {
		memText = fmt.Sprintf("%.0f%%", memPct)
	}
	fmt.Printf("cpu=%5.1f%% %s  gpu=%s  mem=%s  dn=%.0fKB/s up=%.0fKB/s\r",
		cpuVal, freqText, gpuText, memText, down, up)
}

func waitStop(stop <-chan struct{}) <-chan struct{} {
	out := make(chan struct{})
	var once sync.Once
	done := func() { once.Do(func() { close(out) }) }
	go func() {
		sigs := make(chan os.Signal, 1)
		signal.Notify(sigs, os.Interrupt, syscall.SIGTERM)
		select {
		case <-sigs:
		case <-stop:
		}
		done()
	}()
	return out
}

var errInterrupted = fmt.Errorf("interrupted")

func isInterrupt(err error) bool {
	return err == errInterrupted
}
