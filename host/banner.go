package main

import "fmt"

const rainMonitorBanner = `
  _____       _       __  __             _ _             
 |  __ \     (_)     |  \/  |           (_) |            
 | |__) |__ _ _ _ __ | \  / | ___  _ __  _| |_ ___  _ __ 
 |  _  // _` + "`" + ` | | '_ \| |\/| |/ _ \| '_ \| | __/ _ \| '__|
 | | \ \ (_| | | | | | |  | | (_) | | | | | || (_) | |   
 |_|  \_\__,_|_|_| |_|_|  |_|\___/|_| |_|_|\__\___/|_|   
                                                         
`

func printBanner() {
	fmt.Print(rainMonitorBanner)
}

func shouldShowBanner(cfg collectorConfig, cmd string) bool {
	if cfg.Service || cfg.Quiet {
		return false
	}
	return cmd == "" || cmd == cmdRun
}
