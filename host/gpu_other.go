//go:build !darwin && !linux && !windows

package main

func platformGPUUtil() (float64, bool) { return 0, false }

func platformGPUFreq() (float64, bool) { return 0, false }

func platformCPUModel() string { return "" }

func platformGPUModel() string { return "" }

func platformCPUFreqMHz() float64 { return 0 }
