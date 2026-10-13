//go:build darwin

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func installDarwin(cfg collectorConfig) error {
	info := detectService()
	if info.Installed && !info.UserLevel && !processElevated() {
		fmt.Println("发现旧的系统守护进程，将先用 sudo 卸载...")
		if err := elevateAndWait([]string{"uninstall"}, false); err != nil {
			return fmt.Errorf("卸载旧系统服务失败: %w", err)
		}
	}
	if processElevated() && os.Getenv("SUDO_USER") != "" {
		return fmt.Errorf("请不要用 sudo 安装。直接运行程序并选择安装，macOS 会注册为登录项")
	}
	if processElevated() {
		return installServiceAs(cfg, false)
	}
	fmt.Println("macOS 将安装为登录项（登录后自动启动），以便使用蓝牙。")
	return installServiceAs(cfg, true)
}

func launchdPlistPath(userLevel bool) string {
	if userLevel {
		home, err := os.UserHomeDir()
		if err != nil {
			home = "."
		}
		return filepath.Join(home, "Library", "LaunchAgents", serviceName+".plist")
	}
	return "/Library/LaunchDaemons/" + serviceName + ".plist"
}

func launchdDomain(userLevel bool) string {
	if userLevel {
		return "gui/" + strconv.Itoa(os.Getuid())
	}
	return "system"
}

func launchdTarget(userLevel bool) string {
	return launchdDomain(userLevel) + "/" + serviceName
}

func darwinBootout(userLevel bool) {
	_ = exec.Command("launchctl", "bootout", launchdTarget(userLevel)).Run()
	_ = exec.Command("launchctl", "unload", launchdPlistPath(userLevel)).Run()
}

func darwinBootstrap(userLevel bool) error {
	plist := launchdPlistPath(userLevel)
	if _, err := os.Stat(plist); err != nil {
		return fmt.Errorf("找不到服务配置: %s", plist)
	}
	darwinBootout(userLevel)
	if err := exec.Command("launchctl", "bootstrap", launchdDomain(userLevel), plist).Run(); err != nil {
		if err2 := exec.Command("launchctl", "load", "-w", plist).Run(); err2 != nil {
			return fmt.Errorf("launchctl bootstrap 失败: %w", err)
		}
	}
	_ = exec.Command("launchctl", "enable", launchdTarget(userLevel)).Run()
	if err := exec.Command("launchctl", "kickstart", "-k", launchdTarget(userLevel)).Run(); err != nil {
		_ = exec.Command("launchctl", "start", serviceName).Run()
	}
	if !waitInstalledBinary(userLevel, 5*time.Second) {
		return fmt.Errorf("服务未保持运行。可查看 %s", launchdLogHint(userLevel))
	}
	return nil
}

func launchdLogHint(userLevel bool) string {
	if userLevel {
		home, err := os.UserHomeDir()
		if err != nil {
			home = "~"
		}
		return filepath.Join(home, serviceName+".err.log")
	}
	return "/var/log/" + serviceName + ".err.log"
}

func waitInstalledBinary(userLevel bool, d time.Duration) bool {
	deadline := time.Now().Add(d)
	for time.Now().Before(deadline) {
		if installedBinaryRunning(userLevel) {
			return true
		}
		time.Sleep(150 * time.Millisecond)
	}
	return installedBinaryRunning(userLevel)
}

func installedBinaryRunning(userLevel bool) bool {
	dest := filepath.Join(installDir(userLevel), serviceBinaryName())
	out, err := exec.Command("pgrep", "-f", dest).Output()
	return err == nil && strings.TrimSpace(string(out)) != ""
}
