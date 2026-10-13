//go:build windows

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"

	"golang.org/x/sys/windows"
)

func processElevated() bool {
	return windows.GetCurrentProcessToken().IsElevated()
}

func relaunchElevated(args []string) error {
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	verb, err := syscall.UTF16PtrFromString("runas")
	if err != nil {
		return err
	}
	file, err := syscall.UTF16PtrFromString(exe)
	if err != nil {
		return err
	}
	param, err := syscall.UTF16PtrFromString(joinWindowsArgs(args))
	if err != nil {
		return err
	}
	cwd, err := os.Getwd()
	if err != nil {
		cwd = ""
	}
	dir, err := syscall.UTF16PtrFromString(cwd)
	if err != nil {
		return err
	}
	err = windows.ShellExecute(0, verb, file, param, dir, windows.SW_SHOWNORMAL)
	if err != nil {
		return fmt.Errorf("ShellExecute: %w", err)
	}
	return nil
}

func elevateAndWait(args []string, expectInstall bool) error {
	path := filepath.Join(os.TempDir(), fmt.Sprintf("rainmonitor-elevated-%d.json", os.Getpid()))
	_ = os.Remove(path)
	full := append(append([]string{}, args...), "--result", path)
	if err := relaunchElevated(full); err != nil {
		return fmt.Errorf("提升权限失败: %w", err)
	}
	return waitElevatedResult(path, expectInstall)
}

func joinWindowsArgs(args []string) string {
	out := make([]string, len(args))
	for i, arg := range args {
		out[i] = quoteWindowsArg(arg)
	}
	return strings.Join(out, " ")
}
