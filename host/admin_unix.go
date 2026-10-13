//go:build !windows

package main

import (
	"fmt"
	"os"
	"os/exec"
)

func processElevated() bool {
	return os.Geteuid() == 0
}

func relaunchElevated(args []string) error {
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	sudo, err := exec.LookPath("sudo")
	if err != nil {
		return fmt.Errorf("需要 root，请使用 sudo 运行")
	}
	cmd := exec.Command(sudo, append([]string{exe}, args...)...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func elevateAndWait(args []string, expectInstall bool) error {
	_ = expectInstall
	return relaunchElevated(args)
}
