//go:build !darwin

package main

func installDarwin(cfg collectorConfig) error {
	return installServiceAs(cfg, true)
}

func darwinBootstrap(userLevel bool) error {
	_ = userLevel
	return nil
}

func darwinBootout(userLevel bool) {
	_ = userLevel
}

func installedBinaryRunning(userLevel bool) bool {
	_ = userLevel
	return false
}
